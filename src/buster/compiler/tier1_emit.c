// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A bounded Tier 1 machine consumer: selected straight-line IR -> liveness -> required locations -> allocation ->
// frame -> native records. Include after link.c, tier1_select.c, tier1_allocate.c, tier1_rematerialize.c, tier1_cfg.c,
// tier1_liveness.c, tier1_frame.c and tier1_frame_emit.c. The existing uniform-register selector remains a separate fail-closed API.
//
// Every selected input and definition requires a register at its machine phase. At an input phase, all evicted
// register values are stored before any reload reuses those registers. Definition-phase evictions are stored before
// the instruction writes its destination; source registers were captured at the preceding input phase, so the
// selector's destructive-operand repairs still apply. Integer constants may be recomputed after eviction, a zero by a
// zeroing xor: no flag is live between two nodes' records (BUSTER_TIER1_EMIT_BOUNDARY_FLAGS). Initial definitions are
// still emitted. Direct scalar calls publish symbolic CALL references, save live caller registers in fixed frame rows and
// resolve argument moves with reserved R11. A division by a constant's reciprocal takes rax and rdx, whose other values
// that live past it are saved around it in such rows too, as the CFG consumer saves them (tier1_emit_fixed).
//
// This consumer accepts a straight-line chain ending in RET, UNREACHABLE or an empty self-loop sink; every earlier
// block must branch to the immediately following block. The sink has no values to reconcile and retains its branch.
// Every other function, general control flow and phis, goes to tier1_emit_cfg.c, which allocates over the CFG and
// moves values along its edges; numeric fragment order is not a substitute.
// Indirect/variadic/wide-ABI calls, dynamic frames and direct register-to-register fragment transitions are explicit
// unsupported cases here. A variadic definition goes to tier1_emit_cfg.c too, which fills its register save area (#789).
// R11 remains reserved for selection and frame probes.
//
// Borrowed inputs are immutable and may precede allocations in the output arena, but must not overlap either arena's
// unused capacity. The arenas are disjoint. Failure restores both lengths and preserves the result. All retained
// columns have their exact logical length, without required padding. The existing encoder owns its output padding.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

_Static_assert(BUSTER_TIER1_SELECT_MAX_OBJECT_ALIGNMENT == BUSTER_TIER1_FRAME_MAX_ALIGNMENT, "selector and frame alignment bounds agree");

#define BUSTER_TIER1_EMIT_GENERAL_REGISTERS 16
// The emitter's view of who is where is by tier1_cfg.c's register locations, class * 32 + register: a general
// register's location is its number, an opmask register's (#179) 64 + k.
#define BUSTER_TIER1_EMIT_LOCATIONS BUSTER_TIER1_CFG_SPILL
#define BUSTER_TIER1_EMIT_MASK_LOCATION (32u * BUSTER_TIER1_ALLOCATE_MASK)
// The opmask registers the CFG consumer's masks take, k1 to k7, k0 its scratch: and every one a call clobbers.
#define BUSTER_TIER1_EMIT_MASK_REGISTERS 0xfeu
// A floating value's xmm register's location (#141), 32 + n, and the registers the CFG consumer's floating values take,
// xmm0 to xmm14, xmm15 its scratch (BUSTER_TIER1_SELECT_FLOAT_SCRATCH): every one a call clobbers.
#define BUSTER_TIER1_EMIT_VECTOR_LOCATION (32u * BUSTER_TIER1_ALLOCATE_VECTOR)
#define BUSTER_TIER1_EMIT_VECTOR_REGISTERS 0x7fffu
// A call's records past its node's share, a kmov store and load of each of k1 to k7 that a mask holds across it among
// them (#179), and a movsd store and load of each of xmm0 to xmm14 that a floating value holds across it, its floating
// arguments' moves and its floating result's (#141).
#define BUSTER_TIER1_EMIT_CALL_RECORDS (2 * BUSTER_TIER1_EMIT_GENERAL_REGISTERS + 3 * BUSTER_ABI_INTEGER_ARGUMENTS + 6 + 2 * 7 + 2 * (BUSTER_TIER1_SELECT_MAX_ARGUMENTS - BUSTER_TIER1_SELECT_CALL_ARGUMENTS) + 2 * 15 + 2 * BUSTER_ABI_VECTOR_ARGUMENTS + 2)
// The records a node of a fixed-register rule writes past its share, BUSTER_TIER1_SELECT_RECORDS_PER_NODE: its sequence's,
// and the saves and restores of the two fixed registers a division takes (tier1_emit_fixed).
#define BUSTER_TIER1_EMIT_FIXED_RECORDS (BUSTER_TIER1_SELECT_FIXED_RECORDS + 4 - BUSTER_TIER1_SELECT_RECORDS_PER_NODE)

typedef enum Tier1EmitError
{
    TIER1_EMIT_OK,
    TIER1_EMIT_INVALID,
    TIER1_EMIT_UNSUPPORTED,
    TIER1_EMIT_MEMORY,
    TIER1_EMIT_ALLOCATION,
    TIER1_EMIT_FRAME,
    TIER1_EMIT_TARGET,
    TIER1_EMIT_LIMIT,
    // With Tier1EmitOptions.certify, an allocation whose certificate the checker refuses (#939).
    TIER1_EMIT_CERTIFICATE,
} Tier1EmitError;

// Optional cold failure detail. It is independent of the transactional result, so the original nested cause
// survives rollback without publishing any code/arena pointers. A successful call resets it to NONE.
typedef enum Tier1EmitCause
{
    TIER1_EMIT_CAUSE_NONE,
    TIER1_EMIT_CAUSE_IR,
    TIER1_EMIT_CAUSE_LIVENESS,
    TIER1_EMIT_CAUSE_ALLOCATION,
    TIER1_EMIT_CAUSE_CFG_ALLOCATION,
    TIER1_EMIT_CAUSE_FRAME,
    TIER1_EMIT_CAUSE_CERTIFICATE,
} Tier1EmitCause;

// A code certificate's refusal in a TIER1_EMIT_CAUSE_CERTIFICATE diagnostic: its Tier1CertEmittedReason plus this, past
// every Tier1CertError.
#define BUSTER_TIER1_EMIT_CODE_CERTIFICATE 0x100u

typedef struct Tier1EmitDiagnostic Tier1EmitDiagnostic;
struct Tier1EmitDiagnostic
{
    Tier1EmitCause cause;
    uint32_t error;
};

// The access column's values the consumers take (Tier0Function.symbol_access's BUSTER_TIER0_ACCESS_*, which
// c_check_tier1.c checks are these): a symbol's address directly, or from its GOT slot; and a thread-local's (#141), the
// thread pointer plus its offset, a TPOFF32 immediate for one of the image's (local-exec) or the offset from its GOT slot
// (initial-exec), as Tier 0's.
#define BUSTER_TIER1_ACCESS_DIRECT 0
#define BUSTER_TIER1_ACCESS_GOT 1
#define BUSTER_TIER1_ACCESS_LOCAL_EXEC 2
#define BUSTER_TIER1_ACCESS_INITIAL_EXEC 3

typedef struct Tier1EmitOptions Tier1EmitOptions;
struct Tier1EmitOptions
{
    uint32_t register_mask;
    // The xmm registers the CFG consumer's floating values may take (#141), of xmm0 to xmm14; 0 for all of them.
    uint32_t vector_register_mask;
    uint32_t frame_flags;
    // With BUSTER_TIER1_FRAME_KEEP_POINTER in `frame_flags`: a function without calls keeps no frame pointer
    // (-momit-leaf-frame-pointer's leaf rule, #177; tier1_emit_frame_flags).
    bool omit_leaf_frame_pointer;
    bool vector;
    bool rematerialize;
    // Retain each record's IR origin for source/debug publication, without changing allocation or code generation.
    // The CFG certificate also asks for origins, independently of this opt-in.
    bool sources;
    // How each symbol below `symbol_count` is reached (Tier0Function.symbol_access's BUSTER_TIER0_ACCESS_*: directly,
    // through its GOT slot, or a thread-local's from the thread pointer, #141), for its address (IR_SYMBOL_ADDR). Null
    // reaches every symbol directly.
    const uint8_t* symbol_access;
    uint32_t symbol_count;
    // #939: check each allocation before code is emitted from it, by the certificate checker of its kind: a CFG
    // allocation's published program (tier1_certificate_cfg_check), a straight-line allocation's fragments
    // (tier1_allocation_certificate_check). A refusal is TIER1_EMIT_CERTIFICATE and the checker's capacity
    // TIER1_EMIT_MEMORY, each with the checker's Tier1CertError as the diagnostic's TIER1_EMIT_CAUSE_CERTIFICATE. The CFG
    // consumer's code is then certified too, decoded from its records (tier1_certificate_emitted_check): a refusal is
    // TIER1_EMIT_CERTIFICATE, the diagnostic's error its Tier1CertEmittedReason plus BUSTER_TIER1_EMIT_CODE_CERTIFICATE.
    // The C driver's --verify sets it.
    bool certify;
    Tier1EmitDiagnostic* diagnostic;
};

// The frame's flags from the options' (#177): BUSTER_TIER1_FRAME_KEEP_POINTER but in a function without `calls` where the
// options omit a leaf function's frame pointer (Tier1EmitOptions.omit_leaf_frame_pointer). A call here is one to a
// function, a machine row's none (tier1_select_function_call), as Tier 0's frame counts them (tier0_kept_pointer).
static uint32_t tier1_emit_frame_flags(const Tier1EmitOptions* options, bool calls)
{
    return options->frame_flags & (options->omit_leaf_frame_pointer && !calls ? ~BUSTER_TIER1_FRAME_KEEP_POINTER : ~0u);
}

static void tier1_emit_diagnose(const Tier1EmitOptions* options, Tier1EmitCause cause, uint32_t error)
{
    if (error != 0 && options->diagnostic != NULL)
    {
        *options->diagnostic = (Tier1EmitDiagnostic){ .cause = cause, .error = error };
    }
}

typedef struct Tier1EmitPrepared Tier1EmitPrepared;
struct Tier1EmitPrepared
{
    Tier1AllocationInput allocation;
    Tier1RematerializationInput recipes;
    // The recipes' values, which the CFG consumer completes after the frame's layout: a frame object's address's
    // displacement (tier1_emit_cfg_frame).
    uint64_t* recipe_values;
    uint32_t* value_intervals;
    uint32_t* starts;
    uint32_t* ends;
    uint32_t* sizes;
    uint32_t* alignments;
    uint32_t reserved_registers;
    uint32_t call_count;
    // A call whose aggregate register parts exceed the red zone's staging capacity reserves this many eightbytes in
    // the last frame object (#1450). Zero keeps the existing red-zone path. All calls reuse that object's maximum.
    uint32_t staging_slots;
    // The straight-line consumer's nodes of fixed-register rules (tier1_select_fixed_registers), whose records take more
    // than a node's share.
    uint32_t fixed_count;
    // The general registers with a call-save row, and those with a slot column, which the frame's reservations before
    // allocation count: the straight-line consumer's are the same, every caller-saved register where the function calls
    // and the fixed registers' sequences' (tier1_emit_prepare). The CFG consumer's rows are those of its columns that a
    // call or a fixed register's sequence saves, by the allocation (tier1_emit_cfg_saves; #177).
    uint32_t call_save_mask;
    uint32_t call_save_columns;
    // The CFG consumer's: each node's class (null for the straight-line consumer, whose values are all general), the
    // opmask registers whose masks a call saves in rows of their own after the general registers' (#179), and the
    // slots' columns for them, one per opmask register where the function has masks; and the same of the xmm registers
    // whose floating values a call saves (#141), in rows after the opmask registers', one column per xmm register where
    // the function has floating values.
    const uint8_t* classes;
    uint32_t mask_save_mask;
    uint32_t mask_columns;
    uint32_t vector_save_mask;
    uint32_t vector_columns;
    // The CFG consumer's asm bodies (#1839, TIER1_RULE_ASM), which its frame takes as calls (rsp aligned at each, no leaf
    // frame), and the callee-saved registers they touch, which its prologue saves.
    uint32_t asm_count;
    uint32_t asm_callee;
    // The widest vector a value of the function is (#141), 16, 32 or 64 bytes, or 0 without one: the vector class's
    // call-save rows and temporary slot take that width, and its moves of them that width's forms.
    uint32_t vector_bytes;
    // The CFG consumer's target has no AVX512VL (#1281): its 16- and 32-byte vectors move, spill and compute by VEX forms
    // (tier1_select_class_forms, Tier1SelectHoles.vex), and machine rows place their roles below xmm16
    // (tier1_emit_cfg_row).
    bool vex;
    // The target has AVX512F (#1281's review): zmm16 and on and EVEX's 512-bit moves, which a 64-byte row's roles keep
    // even without AVX512VL (Knights Landing's).
    bool zmm;
    // The target has AVX2 (#1281's review): without it, a 32-byte vector's VEX kernels take AVX's forms alone
    // (Tier1SelectHoles.avx2).
    bool avx2;
    // The target has no AVX (#141's SSE targets: the x86-64 baseline and x86-64-v2; `vex` is set too): its 16-byte
    // vectors move, spill and compute by legacy SSE's forms (Tier1SelectHoles.sse, BUSTER_TIER1_ENCODING_SSE).
    bool sse;
    uint32_t slot_columns;
    AbiCall call_abi;
    // The CFG consumer's long doubles (#141): each F80 node's rank among them, in node order (UINT32_MAX for any other
    // node), whose frame object holds its value, and how many there are.
    uint32_t* x87_ranks;
    uint32_t x87_count;
    // The function the CFG consumer prepared, whose long double phis its edges copy (tier1_emit_cfg_x87_phis).
    const IrFunction* function;
};

// The CFG consumer's vector encoding level (BUSTER_TIER1_ENCODING_*): legacy SSE's, VEX's or EVEX's.
static uint32_t tier1_emit_encoding(const Tier1EmitPrepared* prepared)
{
    return prepared->sse ? BUSTER_TIER1_ENCODING_SSE : prepared->vex ? BUSTER_TIER1_ENCODING_VEX : BUSTER_TIER1_ENCODING_EVEX;
}

// A machine row's placement as the CFG consumer claims it (#1284, tier1_emit_cfg_row): each of its roles' register, by
// role, and the registers its records name by number renamed (k's at k). Untrusted: the code's certificate places the
// row's records by it on its own, compares them with the emitted ones, checks that no two roles share a register but
// where the row's semantics lets them (tier1_cert_emitted_row), and proves its operands are where it reads them.
typedef struct Tier1RowPlacement Tier1RowPlacement;
struct Tier1RowPlacement
{
    // VECTOR(j)'s register.
    uint8_t vectors[BUSTER_TIER0_OPERATION_VECTORS];
    // INTEGER(j)'s general register, or FLOATING(j)'s vector one; and MASK(j)'s opmask register.
    uint8_t operands[BUSTER_TIER0_OPERATION_OPERANDS];
    uint8_t masks[BUSTER_TIER0_OPERATION_OPERANDS];
    uint8_t scratch[BUSTER_TIER0_SCRATCH];
    uint8_t mask_scratch[BUSTER_TIER0_SCRATCH];
    uint8_t integer_result;
    uint8_t floating_result;
    uint8_t mask_result;
    uint8_t renamed[8];
    // VECTOR(0)'s register up to the result's first write, record `split` (an index plus one, 0 for none), where the first
    // operand is read in its own register (#1283: tier1_emit_cfg_row_place's `early`), and that write's field
    // (BUSTER_X86_ROLE_REG, _RM or _VVVV): the fields naming VECTOR(0) before it, and that record's others, read `early`.
    uint8_t early;
    uint8_t split;
    uint8_t position;
};

// An optional scalar definition proved by the emitted-code certificate. The index in the array is its IR node;
// [first, end) is its verified kernel in the original record order, not an allocation interval. A consumer must
// still replay concrete writes after scheduling before publishing a location. UINT32_MAX boundaries mean absent.
typedef struct Tier1DebugDefinition Tier1DebugDefinition;
struct Tier1DebugDefinition
{
    uint32_t first;
    uint32_t end;
    uint8_t reg;
    uint8_t class;
    uint8_t bytes;
};

// A scalar phi's register at its defining block's entry, after any successor-placed edge moves. This is an
// assertion proved on every incoming edge by the complete emitted-code certificate, including coalesced copies.
// Its record is an original machine boundary, not a source position or an allocator lifetime.
typedef struct Tier1DebugPhi Tier1DebugPhi;
struct Tier1DebugPhi
{
    uint32_t record;
    uint32_t block;
    uint8_t reg;
    uint8_t class;
    uint8_t bytes;
};

typedef struct Tier1EmitResult Tier1EmitResult;
struct Tier1EmitResult
{
    Tier1SelectCode code;
    Tier1Liveness liveness;
    Tier1EmitPrepared prepared;
    Tier1Allocation allocation;
    Tier1Frame frame;
    uint32_t spill_stores;
    uint32_t spill_loads;
    uint32_t rematerializations;
    uint32_t transition_count;
    LinkReference* references;
    uint32_t reference_count;
    // The references' room: a call's each, and in the CFG consumer a symbol's address's each too, with its options'
    // symbols' access (Tier1EmitOptions.symbol_access).
    uint32_t reference_capacity;
    // The nodes of fixed-register rules (tier1_select_fixed_registers), whose sequences take records past a node's share.
    uint32_t fixed_count;
    // In the CFG consumer, the callee-saved registers its values may take (live across a call), and those they took,
    // which the frame saves; and how many values live across a call it gave them.
    uint32_t callee_registers;
    uint32_t callee_used;
    uint32_t crossing_values;
    const uint8_t* symbol_access;
    uint32_t symbol_access_count;
    uint32_t call_stores;
    uint32_t call_loads;
    // The CFG consumer's machine rows (#141, tier1_emit_cfg_row), and the moves it wrote around them into their roles'
    // registers and out of them (#1283): their operands' and their results'.
    uint32_t machine_rows;
    uint32_t row_moves;
    uint64_t retained_bytes;
    uint64_t transition_scratch_bytes;
    // A function the straight-line consumer doesn't take goes through tier1_emit_cfg.c, which leaves `liveness` and
    // `prepared.allocation` empty: its CFG's allocation problem (values numbered densely, value_nodes[v] their nodes,
    // node_values the inverse, BUSTER_TIER1_CFG_NONE for a node without one), tier1_cfg's result, whose allocation
    // `allocation` is, and what its edges took: move records, stubs of split edges and jumps between blocks.
    bool general;
    Tier1CfgInput cfg_input;
    Tier1CfgAllocation cfg;
    uint32_t* value_nodes;
    uint32_t* node_values;
    uint32_t edge_moves;
    uint32_t edge_stubs;
    // Recomputed values' definitions that nothing read, which the CFG consumer leaves out (tier1_emit_cfg_dead_definition).
    uint32_t dropped_definitions;
    uint32_t jumps;
    // In the CFG consumer, each of the function's labels' first record (IrFunction.labels, #789), where the driver defines
    // its symbol: its block's first record, or where the indirect jump that may go to it has a stub for that edge, the
    // stub's (tier1_emit_cfg_labels), or UINT32_MAX for an absent label no indirect branch reaches. There are label_count
    // entries, preserving the function's label indices, and none in a function without labels.
    uint32_t* label_records;
    uint32_t label_count;
    // Each return's ret, in record order: its record's index in `code`, right after its epilogue (tier1_frame_emit's),
    // `exit_count` of them.
    uint32_t* exits;
    uint32_t exit_count;
    // The frame's events (#1056): tier1_frame_emit's, at the records' boundaries, the prologue's then each epilogue's in
    // record order, which the C driver's unwind tables resolve after branch sizing; and the first record after the
    // prologue, where the body starts (-g's row of it).
    EmissionFrameEvent* frame_events;
    uint32_t frame_event_count;
    uint32_t body_record;
    // With Tier1EmitOptions.certify, that the allocation passed its certificate (#939), and for the CFG consumer's the
    // certificate's facts and edits.
    bool certified;
    uint64_t certificate_facts;
    uint64_t certificate_edits;
    // #939/#1114: both consumers' record origins (Tier1Origin and its payload), with sources or the CFG certificate.
    // Values, each block's first record (and the records' count after them) and each split edge's stub's first
    // (UINT32_MAX for an edge without a stub) support the CFG certificate, also used to prove debug definitions.
    uint32_t* origins;
    uint32_t* origin_values;
    uint32_t* block_records;
    uint32_t* stub_records;
    // Under certify, block_count contiguous rows of 16 general-register call-save owners (node ids), BUSTER_IR_NONE
    // for an unknown row. Null without call-save rows or certification. These are untrusted entry assertions, not
    // proof: the code certificate checks each row's bytes on every incoming edge, alongside the allocated locations.
    uint32_t* entry_rows;
    // Each node whose definition the consumer left out (tier1_emit_cfg_dead_definition), 1, else 0.
    uint8_t* dropped;
    // Each machine row's placement as the consumer claims it (#1284, tier1_emit_cfg_row), by node, in a function with rows;
    // null otherwise.
    Tier1RowPlacement* row_placements;
    // And the certificate of that code (tier1_certificate_emitted_check): its reason (Tier1CertEmittedReason: 0 where it
    // proved the code, a shape's it doesn't decode yet, or a refusal's, which fails the function), and its decoded edits
    // and nodes' instructions.
    bool code_checked;
    uint32_t code_reason;
    uint64_t code_edits;
    uint64_t code_originals;
    // With sources, one entry per IR node, present only after the complete emitted-code certificate succeeds.
    // Register classes use BUSTER_TIER1_ALLOCATE_GPR/VECTOR; only scalar low-byte values are represented here.
    Tier1DebugDefinition* debug_definitions;
    Tier1DebugPhi* debug_phis;
};

typedef struct Tier1EmitTransitions Tier1EmitTransitions;
struct Tier1EmitTransitions
{
    uint32_t* offsets;
    uint32_t* fragments;
    uint32_t* values;
    // Each value's register location, or UINT8_MAX; and each location's value, BUSTER_IR_NONE for none.
    uint8_t* registers;
    uint32_t owners[BUSTER_TIER1_EMIT_LOCATIONS];
};

static Tier1EmitError tier1_emit_prepare(const Tier1SelectPlan* plan, const Tier1Liveness* liveness, const Tier1EmitOptions* options, Arena* output, Tier1EmitPrepared* result)
{
    const Tier1AllocationInput* source = &liveness->allocation;
    const uint32_t count = plan->function->instruction_count;
    const uint64_t uses = (uint64_t)source->use_count + source->count;
    Tier1EmitError error = uses <= BUSTER_TIER1_ALLOCATE_MAX_USES ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    Tier1EmitPrepared prepared = { .allocation = *source };
    // A fixed-register sequence's registers (a division by a constant's reciprocal's rax and rdx) need call-save rows for
    // the values that live past it in them, as calls' registers do.
    uint32_t fixed_saves = 0;
    for (uint32_t node = 0; node < count; node += 1)
    {
        if (plan->function->op[node] == IR_CALL)
        {
            prepared.call_count += 1;
            const uint32_t arguments = plan->function->calls[plan->function->b[node]].argument_count;
            prepared.call_abi.integer_registers = prepared.call_abi.integer_registers > arguments ? prepared.call_abi.integer_registers : arguments;
        }
        const uint32_t fixed = plan->covered[node] ? 0 : tier1_select_fixed_registers(plan->patterns[node].rule);
        fixed_saves |= fixed;
        prepared.fixed_count += fixed != 0;
    }
    prepared.call_abi.stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2;
    prepared.call_save_mask = (prepared.call_count != 0 ? options->register_mask : 0) | (fixed_saves & options->register_mask);
    prepared.call_save_columns = prepared.call_save_mask;
    prepared.slot_columns = count + (uint32_t)__builtin_popcount(prepared.call_save_mask);
    uint32_t* allowed = 0;
    uint32_t* offsets = 0;
    uint32_t* positions = 0;
    uint8_t* required = 0;
    uint8_t* interval_remat = 0;
    uint8_t* operations = 0;
    uint8_t* widths = 0;
    uint8_t* flags = 0;
    uint8_t* marks = 0;
    uint64_t* values = 0;
    uint16_t* costs = 0;
    uint16_t* reload_costs = 0;
    if (error == TIER1_EMIT_OK)
    {
        bool ok = true;
        prepared.value_intervals = ir_allocate(output, count, 4, &ok);
        prepared.starts = ir_allocate(output, prepared.slot_columns, 4, &ok);
        prepared.ends = ir_allocate(output, prepared.slot_columns, 4, &ok);
        prepared.sizes = ir_allocate(output, prepared.slot_columns, 4, &ok);
        prepared.alignments = ir_allocate(output, prepared.slot_columns, 4, &ok);
        allowed = ir_allocate(output, source->count, 4, &ok);
        offsets = ir_allocate(output, (uint64_t)source->count + 1, 4, &ok);
        positions = ir_allocate(output, uses, 4, &ok);
        required = ir_allocate(output, uses, 1, &ok);
        interval_remat = ir_allocate(output, source->count, 1, &ok);
        operations = ir_allocate(output, count, 1, &ok);
        widths = ir_allocate(output, count, 1, &ok);
        flags = ir_allocate(output, count, 1, &ok);
        marks = ir_allocate(output, count, 1, &ok);
        values = ir_allocate(output, count, 8, &ok);
        costs = ir_allocate(output, count, 2, &ok);
        reload_costs = ir_allocate(output, count, 2, &ok);
        if (!ok)
        {
            error = TIER1_EMIT_MEMORY;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        for (uint32_t row = count; row < prepared.slot_columns; row += 1)
        {
            prepared.starts[row] = 0;
            prepared.ends[row] = source->position_count;
            prepared.sizes[row] = 8;
            prepared.alignments[row] = 8;
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            prepared.value_intervals[node] = BUSTER_IR_NONE;
            widths[node] = (uint8_t)tier1_select_storage_width(plan, node);
            if (options->rematerialize && !plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_CONSTANT)
            {
                operations[node] = BUSTER_TIER1_REMAT_INTEGER;
                values[node] = plan->patterns[node].immediate;
            }
        }
        prepared.recipes = (Tier1RematerializationInput){
            .operations = operations,
            .widths = widths,
            .flags = flags,
            .values = values,
            .count = count,
        };
        prepared.recipe_values = values;
        // The model's costs, where it has them, before the marks read them (tier1_rematerialization_model_costs).
        const bool costed = tier1_rematerialization_model_costs(&prepared.recipes, X86_REGISTER_RSP, costs, reload_costs, options->vector);
        prepared.recipes.costs = costs;
        prepared.recipes.reload_costs = reload_costs;
        const bool marked = costed && (BUSTER_VECTOR_PATH(options->vector) ? tier1_rematerialization_marks(&prepared.recipes, marks) : tier1_rematerialization_marks_scalar(&prepared.recipes, marks));
        if (!marked)
        {
            error = TIER1_EMIT_INVALID;
        }
        uint32_t use = 0;
        for (uint32_t interval = 0; interval < source->count; interval += 1)
        {
            const uint32_t value = source->values[interval];
            prepared.value_intervals[value] = interval;
            prepared.starts[value] = source->starts[interval];
            prepared.ends[value] = source->ends[interval];
            prepared.sizes[value] = widths[value];
            prepared.alignments[value] = widths[value];
            interval_remat[interval] = marks[value];
            offsets[interval] = use;
            positions[use] = source->starts[interval];
            required[use++] = 1;
            for (uint32_t u = source->use_offsets[interval]; u < source->use_offsets[interval + 1]; u += 1)
            {
                positions[use] = source->use_positions[u];
                required[use++] = 1;
            }
        }
        offsets[source->count] = use;
        // The prospective spill set contains every selected definition. Its alignment determines frame-base
        // reservations before allocation; reducing it to actual spills later cannot add a reservation.
        const Tier1FrameRequest prospective = {
            .slots = {
                .start = prepared.starts,
                .end = prepared.ends,
                .size = prepared.sizes,
                .alignment = prepared.alignments,
                .indices = source->values,
                .count = source->count,
                .column_count = prepared.slot_columns,
            },
            .calls = &prepared.call_abi,
            .call_count = prepared.call_count != 0,
            .flags = tier1_emit_frame_flags(options, prepared.call_count != 0),
        };
        Tier1FrameRequirements requirements;
        if (tier1_frame_requirements(&prospective, &requirements) != TIER1_FRAME_OK)
        {
            error = TIER1_EMIT_FRAME;
        }
        else
        {
            prepared.reserved_registers = requirements.reserved_registers | (1u << BUSTER_TIER1_SELECT_SCRATCH);
            prepared.allocation.registers[0] = options->register_mask & ~prepared.reserved_registers;
            for (uint32_t interval = 0; interval < source->count; interval += 1)
            {
                allowed[interval] = source->allowed_registers[interval] & prepared.allocation.registers[0];
            }
            prepared.allocation.allowed_registers = allowed;
            prepared.allocation.use_offsets = offsets;
            prepared.allocation.use_positions = positions;
            prepared.allocation.use_required = required;
            prepared.allocation.use_count = use;
            prepared.allocation.rematerializable = interval_remat;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = prepared;
    }
    return error;
}

static Tier1EmitError tier1_emit_transitions(const Tier1EmitPrepared* prepared, const Tier1Allocation* allocation, Arena* scratch, Tier1EmitTransitions* result)
{
    const Tier1AllocationInput* input = &prepared->allocation;
    Tier1EmitError error = TIER1_EMIT_OK;
    Tier1EmitTransitions transitions = {0};
    bool ok = true;
    transitions.offsets = ir_allocate(scratch, (uint64_t)input->position_count + 1, 4, &ok);
    transitions.fragments = ir_allocate(scratch, allocation->fragment_count, 4, &ok);
    transitions.values = ir_allocate(scratch, allocation->fragment_count, 4, &ok);
    transitions.registers = ir_allocate(scratch, input->value_count, 1, &ok);
    uint32_t* cursors = ir_allocate(scratch, input->position_count, 4, &ok);
    if (!ok)
    {
        error = TIER1_EMIT_MEMORY;
    }
    if (error == TIER1_EMIT_OK)
    {
        for (uint32_t location = 0; location < BUSTER_TIER1_EMIT_LOCATIONS; location += 1)
        {
            transitions.owners[location] = BUSTER_IR_NONE;
        }
        if (input->value_count != 0)
        {
            memset(transitions.registers, UINT8_MAX, input->value_count);
        }
    }
    for (uint32_t interval = 0; error == TIER1_EMIT_OK && interval < input->count; interval += 1)
    {
        const uint32_t first = allocation->fragment_offsets[interval];
        const uint32_t end = allocation->fragment_offsets[interval + 1];
        const uint32_t value = input->values[interval];
        if (first >= end || end > allocation->fragment_count || allocation->kinds[first] != BUSTER_TIER1_ALLOCATE_REGISTER)
        {
            error = TIER1_EMIT_ALLOCATION;
        }
        else
        {
            transitions.registers[value] = allocation->registers[first];
        }
        uint32_t position = input->starts[interval];
        for (uint32_t f = first; error == TIER1_EMIT_OK && f < end; f += 1)
        {
            const uint32_t kind = allocation->kinds[f];
            if (allocation->starts[f] != position || allocation->ends[f] <= position || allocation->ends[f] > input->ends[interval] || kind > BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
            {
                error = TIER1_EMIT_ALLOCATION;
            }
            else if (kind == BUSTER_TIER1_ALLOCATE_REGISTER && !tier1_select_register_allowed(allocation->registers[f]))
            {
                error = TIER1_EMIT_ALLOCATION;
            }
            else if (f != first && kind == BUSTER_TIER1_ALLOCATE_REGISTER && (allocation->kinds[f - 1] == BUSTER_TIER1_ALLOCATE_REGISTER || (position & 1) != 0))
            {
                // A definition-phase reload could overwrite an already captured source. Required definitions and
                // even input phases produced above do not create such a transition.
                error = TIER1_EMIT_UNSUPPORTED;
            }
            else
            {
                transitions.values[f] = value;
                if (f != first)
                {
                    transitions.offsets[position + 1] += 1;
                }
                position = allocation->ends[f];
            }
        }
        if (error == TIER1_EMIT_OK && position != input->ends[interval])
        {
            error = TIER1_EMIT_ALLOCATION;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        for (uint32_t position = 0; position < input->position_count; position += 1)
        {
            transitions.offsets[position + 1] += transitions.offsets[position];
            cursors[position] = transitions.offsets[position];
        }
        for (uint32_t interval = 0; interval < input->count; interval += 1)
        {
            for (uint32_t f = allocation->fragment_offsets[interval] + 1; f < allocation->fragment_offsets[interval + 1]; f += 1)
            {
                const uint32_t position = allocation->starts[f];
                transitions.fragments[cursors[position]++] = f;
            }
        }
        *result = transitions;
    }
    return error;
}

// #939: where each record of the CFG consumer's comes from, for the certificate of its code (tier1_certificate_emitted.c):
// its kind in the top 4 bits, and below them the node, edge or block it belongs to. Every record is marked after the step
// that writes it (tier1_emit_mark), with a value where the step has one (a value's transition, whose recomputation names
// it), else BUSTER_IR_NONE. Untrusted: the bridge decodes every record by its own bytes, and an origin only says which
// decoding applies. Without a certificate the columns are null and marking is a counter's update.
#define BUSTER_TIER1_ORIGIN_SHIFT 28
#define BUSTER_TIER1_ORIGIN_PAYLOAD ((1u << BUSTER_TIER1_ORIGIN_SHIFT) - 1)
typedef enum Tier1Origin
{
    // Unmarked: the bridge refuses it.
    TIER1_ORIGIN_NONE,
    // The prologue (payload 0) and an epilogue (payload 1): tier1_frame_emit's.
    TIER1_ORIGIN_FRAME,
    // The entry's: narrow parameters' extensions, and the parameters' moves and loads (tier1_emit_cfg_entry).
    TIER1_ORIGIN_ENTRY,
    // A value's move between storage at position `payload`: a fragment's transition, a fixed rule's or a call's register
    // save and restore, a call's argument moves (the node's definition phase's).
    TIER1_ORIGIN_MOVE,
    // Edge `payload`'s moves, wherever they are placed.
    TIER1_ORIGIN_EDGE,
    // Node `payload`'s own records: its rule's, a call's sequence, a branch's comparison.
    TIER1_ORIGIN_NODE,
    // A jump between blocks and stubs.
    TIER1_ORIGIN_CONTROL,
    // Node `payload`'s records the bridge doesn't decode: aggregates' copies, stack arguments, a dynamic frame's call area.
    TIER1_ORIGIN_OTHER,
} Tier1Origin;

static uint32_t tier1_origin(Tier1Origin kind, uint32_t payload)
{
    return (uint32_t)kind << BUSTER_TIER1_ORIGIN_SHIFT | (payload & BUSTER_TIER1_ORIGIN_PAYLOAD);
}

static void tier1_emit_mark(Tier1EmitBuffer* buffer, uint32_t origin, uint32_t value)
{
    uint32_t* const origins = buffer->origins;
    uint32_t* const values = buffer->origin_values;
    const uint32_t count = buffer->count;
    for (uint32_t r = buffer->marked; origins != 0 && r < count; r += 1)
    {
        origins[r] = origin;
        if (values != NULL)
        {
            values[r] = value;
        }
    }
    buffer->marked = count;
}

static bool tier1_emit_append(Tier1EmitBuffer* buffer, X86Instruction instruction)
{
    const bool room = buffer->count < buffer->capacity;
    if (room)
    {
        buffer->records[buffer->count++] = instruction;
    }
    return room;
}

// The flags live between two nodes' records, where fragment transitions and edge moves run: none. Selection keeps each
// flag's producer and its readers in one node's records (a compare and its setcc, conditional jump or cmov), as its
// constants' immediate requests already assume (tier1_select_request's live_flags 0), so that a recomputed zero is a
// zeroing xor there rather than a mov (tier1_rematerialize).
#define BUSTER_TIER1_EMIT_BOUNDARY_FLAGS 0

// A recomputed value's copy (tier1_rematerialize's) at the end of `buffer`, with its reference where it is a symbol's: a
// PC32 for its lea from rip, a LOAD_GOT for its GOT slot's load, each copy one more for link_add_code_fixups (#98).
// TIER1_EMIT_ALLOCATION where the copy can't be made or has no room for its reference, which the consumers count before
// they emit.
static Tier1EmitError tier1_emit_copy(Tier1EmitResult* result, const Tier1RematerializationInput* recipes, uint32_t index, uint8_t destination, uint8_t frame_base, Tier1EmitBuffer* buffer)
{
    Tier1Rematerialized copy = { 0 };
    const bool made = tier1_rematerialize(recipes, index, destination, frame_base, BUSTER_TIER1_EMIT_BOUNDARY_FLAGS, &copy);
    const bool symbolic = made && copy.symbol != BUSTER_TIER1_REMAT_NO_SYMBOL;
    Tier1EmitError error = made && (!symbolic || result->reference_count < result->reference_capacity) ? TIER1_EMIT_OK : TIER1_EMIT_ALLOCATION;
    if (error == TIER1_EMIT_OK && symbolic)
    {
        result->references[result->reference_count++] = (LinkReference){
            .instruction = buffer->count,
            .field = X86_FIXUP_DISPLACEMENT,
            .kind = copy.got ? BUSTER_LINK_FIXUP_LOAD_GOT : BUSTER_LINK_FIXUP_PC32,
            .target = copy.symbol,
            .addend = copy.addend,
        };
    }
    error = error != TIER1_EMIT_OK || tier1_emit_append(buffer, copy.instruction) ? error : TIER1_EMIT_MEMORY;
    result->rematerializations += error == TIER1_EMIT_OK;
    return error;
}

// A value's class: the CFG consumer's columns', or general.
static uint32_t tier1_emit_class(const Tier1EmitPrepared* prepared, uint32_t value)
{
    return prepared->classes != 0 ? prepared->classes[value] : BUSTER_TIER1_ALLOCATE_GPR;
}

// A value's spill or reload at its slot: a general register's mov of its width, a mask's kmov of its own (#179), which
// writes or reads the mask's bytes alone, a floating value's movss or movsd (#141), or a vector's vmovdqu32 or, at a target
// without AVX512VL, vmovdqu (#1281).
static X86Instruction tier1_emit_spill(const Tier1EmitPrepared* prepared, const Tier1Allocation* allocation, const Tier1Frame* frame, uint32_t value, uint8_t reg, bool load)
{
    const uint32_t width = prepared->sizes[value];
    const uint32_t class = tier1_emit_class(prepared, value);
    const bool mask = class == BUSTER_TIER1_ALLOCATE_MASK;
    const bool vector = class == BUSTER_TIER1_ALLOCATE_VECTOR;
    const Tier1SelectMaskForms* forms = tier1_select_mask_forms(width);
    const Tier1SelectFloatForms* floats = tier1_select_class_forms(width, tier1_emit_encoding(prepared));
    const uint16_t general = load ? (width == 8 ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_R32_RM32) : (width == 8 ? X86_FORM_MOV_RM64_R64 : X86_FORM_MOV_RM32_R32);
    const X86Instruction record = {
        .displacement = frame->offset[allocation->spill_ids[value]],
        .reg = mask ? (uint8_t)(reg & 7) : vector ? (uint8_t)(reg & 15) : reg,
        .rm = (uint8_t)frame->base_register,
        .mode = BUSTER_X86_MEMORY_BASE,
        .form = mask ? (load ? forms->load : forms->store) : vector ? (load ? floats->load : floats->store) : general,
    };
    return record;
}

static Tier1EmitError tier1_emit_phase(const Tier1EmitPrepared* prepared, const Tier1Allocation* allocation, const Tier1Frame* frame, Tier1EmitTransitions* transitions, uint32_t position, Tier1EmitBuffer* buffer, Tier1EmitResult* result)
{
    Tier1EmitError error = TIER1_EMIT_OK;
    const uint32_t first = transitions->offsets[position];
    const uint32_t end = transitions->offsets[position + 1];
    // Stores are a separate pass: a reload may use any register whose old value is evicted at this position.
    for (uint32_t e = first; error == TIER1_EMIT_OK && e < end; e += 1)
    {
        const uint32_t f = transitions->fragments[e];
        const uint32_t value = transitions->values[f];
        if (allocation->kinds[f - 1] == BUSTER_TIER1_ALLOCATE_REGISTER && allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_SPILL)
        {
            if (allocation->spill_ids[value] >= frame->count)
            {
                error = TIER1_EMIT_ALLOCATION;
            }
            else if (!tier1_emit_append(buffer, tier1_emit_spill(prepared, allocation, frame, value, allocation->registers[f - 1], false)))
            {
                error = TIER1_EMIT_MEMORY;
            }
            else
            {
                result->spill_stores += 1;
            }
            tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_MOVE, position), value);
        }
    }
    for (uint32_t e = first; error == TIER1_EMIT_OK && e < end; e += 1)
    {
        const uint32_t f = transitions->fragments[e];
        const uint32_t value = transitions->values[f];
        const uint8_t kind = allocation->kinds[f];
        const uint8_t previous = allocation->kinds[f - 1];
        const uint8_t reg = allocation->registers[f];
        // The fragments' registers are their class's: the emitter's view has them by location.
        const uint32_t base_location = 32 * tier1_emit_class(prepared, value);
        if (previous == BUSTER_TIER1_ALLOCATE_REGISTER && transitions->owners[base_location + allocation->registers[f - 1]] == value)
        {
            transitions->owners[base_location + allocation->registers[f - 1]] = BUSTER_IR_NONE;
        }
        if (kind == BUSTER_TIER1_ALLOCATE_REGISTER && previous == BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
        {
            // The value's definition again, rather than a reload: it has no slot. No mask (#179) and no floating value
            // (#141) has a recipe: their patterns are their classes' own, none a constant's, an address's or a copy of one.
            error = base_location != 0 ? TIER1_EMIT_ALLOCATION : tier1_emit_copy(result, &prepared->recipes, value, reg, (uint8_t)frame->base_register, buffer);
            transitions->owners[base_location + reg] = value;
        }
        else if (kind == BUSTER_TIER1_ALLOCATE_REGISTER)
        {
            const bool reload = previous == BUSTER_TIER1_ALLOCATE_SPILL && allocation->spill_ids[value] < frame->count;
            error = reload ? error : TIER1_EMIT_ALLOCATION;
            if (error == TIER1_EMIT_OK && !tier1_emit_append(buffer, tier1_emit_spill(prepared, allocation, frame, value, reg, true)))
            {
                error = TIER1_EMIT_MEMORY;
            }
            result->spill_loads += error == TIER1_EMIT_OK;
            transitions->owners[base_location + reg] = value;
        }
        tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_MOVE, position), value);
        transitions->registers[value] = kind == BUSTER_TIER1_ALLOCATE_REGISTER ? (uint8_t)(base_location + reg) : UINT8_MAX;
        result->transition_count += 1;
    }
    return error;
}

static X86Instruction tier1_emit_call_save(const Tier1EmitResult* result, uint32_t reg, bool load)
{
    const uint32_t preceding = result->prepared.call_save_mask & ((1u << reg) - 1);
    const uint32_t row = result->allocation.spill_count + (uint32_t)__builtin_popcount(preceding);
    const X86Instruction record = {
        .displacement = result->frame.offset[row],
        .reg = (uint8_t)reg,
        .rm = (uint8_t)result->frame.base_register,
        .mode = BUSTER_X86_MEMORY_BASE,
        .form = load ? X86_FORM_MOV_R64_RM64 : X86_FORM_MOV_RM64_R64,
    };
    return record;
}

static X86Instruction tier1_emit_move(uint32_t source, uint32_t destination, uint32_t width)
{
    const X86Instruction record = {
        .reg = (uint8_t)destination,
        .rm = (uint8_t)source,
        .form = width == 4 ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64,
    };
    return record;
}

// A node whose rule takes x86's fixed registers (tier1_select_fixed: a division or a shift by a register, a division by a
// constant's reciprocal): the `saved` registers, which hold other values that live past it, stored in their call-save rows
// before its records and loaded after them. Both consumers give those registers rows (tier1_emit_prepare, and
// tier1_emit_cfg_problem) and say which values live past the node, each by its own liveness. Out of line: the consumers'
// loops over every node take it for a few of a function's, and the long sequences beneath it stay out of those loops.
static __attribute__((noinline)) Tier1EmitError tier1_emit_fixed(Tier1EmitResult* emitted, uint32_t node, uint32_t saved, const Tier1SelectHoles* holes, Tier1EmitBuffer* buffer)
{
    X86Instruction records[BUSTER_TIER1_SELECT_FIXED_RECORDS];
    const uint32_t count = tier1_select_fixed(holes, records);
    const bool rows = (saved & ~emitted->prepared.call_save_mask) == 0;
    bool appended = rows && count != 0;
    for (uint32_t reg = 0; appended && reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        appended = ((saved >> reg) & 1) == 0 || tier1_emit_append(buffer, tier1_emit_call_save(emitted, reg, false));
        emitted->call_stores += (saved >> reg) & 1;
    }
    tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_MOVE, buffer->origin_base + 2 * node + 1), BUSTER_IR_NONE);
    for (uint32_t r = 0; appended && r < count; r += 1)
    {
        appended = tier1_emit_append(buffer, records[r]);
    }
    tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_NODE, node), BUSTER_IR_NONE);
    for (uint32_t reg = 0; appended && reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        appended = ((saved >> reg) & 1) == 0 || tier1_emit_append(buffer, tier1_emit_call_save(emitted, reg, true));
        emitted->call_loads += (saved >> reg) & 1;
    }
    tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_MOVE, buffer->origin_base + 2 * node + 1), BUSTER_IR_NONE);
    return appended ? TIER1_EMIT_OK : !rows ? TIER1_EMIT_ALLOCATION : count == 0 ? TIER1_EMIT_INVALID : TIER1_EMIT_MEMORY;
}

// Both consumers' calls save the values that live past them in caller-saved registers, each register in its call-save
// row, and load them back where something needs them, not right after the call (#1116): a value live across a run of
// calls that read none of it stays in its row, stored once and loaded once. Within straight-line code (the
// straight-line consumer's function, a CFG block), `owed` are the registers whose values are in their rows and not in
// the registers, which the calls clobbered, and `rows` the value each row holds: its register's owner when a call stored
// or kept it. A call doesn't store a register whose row holds its owner, unchanged since: within straight-line code an
// SSA value's bits don't change, so the row still holds them. A CFG block may carry its sole earlier predecessor's
// rows (tier1_emit_cfg_records); the code certificate proves the additional homes. A register is owed while its owner
// is unchanged, so whatever would
// change the owner, or read or store the register, takes the value back first (tier1_emit_owed).
// The rows are the 16 general registers' lanes of a vector, and the owners' first 16 locations are theirs, so the vector
// path compares and blends them whole; `vector` picks it or its scalar twin, which gives the same rows and masks.
typedef struct Tier1EmitSaves Tier1EmitSaves;
struct Tier1EmitSaves
{
    uint32_t owed;
    bool vector;
    uint32_t rows[BUSTER_TIER1_EMIT_GENERAL_REGISTERS];
};

_Static_assert(BUSTER_TIER1_EMIT_GENERAL_REGISTERS == 16, "the call-save rows are one vector of 16 lanes");

static void tier1_emit_saves_reset(Tier1EmitSaves* saves, bool vector)
{
    saves->owed = 0;
    saves->vector = vector;
    if (BUSTER_VECTOR_PATH(vector))
    {
        _mm512_storeu_si512(saves->rows, _mm512_set1_epi32((int)BUSTER_IR_NONE));
    }
    else
    {
        for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
        {
            saves->rows[reg] = BUSTER_IR_NONE;
        }
    }
}

// The owed registers whose values node needs back before its records: every one at the straight-line code's last node,
// which jumps, returns or traps, and at a `selected` node's rule that takes x86's fixed registers, whose saves store what
// the registers hold; otherwise those its uses read, `defined` (the general register its definition writes, UINT8_MAX for
// none) and those its transitions store from or load into, at its two phases from `use` on.
static uint32_t tier1_emit_owed(const Tier1SelectPlan* plan, const Tier1EmitPrepared* prepared, const Tier1Allocation* allocation, const Tier1EmitTransitions* transitions, uint32_t node, uint32_t use, uint8_t defined, bool selected,
                                bool last, uint32_t owed)
{
    const IrFunction* function = plan->function;
    uint32_t needed = last || (selected && tier1_select_node_fixed(function, &plan->patterns[node]) != 0) ? owed : 0;
    needed |= defined < BUSTER_TIER1_EMIT_GENERAL_REGISTERS ? 1u << defined : 0;
    for (uint32_t u = plan->effective_use_offsets[node]; u < plan->effective_use_offsets[node + 1]; u += 1)
    {
        const uint32_t reg = transitions->registers[plan->effective_use_values[u]];
        needed |= reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS ? 1u << reg : 0;
    }
    for (uint32_t p = 0; selected && p < BUSTER_TIER1_SELECT_VALUES; p += 1)
    {
        const uint32_t value = plan->patterns[node].values[p];
        const uint32_t reg = value < function->instruction_count ? transitions->registers[value] : UINT8_MAX;
        needed |= reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS ? 1u << reg : 0;
    }
    // The two phases' transitions are adjacent, positions `use` and `use + 1` in order.
    for (uint32_t e = transitions->offsets[use]; e < transitions->offsets[use + 2]; e += 1)
    {
        const uint32_t f = transitions->fragments[e];
        const bool general = tier1_emit_class(prepared, transitions->values[f]) == BUSTER_TIER1_ALLOCATE_GPR;
        const uint8_t from = allocation->registers[f - 1];
        const uint8_t to = allocation->registers[f];
        needed |= general && allocation->kinds[f - 1] == BUSTER_TIER1_ALLOCATE_REGISTER && from < BUSTER_TIER1_EMIT_GENERAL_REGISTERS ? 1u << from : 0;
        needed |= general && allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && to < BUSTER_TIER1_EMIT_GENERAL_REGISTERS ? 1u << to : 0;
    }
    return needed & owed;
}

// The loads of `repaid`, owed registers, from their rows, the lowest register first: an iteration per load. They are
// moves at the use phase, position `use`, of the node that needs them (#939: the code's certificate decodes them as
// edits there).
static Tier1EmitError tier1_emit_repay(Tier1EmitResult* result, Tier1EmitSaves* saves, uint32_t repaid, uint32_t use, Tier1EmitBuffer* buffer)
{
    bool appended = true;
    for (uint32_t left = repaid; appended && left != 0; left &= left - 1)
    {
        appended = tier1_emit_append(buffer, tier1_emit_call_save(result, (uint32_t)__builtin_ctz(left), true));
    }
    tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_MOVE, use), BUSTER_IR_NONE);
    result->call_loads += (uint32_t)__builtin_popcount(repaid);
    saves->owed &= ~repaid;
    return appended ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
}

// The `saved` registers whose rows hold their owners already, which a call needn't store.
static uint32_t tier1_emit_kept(const Tier1EmitSaves* saves, const Tier1EmitTransitions* transitions, uint32_t saved)
{
    uint32_t kept = 0;
    if (BUSTER_VECTOR_PATH(saves->vector))
    {
        kept = _mm512_mask_cmpeq_epi32_mask((__mmask16)saved, _mm512_loadu_si512(saves->rows), _mm512_loadu_si512(transitions->owners));
    }
    else
    {
        for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
        {
            kept |= ((saved >> reg) & 1) != 0 && saves->rows[reg] == transitions->owners[reg] ? 1u << reg : 0;
        }
    }
    return kept;
}

// After a call that saved `saved`: their rows hold their owners, and their loads are owed. An owed register the call
// didn't save is a value nothing reads past it, dropped.
static void tier1_emit_saved(Tier1EmitSaves* saves, const Tier1EmitTransitions* transitions, uint32_t saved)
{
    if (BUSTER_VECTOR_PATH(saves->vector))
    {
        _mm512_storeu_si512(saves->rows, _mm512_mask_mov_epi32(_mm512_loadu_si512(saves->rows), (__mmask16)saved, _mm512_loadu_si512(transitions->owners)));
    }
    else
    {
        for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
        {
            saves->rows[reg] = ((saved >> reg) & 1) != 0 ? transitions->owners[reg] : saves->rows[reg];
        }
    }
    saves->owed = saved;
}

// After a fixed registers' sequence, whose saves stored those registers' rows over whatever they held.
static void tier1_emit_fixed_rows(Tier1EmitSaves* saves, uint32_t fixed)
{
    if (BUSTER_VECTOR_PATH(saves->vector))
    {
        _mm512_storeu_si512(saves->rows, _mm512_mask_mov_epi32(_mm512_loadu_si512(saves->rows), (__mmask16)fixed, _mm512_set1_epi32((int)BUSTER_IR_NONE)));
    }
    else
    {
        for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
        {
            saves->rows[reg] = ((fixed >> reg) & 1) != 0 ? BUSTER_IR_NONE : saves->rows[reg];
        }
    }
}

// The allocator has already split ordinary spills. Call preservation is local to a bounded machine-register set:
// a surviving register is saved before argument shuffles and loaded back where something needs it (Tier1EmitSaves).
// Keeping this separate from fixed interval constraints avoids banning caller registers over an entire live interval.
// The registers a call at `node` saves: those holding values that live past it, here a value's interval end.
static uint32_t tier1_emit_call_saved(const Tier1EmitResult* result, const Tier1EmitTransitions* transitions, uint32_t node)
{
    const uint32_t position = node * 2 + 1;
    uint32_t saved = 0;
    for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        const uint32_t owner = transitions->owners[reg];
        if (owner != BUSTER_IR_NONE && owner != node && result->prepared.ends[owner] > position && transitions->registers[owner] == reg)
        {
            saved |= 1u << reg;
        }
    }
    return saved;
}

// The first of `count` positions in ascending order at or past `start`, by halving: `count` where there is none.
static uint32_t tier1_emit_first_at(const uint32_t* positions, uint32_t count, uint32_t start)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = positions[middle] < start ? middle + 1 : low;
        high = positions[middle] < start ? high : middle;
    }
    return low;
}

// Whether a fragment from `start` to `end` covers one of the positions, other than `own` (its value's definition: a call's
// result, or a fixed register's sequence's, which its node doesn't save).
static bool tier1_emit_covers(const uint32_t* positions, uint32_t count, uint32_t start, uint32_t end, uint32_t own)
{
    uint32_t first = tier1_emit_first_at(positions, count, start);
    first += first < count && positions[first] == own ? 1 : 0;
    return first < count && positions[first] < end;
}

// The calls' definition phases, and each fixed register's sequences' (a division's rax and rdx, a shift's rcx, of the
// registers in `columns`), `base` past the plan's node positions, in position order: the calls' in `positions` from 0, then
// each register's run from `offsets[reg]` to `offsets[reg + 1]`, the registers in order. Their room on `scratch`. The
// function's `calls` and `fixed` nodes (Tier1EmitPrepared.call_count, the fixed registers' sequences' count) say how many:
// a function with neither, most leaves, has no positions and takes no pass over its nodes, and one without fixed
// registers' sequences one; a node's registers are its mask's set bits alone (#177's measurement: a loop over all 16 for
// every node cost 0.46% of `--tier1 -c sqlite3.c`'s instructions). A count past `calls` is refused.
typedef struct Tier1EmitSavePositions Tier1EmitSavePositions;
struct Tier1EmitSavePositions
{
    uint32_t* positions;
    uint32_t call_count;
    uint32_t offsets[BUSTER_TIER1_EMIT_GENERAL_REGISTERS + 1];
};

static bool tier1_emit_save_positions(const Tier1SelectPlan* plan, uint32_t columns, uint32_t base, uint32_t calls, uint32_t fixed_nodes, Arena* scratch, Tier1EmitSavePositions* saves)
{
    const IrFunction* function = plan->function;
    const uint32_t count = calls != 0 || fixed_nodes != 0 ? function->instruction_count : 0;
    memset(saves, 0, sizeof(*saves));
    for (uint32_t node = 0; fixed_nodes != 0 && node < count; node += 1)
    {
        uint32_t fixed = plan->covered[node] ? 0 : tier1_select_node_fixed(function, &plan->patterns[node]) & columns;
        while (fixed != 0)
        {
            saves->offsets[__builtin_ctz(fixed) + 1] += 1;
            fixed &= fixed - 1;
        }
    }
    saves->offsets[0] = calls;
    for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        saves->offsets[reg + 1] += saves->offsets[reg];
    }
    bool ok = true;
    const uint32_t total = saves->offsets[BUSTER_TIER1_EMIT_GENERAL_REGISTERS];
    saves->positions = total != 0 ? ir_allocate(scratch, total, 4, &ok) : 0;
    uint32_t cursors[BUSTER_TIER1_EMIT_GENERAL_REGISTERS];
    memcpy(cursors, saves->offsets, sizeof(cursors));
    for (uint32_t node = 0; ok && saves->positions != 0 && node < count; node += 1)
    {
        const uint32_t position = base + 2 * node + 1;
        const bool call = tier1_select_function_call(function, node);
        ok = !call || saves->call_count < calls;
        if (ok && call)
        {
            saves->positions[saves->call_count++] = position;
        }
        uint32_t fixed = fixed_nodes == 0 || plan->covered[node] ? 0 : tier1_select_node_fixed(function, &plan->patterns[node]) & columns;
        while (fixed != 0)
        {
            const uint32_t reg = (uint32_t)__builtin_ctz(fixed);
            saves->positions[cursors[reg]++] = position;
            fixed &= fixed - 1;
        }
    }
    return ok;
}

// Whether general register fragment `f`, in a register of `columns`, covers a call's definition phase, where its register
// isn't one of `callee` (whose values survive a call), or one of a fixed register's sequence that takes its register, a
// callee-saved one among them (an asm body's, #1839), but its own value's definition `own`: the values
// tier1_emit_call_saved and the CFG consumer's calls save, and those a fixed register's sequence saves around it
// (tier1_emit_fixed, tier1_emit_cfg_asm), each in its register's call-save row.
static bool tier1_emit_fragment_saved(const Tier1Allocation* allocation, uint32_t f, uint32_t own, uint32_t columns, uint32_t callee, const Tier1EmitSavePositions* saves)
{
    const uint32_t reg = allocation->registers[f];
    const bool held = allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS && ((columns >> reg) & 1) != 0;
    const bool caller = held && ((callee >> reg) & 1) == 0;
    const uint32_t start = allocation->starts[f];
    const uint32_t end = allocation->ends[f];
    const bool across_call = caller && tier1_emit_covers(saves->positions, saves->call_count, start, end, own);
    const uint32_t first = held ? saves->offsets[reg] : 0;
    const uint32_t last = held ? saves->offsets[reg + 1] : 0;
    const bool across_fixed = held && first < last && tier1_emit_covers(saves->positions + first, last - first, start, end, own);
    return across_call || across_fixed;
}

// The straight-line consumer's call-save rows (#177): those of its columns, every caller-saved register where the function
// calls, that some call or fixed register's sequence saves, by the allocation (tier1_emit_fragment_saved). The rest take
// no row: in a function whose calls no value lives past, none.
static Tier1EmitError tier1_emit_saves(const Tier1SelectPlan* plan, Arena* scratch, Tier1EmitResult* emitted)
{
    const Tier1Allocation* allocation = &emitted->allocation;
    const Tier1AllocationInput* input = &emitted->prepared.allocation;
    const uint32_t columns = emitted->prepared.call_save_columns;
    const uint64_t mark = scratch->length;
    Tier1EmitSavePositions saves;
    const bool ok = tier1_emit_save_positions(plan, columns, 0, emitted->prepared.call_count, emitted->prepared.fixed_count, scratch, &saves);
    uint32_t saved = 0;
    // A register found saved isn't searched again (its fragments' columns left out), and once every column's is, nothing is.
    for (uint32_t i = 0; ok && saves.positions != 0 && saved != columns && i < allocation->interval_count; i += 1)
    {
        const uint32_t own = 2 * input->values[i] + 1;
        for (uint32_t f = allocation->fragment_offsets[i]; f < allocation->fragment_offsets[i + 1]; f += 1)
        {
            saved |= tier1_emit_fragment_saved(allocation, f, own, columns & ~saved, 0, &saves) ? 1u << allocation->registers[f] : 0;
        }
    }
    emitted->prepared.call_save_mask = saved & columns;
    arena_pop_to(scratch, mark);
    return ok ? TIER1_EMIT_OK : TIER1_EMIT_MEMORY;
}

// A call's aggregate result object (#789): the frame object its into operand is the address of, as a base register and a
// displacement from it, where `valid`.
typedef struct Tier1EmitInto Tier1EmitInto;
struct Tier1EmitInto
{
    int32_t displacement;
    uint8_t base;
    bool valid;
};

// The store form of `bytes` (1, 2, 4 or 8) from a general register.
static uint16_t tier1_emit_store_form(uint32_t bytes)
{
    return bytes == 8 ? X86_FORM_MOV_RM64_R64 : bytes == 4 ? X86_FORM_MOV_RM32_R32 : bytes == 2 ? X86_FORM_MOV_RM16_R16 : X86_FORM_MOV_RM8_R8;
}

// `size` bytes copied from `from_base` + `from_displacement` to `to_base` + `to_displacement` through general register
// `data` (#789): pieces of the largest of 8, 4, 2 and 1 bytes the size holds, one at each multiple of it and, where the
// size isn't one, the last ending at the end, over the bytes before it. No byte outside either range is read or written;
// neither base is `data`.
static bool tier1_emit_copy_bytes(Tier1EmitBuffer* buffer, uint8_t data, uint8_t to_base, int32_t to_displacement, uint8_t from_base, int32_t from_displacement, uint32_t size)
{
    const uint32_t piece = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
    const uint16_t load = piece == 8 ? X86_FORM_MOV_R64_RM64 : piece == 4 ? X86_FORM_MOV_R32_RM32 : piece == 2 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOVZX_R32_RM8;
    const uint32_t whole = size / piece;
    const uint32_t pieces = whole + (size % piece != 0 ? 1u : 0u);
    bool appended = to_base != data && from_base != data;
    for (uint32_t k = 0; appended && k < pieces; k += 1)
    {
        const int32_t offset = (int32_t)(k < whole ? piece * k : size - piece);
        const X86Instruction read = { .displacement = from_displacement + offset, .reg = data, .rm = from_base, .mode = BUSTER_X86_MEMORY_BASE, .form = load };
        const X86Instruction write = { .displacement = to_displacement + offset, .reg = data, .rm = to_base, .mode = BUSTER_X86_MEMORY_BASE, .form = tier1_emit_store_form(piece) };
        appended = tier1_emit_append(buffer, read) && tier1_emit_append(buffer, write);
    }
    return appended;
}

// The low `bytes` bytes (1 to 8) of general register `reg` stored at `base` + `displacement` (#789): one store for 1, 2,
// 4 or 8 bytes, else the largest piece of them that is left at a time, each after shifting the register down past the
// pieces stored, which changes it.
static bool tier1_emit_store_part(Tier1EmitBuffer* buffer, uint8_t reg, uint32_t bytes, uint8_t base, int32_t displacement)
{
    bool appended = bytes != 0 && bytes <= 8;
    uint32_t done = 0;
    for (uint32_t step = 0; appended && step < 3 && done < bytes; step += 1)
    {
        const uint32_t left = bytes - done;
        const uint32_t piece = left >= 8 ? 8 : left >= 4 ? 4 : left >= 2 ? 2 : 1;
        const X86Instruction store = { .displacement = displacement + (int32_t)done, .reg = reg, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = tier1_emit_store_form(piece) };
        appended = tier1_emit_append(buffer, store);
        done += piece;
        const X86Instruction shift = { .form = X86_FORM_SHR_RM64_IMM8, .rm = reg, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = (int32_t)(8 * piece) };
        appended = appended && (done == bytes || tier1_emit_append(buffer, shift));
    }
    return appended && done == bytes;
}

// The red zone's staging slot `k` below rsp (#789), where an aggregate's part waits between its copy from the aggregate's
// address and its register's load: 8 bytes each, 14 of them at most (the argument registers'). Slot 15, the zone's last,
// holds rax while a copy whose address the scratch register holds goes through rax (tier1_emit_cfg_aggregate_copy).
#define BUSTER_TIER1_EMIT_STAGING_SAVE 15u

static int32_t tier1_emit_staging(uint32_t k)
{
    return -8 * (int32_t)(k + 1);
}

// The lowest address of a part of `bytes` staged from slot `k` on (#141): a whole vector register's takes its bytes' slots
// (tier1_select_part_slots), k's and the ones below it, an eightbyte's slot k alone.
static int32_t tier1_emit_staging_part(uint32_t k, uint32_t bytes)
{
    return tier1_emit_staging(k + tier1_select_part_slots(bytes) - 1);
}

// A call argument's part waits in the reserved frame object where one is needed (#1450), otherwise below rsp as
// before. The frame object is last in the CFG consumer's row order and uses the stable frame base, also when a dynamic
// allocation or the call's outgoing area moves rsp. Its accesses are unaligned, so eightbyte alignment is sufficient.
static Tier1EmitInto tier1_emit_argument_staging(const Tier1EmitResult* emitted, uint32_t k, uint32_t bytes)
{
    const uint32_t slots = emitted->prepared.staging_slots;
    const bool framed = slots != 0;
    const uint32_t capacity = framed ? slots : BUSTER_TIER1_SELECT_STAGING_SLOTS;
    const bool valid = k <= capacity && tier1_select_part_slots(bytes) <= capacity - k && (!framed || emitted->frame.count != 0);
    const Tier1EmitInto place = {
        .displacement = !valid ? 0 : framed ? emitted->frame.offset[emitted->frame.count - 1] + (int32_t)(8 * k) : tier1_emit_staging_part(k, bytes),
        .base = (uint8_t)(framed ? emitted->frame.base_register : X86_REGISTER_RSP),
        .valid = valid,
    };
    return place;
}

// A call at `node`: the `saved` registers' values live past it in their call-save rows, each stored before the argument
// moves but those in `kept`, whose rows hold them already (#1116). Their loads are owed (Tier1EmitSaves).
static Tier1EmitError tier1_emit_call(const Tier1SelectPlan* plan, uint32_t node, const uint32_t* argument_registers, uint32_t saved, uint32_t kept, uint32_t destination, const Tier1EmitInto* into,
                                      Tier1EmitBuffer* buffer, Arena* scratch,
                                      Tier1EmitResult* result)
{
    const IrFunction* function = plan->function;
    const IrCall* call = &function->calls[function->b[node]];
    const uint64_t scratch_mark = scratch->length;
    const uint32_t result_type = function->type[node];
    // A long double's (#141) comes in st0, which the CFG consumer's `into` object takes.
    const bool general_result = result_type != IR_VOID && tier1_select_float_width(result_type) == 0 && result_type != IR_F80;
    Tier1EmitError error = TIER1_EMIT_OK;
    if ((saved & ~result->prepared.call_save_mask) != 0 || (general_result && (destination >= BUSTER_TIER1_EMIT_GENERAL_REGISTERS || ((saved >> destination) & 1))))
    {
        error = TIER1_EMIT_ALLOCATION;
    }
    // The register arguments into their registers, each where its place says (tier1_select_next_place, #141): the
    // integers and pointers into the general argument registers, and an indirect call's callee, its effective use after
    // the arguments, into R10, which no argument takes but a static chain, as one parallel move through the scratch
    // register; the floating
    // values into xmm0 to xmm7, as another through the vector class's. A move doesn't overwrite a source before it is
    // read. The arguments past their classes' registers are the CFG consumer's, which stores them first
    // (tier1_emit_cfg_stack_argument): `argument_registers` holds a location for the others alone (a general register's
    // number, or an xmm register's BUSTER_TIER1_EMIT_VECTOR_LOCATION + n), and the callee's at the arguments' count.
    // A static chain (#148) is a general argument in r10, and its indirect call's callee goes to rax instead
    // (tier1_select_callee_register).
    const bool indirect = function->op[node] == IR_CALL_INDIRECT;
    const uint8_t callee = tier1_select_callee_register(function, call);
    uint32_t sources[BUSTER_ABI_INTEGER_ARGUMENTS + 2];
    uint32_t destinations[BUSTER_ABI_INTEGER_ARGUMENTS + 2];
    uint32_t vector_sources[BUSTER_ABI_VECTOR_ARGUMENTS];
    uint32_t vector_destinations[BUSTER_ABI_VECTOR_ARGUMENTS];
    uint32_t general = 0;
    uint32_t vectors = 0;
    // A call with ABI records (#789) takes each argument where its record says. A by-value aggregate's parts in registers
    // wait in staging slots (tier1_emit_argument_staging), one after another in the arguments' order, where the CFG
    // consumer copied them from its address before the moves read and write the argument registers it may be in
    // (tier1_emit_cfg_aggregate_copy); their registers are loaded from there after the moves. One on the stack whole is
    // the CFG consumer's too, copied into its slots, and so is a part in a stack slot (#141, tier1_select_location_parts).
    const IrAbiCall* abi = call->abi != 0 ? &function->abi_calls[call->abi - 1] : 0;
    uint8_t part_registers[BUSTER_ABI_INTEGER_ARGUMENTS + BUSTER_ABI_VECTOR_ARGUMENTS];
    // Each part's bytes (a whole vector register's 16, 32 or 64, #141) and the staging slots before it.
    uint8_t part_bytes[BUSTER_ABI_INTEGER_ARGUMENTS + BUSTER_ABI_VECTOR_ARGUMENTS];
    uint32_t part_slots[BUSTER_ABI_INTEGER_ARGUMENTS + BUSTER_ABI_VECTOR_ARGUMENTS];
    uint32_t part_count = 0;
    uint32_t slot_count = 0;
    // The 64-byte parts (#141) wait in zmm16 and on instead (tier1_select_part_in_zmm): part_slots holds their register.
    uint32_t zmm_count = 0;
    Tier1SelectPlaces places = { 0 };
    for (uint32_t argument = 0; argument < call->argument_count; argument += 1)
    {
        const Tier1SelectPlace place = tier1_select_argument_place(function, call, argument, &places);
        const uint32_t location = argument_registers[argument];
        const IrPassed* record = place.kind == BUSTER_TIER1_PLACE_AGGREGATE ? tier1_select_argument_record(function, call, argument) : 0;
        Tier1SelectPart parts[2];
        const bool whole = record != 0 && tier1_select_location_whole(record->location);
        const uint32_t count = record != 0 && !whole ? tier1_select_location_parts(record->location, record->size, parts) : 0;
        uint32_t registered = 0;
        for (uint32_t k = 0; k < count; k += 1)
        {
            registered += parts[k].reg != BUSTER_ABI_REGISTER_NONE;
        }
        // An ignored aggregate (#141) passes nothing.
        const bool ignored = record != 0 && record->location.kind == BUSTER_ABI_LOCATION_IGNORE;
        error = record == 0 || whole || ignored || (count != 0 && part_count + registered <= BUSTER_ARRAY_LENGTH(part_registers)) ? error : TIER1_EMIT_ALLOCATION;
        for (uint32_t k = 0; error == TIER1_EMIT_OK && k < count; k += 1)
        {
            if (parts[k].reg != BUSTER_ABI_REGISTER_NONE)
            {
                const bool zmm = tier1_select_part_in_zmm(parts[k].bytes);
                part_registers[part_count] = parts[k].reg;
                part_bytes[part_count] = parts[k].bytes;
                part_slots[part_count] = zmm ? BUSTER_TIER1_SELECT_PART_ZMM + zmm_count : slot_count;
                slot_count += tier1_select_argument_part_slots(parts[k].bytes);
                zmm_count += zmm ? 1 : 0;
                part_count += 1;
            }
        }
        if (place.kind == BUSTER_TIER1_PLACE_GENERAL)
        {
            error = location < BUSTER_TIER1_EMIT_GENERAL_REGISTERS ? error : TIER1_EMIT_ALLOCATION;
            sources[general] = location;
            destinations[general] = tier1_select_parameter_registers[place.index];
            general += 1;
        }
        else if (place.kind == BUSTER_TIER1_PLACE_VECTOR)
        {
            error = location - BUSTER_TIER1_EMIT_VECTOR_LOCATION < 16 ? error : TIER1_EMIT_ALLOCATION;
            vector_sources[vectors] = location - BUSTER_TIER1_EMIT_VECTOR_LOCATION;
            vector_destinations[vectors] = place.index;
            vectors += 1;
        }
    }
    if (indirect)
    {
        sources[general] = argument_registers[call->argument_count];
        destinations[general] = callee;
        general += 1;
    }
    const Tier1ParallelMoves moves = {
        .sources = sources,
        .destinations = destinations,
        .count = general,
        .scratch = BUSTER_TIER1_SELECT_SCRATCH,
    };
    const Tier1ParallelMoves vector_moves = {
        .sources = vector_sources,
        .destinations = vector_destinations,
        .count = vectors,
        .scratch = BUSTER_TIER1_SELECT_FLOAT_SCRATCH,
    };
    Tier1ResolvedMoves resolved = {0};
    Tier1ResolvedMoves vector_resolved = {0};
    if (error == TIER1_EMIT_OK)
    {
        const Tier1AllocationError moved = tier1_resolve_moves(&moves, scratch, &resolved);
        error = moved == TIER1_ALLOCATION_OK ? TIER1_EMIT_OK : moved == TIER1_ALLOCATION_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_ALLOCATION;
    }
    if (error == TIER1_EMIT_OK && vectors != 0)
    {
        const Tier1AllocationError moved = tier1_resolve_moves(&vector_moves, scratch, &vector_resolved);
        error = moved == TIER1_ALLOCATION_OK ? TIER1_EMIT_OK : moved == TIER1_ALLOCATION_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_ALLOCATION;
    }
    for (uint32_t reg = 0; error == TIER1_EMIT_OK && reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
    {
        if (((saved & ~kept) >> reg) & 1)
        {
            if (!tier1_emit_append(buffer, tier1_emit_call_save(result, reg, false)))
            {
                error = TIER1_EMIT_MEMORY;
            }
            result->call_stores += 1;
        }
    }
    for (uint32_t move = 0; error == TIER1_EMIT_OK && move < resolved.count; move += 1)
    {
        if (!tier1_emit_append(buffer, tier1_emit_move(resolved.sources[move], resolved.destinations[move], 8)))
        {
            error = TIER1_EMIT_MEMORY;
        }
    }
    // The moves are full-width, so a source two arguments of different widths share is read whole. An I32 argument's
    // upper half is left as it is: the callee reads the low 32 bits alone (System V leaves the rest unspecified, #1116).
    for (uint32_t move = 0; error == TIER1_EMIT_OK && move < vector_resolved.count; move += 1)
    {
        if (!tier1_emit_append(buffer, tier1_select_float_move((uint8_t)vector_resolved.destinations[move], (uint8_t)vector_resolved.sources[move])))
        {
            error = TIER1_EMIT_MEMORY;
        }
    }
    // The saves, the arguments' moves and their narrowing are moves of values (#939).
    tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_MOVE, buffer->origin_base + 2 * node + 1), BUSTER_IR_NONE);
    // The aggregates' parts from their staging slots (#789), each its 8 bytes, or a whole vector register's its bytes (#141)
    // from its slots' lowest (tier1_emit_argument_staging): past an aggregate's end, the register's bytes are padding, which the
    // callee ignores. A 64-byte part (#141) from the zmm register it waits in (tier1_emit_cfg_aggregate_zmm).
    const uint32_t staging_capacity = result->prepared.staging_slots != 0 ? result->prepared.staging_slots : BUSTER_TIER1_SELECT_STAGING_SLOTS;
    error = error == TIER1_EMIT_OK && (slot_count > staging_capacity || zmm_count > BUSTER_ABI_VECTOR_ARGUMENTS) ? TIER1_EMIT_ALLOCATION : error;
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < part_count; k += 1)
    {
        const bool vector_part = (part_registers[k] & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
        const bool zmm = vector_part && tier1_select_part_in_zmm(part_bytes[k]);
        const Tier1EmitInto staged = zmm ? (Tier1EmitInto){ .valid = true } : tier1_emit_argument_staging(result, part_slots[k], part_bytes[k]);
        const X86Instruction load = {
            .displacement = staged.displacement,
            .reg = (uint8_t)(part_registers[k] & 0x1f),
            .rm = zmm ? (uint8_t)part_slots[k] : staged.base,
            .mode = zmm ? BUSTER_X86_REGISTER_DIRECT : BUSTER_X86_MEMORY_BASE,
            .form = vector_part ? tier1_select_part_form(part_bytes[k], true) : X86_FORM_MOV_R64_RM64,
        };
        error = !staged.valid ? TIER1_EMIT_ALLOCATION : tier1_emit_append(buffer, load) ? error : TIER1_EMIT_MEMORY;
    }
    // An aggregate result through the hidden pointer (#789): rdi, which no argument takes then, the result object's address.
    const bool hidden = abi != 0 && abi->result_size != 0 && abi->call.result.kind == BUSTER_ABI_RESULT_MEMORY;
    const bool parts_returned = abi != 0 && abi->result_size != 0 && abi->call.result.kind == BUSTER_ABI_RESULT_REGISTERS;
    error = error != TIER1_EMIT_OK || (!hidden && !parts_returned) || (into != 0 && into->valid) ? error : TIER1_EMIT_ALLOCATION;
    if (error == TIER1_EMIT_OK && hidden)
    {
        const X86Instruction address = { .displacement = into->displacement, .reg = X86_REGISTER_RDI, .rm = into->base, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_LEA_R64_M };
        error = tier1_emit_append(buffer, address) ? error : TIER1_EMIT_MEMORY;
    }
    tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_OTHER, node), BUSTER_IR_NONE);
    // A variadic call's al: how many vector registers its arguments take, an upper bound the callee's prologue reads (with
    // ABI records, its records' count, its aggregates' parts among them).
    const uint32_t vector_count = abi != 0 ? abi->call.vector_registers : vectors;
    if (error == TIER1_EMIT_OK && call->variadic)
    {
        const X86Instruction count = vector_count == 0 ? (X86Instruction){ .form = X86_FORM_XOR_R32_RM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX }
                                                       : (X86Instruction){ .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_RAX, .immediate = (int32_t)vector_count };
        error = tier1_emit_append(buffer, count) ? error : TIER1_EMIT_MEMORY;
    }
    if (error == TIER1_EMIT_OK && indirect)
    {
        error = tier1_emit_append(buffer, (X86Instruction){ .form = X86_FORM_CALL_RM64, .rm = callee }) ? error : TIER1_EMIT_MEMORY;
    }
    else if (error == TIER1_EMIT_OK)
    {
        const LinkReference reference = {
            .instruction = buffer->count,
            .field = X86_FIXUP_DISPLACEMENT,
            .kind = BUSTER_LINK_FIXUP_CALL,
            .target = function->a[node],
            .addend = 0,
        };
        const X86Instruction instruction = { .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP };
        if (result->reference_count >= result->reference_capacity || !tier1_emit_append(buffer, instruction))
        {
            error = TIER1_EMIT_MEMORY;
        }
        else
        {
            result->references[result->reference_count++] = reference;
        }
    }
    tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_NODE, node), BUSTER_IR_NONE);
    // An aggregate result in registers (#789): each part's bytes into the result object at its offset, exactly (the object
    // may be no larger than the aggregate), a general register's by tier1_emit_store_part and an xmm one's by movss or movsd,
    // or through the scratch register for any other size.
    Tier1SelectPart returned[BUSTER_TIER1_SELECT_RESULT_PARTS];
    const uint32_t returned_count = parts_returned ? tier1_select_result_parts(abi->call.result, abi->result_size, returned) : 0;
    // In the x87's registers (#141): st0's 10 bytes into the first part's 16, then st1's into the second's.
    const uint32_t x87_parts = parts_returned ? tier1_select_x87_parts(abi->call.result, abi->result_size) : 0;
    error = error == TIER1_EMIT_OK && parts_returned && returned_count == 0 && x87_parts == 0 ? TIER1_EMIT_ALLOCATION : error;
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < x87_parts; k += 1)
    {
        const X86Instruction store = { .form = X86_FORM_FSTP_M80, .rm = into->base, .mode = BUSTER_X86_MEMORY_BASE, .displacement = into->displacement + (int32_t)(16 * k) };
        error = tier1_emit_append(buffer, store) ? error : TIER1_EMIT_MEMORY;
    }
    for (uint32_t k = 0; error == TIER1_EMIT_OK && k < returned_count; k += 1)
    {
        const Tier1SelectPart part = returned[k];
        const bool vector_part = (part.reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
        const uint8_t xmm_part = (uint8_t)(part.reg & 0x1f);
        const int32_t displacement = into->displacement + part.offset;
        bool appended = true;
        if (vector_part && (part.bytes == 4 || part.bytes == 8 || part.bytes >= 16))
        {
            // A whole vector register's (#141) by its width's store.
            const uint16_t form = part.bytes >= 16 ? tier1_select_part_form(part.bytes, false) : part.bytes == 8 ? X86_FORM_MOVSD_XM64_X : X86_FORM_MOVSS_XM32_X;
            const X86Instruction store = { .displacement = displacement, .reg = xmm_part, .rm = into->base, .mode = BUSTER_X86_MEMORY_BASE, .form = form };
            appended = tier1_emit_append(buffer, store);
        }
        else if (vector_part)
        {
            const X86Instruction bits = { .form = X86_FORM_MOVQ_RM64_X, .reg = xmm_part, .rm = BUSTER_TIER1_SELECT_SCRATCH, .mode = BUSTER_X86_REGISTER_DIRECT };
            appended = tier1_emit_append(buffer, bits) && tier1_emit_store_part(buffer, BUSTER_TIER1_SELECT_SCRATCH, part.bytes, into->base, displacement);
        }
        else
        {
            appended = tier1_emit_store_part(buffer, part.reg, part.bytes, into->base, displacement);
        }
        error = appended ? error : TIER1_EMIT_MEMORY;
    }
    // A long double result (#141): st0 into the call's object, which `into` addresses.
    if (error == TIER1_EMIT_OK && result_type == IR_F80)
    {
        error = into != 0 && into->valid ? error : TIER1_EMIT_INVALID;
        const X86Instruction record = { .form = X86_FORM_FSTP_M80, .rm = error == TIER1_EMIT_OK ? into->base : 0, .mode = BUSTER_X86_MEMORY_BASE, .displacement = error == TIER1_EMIT_OK ? into->displacement : 0 };
        error = error != TIER1_EMIT_OK || tier1_emit_append(buffer, record) ? error : TIER1_EMIT_MEMORY;
    }
    tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_OTHER, node), BUSTER_IR_NONE);
    // The result into its register, a narrow one zero-extended into its canonical 32-bit storage: the callee leaves the
    // bits past its width unspecified, as clang's code does. An I32 result stays where it is in rax: the bits past its
    // 32 are unspecified in Tier 1's storage too (tier1_select_width, #1116). A floating one from xmm0 (#141).
    const uint32_t result_width = result_type != IR_VOID ? tier1_select_width(result_type) : 0;
    const uint32_t narrow_result = result_type == IR_I1 || result_type == IR_I8 ? 1 : result_type == IR_I16 ? 2 : 0;
    const uint32_t xmm = destination - BUSTER_TIER1_EMIT_VECTOR_LOCATION;
    if (error == TIER1_EMIT_OK && tier1_select_float_width(result_type) != 0)
    {
        error = xmm < 16 ? error : TIER1_EMIT_ALLOCATION;
        error = error != TIER1_EMIT_OK || xmm == 0 || tier1_emit_append(buffer, tier1_select_float_move((uint8_t)xmm, 0)) ? error : TIER1_EMIT_MEMORY;
    }
    else if (error == TIER1_EMIT_OK && narrow_result != 0)
    {
        const X86Instruction extend = { .form = narrow_result == 1 ? X86_FORM_MOVZX_R32_RM8 : X86_FORM_MOVZX_R32_RM16, .reg = (uint8_t)destination, .rm = X86_REGISTER_RAX };
        error = tier1_emit_append(buffer, extend) ? error : TIER1_EMIT_MEMORY;
    }
    else if (error == TIER1_EMIT_OK && result_width != 0 && destination != X86_REGISTER_RAX)
    {
        if (!tier1_emit_append(buffer, tier1_emit_move(X86_REGISTER_RAX, destination, result_width)))
        {
            error = TIER1_EMIT_MEMORY;
        }
    }
    tier1_emit_mark(buffer, tier1_origin(TIER1_ORIGIN_NODE, node), BUSTER_IR_NONE);
    arena_pop_to(scratch, scratch_mark);
    return error;
}

static Tier1EmitError tier1_emit_records(const Tier1SelectPlan* plan, const uint8_t* allowed_forms, bool vector, bool sources, Arena* output, Arena* scratch, Tier1EmitResult* result, uint32_t* error_node)
{
    const uint32_t count = plan->function->instruction_count;
    // The nodes of fixed-register rules, whose sequences and saves take records past their share (tier1_emit_prepare's).
    result->fixed_count = result->prepared.fixed_count;
    const uint64_t capacity = (uint64_t)count * BUSTER_TIER1_SELECT_RECORDS_PER_NODE + (uint64_t)result->allocation.fragment_count * 2 + BUSTER_TIER1_FRAME_EMIT_RECORDS * 2 + (uint64_t)result->prepared.call_count * BUSTER_TIER1_EMIT_CALL_RECORDS +
                              (uint64_t)result->fixed_count * BUSTER_TIER1_EMIT_FIXED_RECORDS;
    const bool terminal_sink = count != 0 && plan->function->op[count - 1] == IR_BR;
    const uint32_t branch_capacity = BUSTER_TIER1_FRAME_EMIT_BRANCHES * 2 + terminal_sink;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    Tier1EmitError error = capacity <= UINT32_MAX ? TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    Tier1SelectCode* code = &result->code;
    Tier1EmitBuffer buffer = { .capacity = (uint32_t)capacity, .branch_capacity = branch_capacity };
    Tier1EmitTransitions transitions = {0};
    Tier1SelectRequests requests = { .vector = vector };
    if (error == TIER1_EMIT_OK)
    {
        bool ok = true;
        code->records = ir_allocate(output, capacity, sizeof(X86Instruction), &ok);
        code->cost_ids = ir_allocate(output, capacity, 1, &ok);
        code->node_first = ir_allocate(output, (uint64_t)count + 1, 4, &ok);
        code->branches = ir_allocate(output, branch_capacity, 4, &ok);
        code->targets = ir_allocate(output, branch_capacity, 4, &ok);
        result->origins = sources ? ir_allocate(output, capacity, 4, &ok) : NULL;
        if (result->prepared.call_count != 0)
        {
            result->references = ir_allocate(output, result->prepared.call_count, sizeof(LinkReference), &ok);
            result->reference_capacity = result->prepared.call_count;
        }
        // A straight-line chain returns once at most, at its end: its prologue's events and one epilogue's.
        result->exits = ir_allocate(output, 1, 4, &ok);
        result->exit_count = 0;
        result->frame_events = ir_allocate(output, 2 * BUSTER_TIER1_FRAME_EMIT_EVENTS, sizeof(EmissionFrameEvent), &ok);
        result->frame_event_count = 0;
        if (!ok)
        {
            error = TIER1_EMIT_MEMORY;
        }
        else
        {
            buffer.records = code->records;
            buffer.branches = code->branches;
            buffer.targets = code->targets;
            buffer.events = result->frame_events;
            buffer.event_capacity = 2 * BUSTER_TIER1_FRAME_EMIT_EVENTS;
            error = tier1_emit_transitions(&result->prepared, &result->allocation, scratch, &transitions);
        }
    }
    if (error == TIER1_EMIT_OK && count != 0 && tier1_frame_emit(&result->frame, false, &buffer) != TIER1_FRAME_EMIT_OK)
    {
        error = TIER1_EMIT_FRAME;
    }
    result->body_record = buffer.count;
    // The calls' saves (Tier1EmitSaves, #1116): the chain is straight-line code from its entry to its end.
    Tier1EmitSaves saves;
    tier1_emit_saves_reset(&saves, vector);
    for (uint32_t node = 0; error == TIER1_EMIT_OK && node < count; node += 1)
    {
        code->node_first[node] = buffer.count;
        // The owed loads this node needs, first; a definition writes the register its interval starts in. A return's
        // other owed values are dead.
        const uint32_t op = plan->function->op[node];
        const bool leaves = op == IR_RET || op == IR_UNREACHABLE;
        if (saves.owed != 0)
        {
            // A by-value aggregate's parameter is computed at its node, as a frame object's address is (#789).
            const bool selected = !plan->covered[node] && op != IR_PHI && !tier1_select_entry_parameter(plan->function, node);
            const bool defines = !plan->covered[node] && op != IR_BR && op != IR_PARAM && op != IR_CALL && plan->function->type[node] != IR_VOID;
            const uint32_t repaid = tier1_emit_owed(plan, &result->prepared, &result->allocation, &transitions, node, node * 2, defines ? transitions.registers[node] : UINT8_MAX, selected, node + 1 == count && !leaves, saves.owed);
            error = repaid != 0 ? tier1_emit_repay(result, &saves, repaid, node * 2, &buffer) : error;
        }
        error = error == TIER1_EMIT_OK ? tier1_emit_phase(&result->prepared, &result->allocation, &result->frame, &transitions, node * 2, &buffer, result) : error;
        Tier1SelectHoles holes = {0};
        uint32_t argument_registers[BUSTER_ABI_INTEGER_ARGUMENTS] = {0};
        const bool calls = plan->function->op[node] == IR_CALL;
        if (error == TIER1_EMIT_OK && !plan->covered[node] && plan->function->op[node] != IR_BR)
        {
            const Tier1SelectPattern* pattern = &plan->patterns[node];
            bool allocated = plan->function->type[node] == IR_VOID || tier1_select_register_allowed(transitions.registers[node]);
            for (uint32_t p = 0; p < BUSTER_TIER1_SELECT_VALUES; p += 1)
            {
                const uint32_t value = pattern->values[p];
                allocated &= value == BUSTER_IR_NONE || tier1_select_register_allowed(transitions.registers[value]);
            }
            if (plan->function->op[node] == IR_PARAM)
            {
                const Tier1SelectPlace place = tier1_select_parameter_place(plan->function, plan->function->a[node]);
                allocated &= place.kind == BUSTER_TIER1_PLACE_GENERAL && transitions.registers[node] == tier1_select_parameter_registers[place.index];
            }
            if (calls)
            {
                const uint32_t first = plan->effective_use_offsets[node];
                const uint32_t end = plan->effective_use_offsets[node + 1];
                for (uint32_t use = first; use < end; use += 1)
                {
                    const uint32_t value = plan->effective_use_values[use];
                    argument_registers[use - first] = transitions.registers[value];
                    allocated &= tier1_select_register_allowed(transitions.registers[value]);
                }
            }
            if (!allocated)
            {
                error = TIER1_EMIT_ALLOCATION;
            }
            else if (!calls)
            {
                holes = tier1_select_holes(plan, transitions.registers, node);
            }
        }
        if (error == TIER1_EMIT_OK)
        {
            error = tier1_emit_phase(&result->prepared, &result->allocation, &result->frame, &transitions, node * 2 + 1, &buffer, result);
        }
        if (error == TIER1_EMIT_OK && calls)
        {
            const uint32_t saved = tier1_emit_call_saved(result, &transitions, node);
            error = tier1_emit_call(plan, node, argument_registers, saved, tier1_emit_kept(&saves, &transitions, saved), transitions.registers[node], 0, &buffer, scratch, result);
            tier1_emit_saved(&saves, &transitions, saved);
        }
        else if (error == TIER1_EMIT_OK && terminal_sink && node + 1 == count)
        {
            // Only the validated one-instruction final block reaches here. Keep the actual infinite loop if a
            // preceding ordinary call returns; a helper's name is not proof that replacing the loop is legal.
            if (buffer.branch_count == buffer.branch_capacity || !tier1_emit_append(&buffer, (X86Instruction){ .form = X86_FORM_JMP_REL8 }))
            {
                error = TIER1_EMIT_MEMORY;
            }
            else
            {
                buffer.branches[buffer.branch_count] = buffer.count - 1;
                buffer.targets[buffer.branch_count] = code->node_first[node];
                buffer.branch_count += 1;
            }
        }
        else if (error == TIER1_EMIT_OK && !plan->covered[node] && tier1_select_fixed_registers(plan->patterns[node].rule) != 0)
        {
            // Its fixed registers' other values that live past it, as a call's (but the destination's, which no other
            // value holds at the node's definition).
            const uint32_t saved = tier1_emit_call_saved(result, &transitions, node) & tier1_select_fixed_registers(plan->patterns[node].rule) & ~(1u << holes.destination);
            error = tier1_emit_fixed(result, node, saved, &holes, &buffer);
            tier1_emit_fixed_rows(&saves, tier1_select_fixed_registers(plan->patterns[node].rule));
        }
        else if (error == TIER1_EMIT_OK && !plan->covered[node] && plan->function->op[node] != IR_BR)
        {
            const Tier1SelectRecords selected = tier1_select_expand(&holes);
            const bool returns = plan->function->op[node] == IR_RET;
            if (selected.overflowed || (returns && (selected.count == 0 || selected.records[selected.count - 1].form != X86_FORM_RET)))
            {
                error = TIER1_EMIT_INVALID;
            }
            // A return's epilogue goes before its ret, the last record, after any request's record.
            const uint32_t first = buffer.count;
            for (uint32_t r = 0; error == TIER1_EMIT_OK && r < selected.count; r += 1)
            {
                if (returns && r + 1 == selected.count && tier1_frame_emit(&result->frame, true, &buffer) != TIER1_FRAME_EMIT_OK)
                {
                    error = TIER1_EMIT_FRAME;
                }
                else if (!tier1_emit_append(&buffer, selected.records[r]))
                {
                    error = TIER1_EMIT_MEMORY;
                }
            }
            if (error == TIER1_EMIT_OK)
            {
                tier1_select_requests_add(&requests, &selected, first, buffer.records);
            }
            if (error == TIER1_EMIT_OK && returns && result->exit_count == 0)
            {
                result->exits[0] = buffer.count - 1;
                result->exit_count = 1;
            }
            else if (error == TIER1_EMIT_OK && returns)
            {
                error = TIER1_EMIT_INVALID;
            }
        }
        if (error == TIER1_EMIT_OK && !plan->covered[node] && plan->function->type[node] != IR_VOID)
        {
            transitions.owners[transitions.registers[node]] = node;
        }
        saves.owed = leaves ? 0 : saves.owed;
        if (error != TIER1_EMIT_OK && error_node != 0)
        {
            *error_node = node;
        }
    }
    // The chain's last node took every owed value back, or left the function.
    error = saves.owed == 0 ? error : TIER1_EMIT_INVALID;
    // The function's last immediates' forms, chosen before the records are checked.
    if (error == TIER1_EMIT_OK)
    {
        tier1_select_requests_flush(&requests, buffer.records);
    }
    if (error == TIER1_EMIT_OK && x86_validate(buffer.records, buffer.count) != buffer.count)
    {
        error = TIER1_EMIT_INVALID;
    }
    for (uint32_t r = 0; error == TIER1_EMIT_OK && r < buffer.count; r += 1)
    {
        if (!x86_target_form_allowed(allowed_forms, buffer.records[r].form))
        {
            error = TIER1_EMIT_TARGET;
        }
        else
        {
            code->cost_ids[r] = x86_zen5_id(&buffer.records[r]);
            code->unknown_costs += code->cost_ids[r] == 0;
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        code->count = buffer.count;
        code->branch_count = buffer.branch_count;
        result->frame_event_count = buffer.event_count;
        code->node_first[count] = buffer.count;
        code->error_node = BUSTER_IR_NONE;
        code->arena_bytes = output->length - output_mark;
        result->transition_scratch_bytes = scratch->length - scratch_mark;
        if (sources)
        {
            // A straight chain has no edge stubs or entry copies: node_first already partitions its records.
            // Populate once after emission so a compile without -g adds no work to the record-emission loops.
            uint32_t* const origins = result->origins;
            const uint32_t* const first = code->node_first;
            for (uint32_t r = 0; r < result->body_record; r += 1)
            {
                origins[r] = tier1_origin(TIER1_ORIGIN_FRAME, 0);
            }
            for (uint32_t node = 0; node < count; node += 1)
            {
                for (uint32_t r = first[node]; r < first[node + 1]; r += 1)
                {
                    origins[r] = tier1_origin(TIER1_ORIGIN_NODE, node);
                }
            }
            for (uint32_t e = 0; e < result->frame_event_count; e += 1)
            {
                const EmissionFrameEvent event = result->frame_events[e];
                if (event.kind == EMISSION_FRAME_REMEMBER_STATE)
                {
                    for (uint32_t r = event.at.boundary; r < code->count; r += 1)
                    {
                        origins[r] = tier1_origin(TIER1_ORIGIN_FRAME, 1);
                    }
                }
            }
        }
    }
    return error;
}

#include "tier1_emit_cfg.c"
#include "tier1_certificate_emitted.c"
#include "tier1_debug_straight.c"

static __attribute__((unused)) Tier1EmitError tier1_emit_function(const Tier1SelectPlan* plan, const Tier1EmitOptions* options, const uint8_t* allowed_forms, Arena* output, Arena* scratch, Tier1EmitResult* result, uint32_t* error_node)
{
    const bool arguments = plan && plan->function && options && allowed_forms && output && scratch && output != scratch && output->base != scratch->base && result;
    Tier1EmitError error = arguments ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    const uint64_t output_mark = arguments ? output->length : 0;
    const uint64_t scratch_mark = arguments ? scratch->length : 0;
    Tier1EmitResult emitted = {0};
    if (options != NULL && options->diagnostic != NULL)
    {
        *options->diagnostic = (Tier1EmitDiagnostic){ 0 };
    }
    if (error_node != 0)
    {
        *error_node = BUSTER_IR_NONE;
    }
    if (error == TIER1_EMIT_OK)
    {
        const uint32_t flags = BUSTER_TIER1_FRAME_KEEP_POINTER | BUSTER_TIER1_FRAME_NO_RED_ZONE | BUSTER_TIER1_FRAME_PROBE_STACK;
        if ((options->frame_flags & ~flags) != 0)
        {
            error = TIER1_EMIT_UNSUPPORTED;
        }
        else if ((options->register_mask & ~tier1_liveness_register_mask()) != 0)
        {
            error = TIER1_EMIT_INVALID;
        }
    }
    // A phi-free straight-line chain keeps this consumer; every other function goes through the CFG consumer.
    bool chain = error == TIER1_EMIT_OK && tier1_emit_cfg_chain(plan);
    if (error == TIER1_EMIT_OK && !chain)
    {
        error = tier1_emit_cfg_function(plan, options, allowed_forms, output, scratch, &emitted, error_node);
    }
    if (error == TIER1_EMIT_OK && chain)
    {
        const Tier1LivenessError live = tier1_liveness_build(plan, output, scratch, BUSTER_VECTOR_PATH(options->vector), &emitted.liveness);
        tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_LIVENESS, live);
        error = live == TIER1_LIVENESS_OK ? TIER1_EMIT_OK : live == TIER1_LIVENESS_MEMORY ? TIER1_EMIT_MEMORY : live == TIER1_LIVENESS_LIMIT ? TIER1_EMIT_LIMIT : live == TIER1_LIVENESS_UNSUPPORTED ? TIER1_EMIT_UNSUPPORTED : TIER1_EMIT_INVALID;
    }
    if (error == TIER1_EMIT_OK && chain)
    {
        error = tier1_emit_prepare(plan, &emitted.liveness, options, output, &emitted.prepared);
    }
    Tier1AllocationError allocated = TIER1_ALLOCATION_OK;
    if (error == TIER1_EMIT_OK && chain)
    {
        allocated = BUSTER_VECTOR_PATH(options->vector) ? tier1_allocate(&emitted.prepared.allocation, output, &emitted.allocation) : tier1_allocate_scalar(&emitted.prepared.allocation, output, &emitted.allocation);
        tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_ALLOCATION, allocated);
        error = allocated == TIER1_ALLOCATION_OK ? TIER1_EMIT_OK : allocated == TIER1_ALLOCATION_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_ALLOCATION;
    }
    // This consumer's allocation keeps each interval in the registers it allows and moves no value from one register to
    // another (tier1_emit_transitions): a parameter bound to its incoming register, once evicted, reloads there alone,
    // which fails where another operand of the same node holds that register (#141's 128-bit halves found it). The CFG
    // consumer, whose allocation moves values between registers, takes such a function whole, from nothing this one made.
    if (error == TIER1_EMIT_ALLOCATION && chain && allocated == TIER1_ALLOCATION_CONSTRAINT)
    {
        arena_pop_to(output, output_mark);
        arena_pop_to(scratch, scratch_mark);
        emitted = (Tier1EmitResult){ 0 };
        if (options->diagnostic != NULL)
        {
            *options->diagnostic = (Tier1EmitDiagnostic){ 0 };
        }
        chain = false;
        error = tier1_emit_cfg_function(plan, options, allowed_forms, output, scratch, &emitted, error_node);
    }
    if (error == TIER1_EMIT_OK && chain && (options->certify || options->sources))
    {
        // The fragments' certificate (#939): a straight line has no edges, so the fragment checker is the whole proof of
        // its allocation; its scratch on `scratch`, popped after.
        const uint64_t mark = scratch->length;
        const size_t size = tier1_interval_scratch_size(&emitted.prepared.allocation, &emitted.allocation);
        void* buffer = size != 0 ? arena_push(scratch, size, 64) : 0;
        Tier1IntervalReport report = { 0 };
        const Tier1CertError certified = buffer == 0 ? TIER1_CERT_OK : tier1_allocation_certificate_check(&emitted.prepared.allocation, &emitted.allocation, buffer, size, &report);
        tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_CERTIFICATE, certified);
        error = buffer == 0 || certified == TIER1_CERT_CAPACITY ? TIER1_EMIT_MEMORY : certified == TIER1_CERT_OK ? TIER1_EMIT_OK : TIER1_EMIT_CERTIFICATE;
        emitted.certified = error == TIER1_EMIT_OK;
        arena_pop_to(scratch, mark);
    }
    if (error == TIER1_EMIT_OK && chain)
    {
        error = tier1_emit_saves(plan, scratch, &emitted);
    }
    if (error == TIER1_EMIT_OK && chain)
    {
        const uint32_t spill_count = emitted.allocation.spill_count;
        const uint32_t call_slots = (uint32_t)__builtin_popcount(emitted.prepared.call_save_mask);
        const uint32_t* indices = emitted.allocation.spill_values;
        if (call_slots != 0)
        {
            bool ok = true;
            uint32_t* combined = ir_allocate(scratch, (uint64_t)spill_count + call_slots, 4, &ok);
            if (!ok)
            {
                error = TIER1_EMIT_MEMORY;
            }
            else
            {
                if (spill_count != 0)
                {
                    memcpy(combined, indices, (uint64_t)spill_count * sizeof(*combined));
                }
                // Each row's column, its register's of the columns (#177).
                uint32_t slot = spill_count;
                for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
                {
                    if (((emitted.prepared.call_save_mask >> reg) & 1) != 0)
                    {
                        combined[slot++] = plan->function->instruction_count + (uint32_t)__builtin_popcount(emitted.prepared.call_save_columns & ((1u << reg) - 1));
                    }
                }
                indices = combined;
            }
        }
        const Tier1FrameRequest request = {
            .slots = {
                .start = emitted.prepared.starts,
                .end = emitted.prepared.ends,
                .size = emitted.prepared.sizes,
                .alignment = emitted.prepared.alignments,
                .indices = indices,
                .count = spill_count + call_slots,
                .column_count = emitted.prepared.slot_columns,
            },
            .calls = &emitted.prepared.call_abi,
            .call_count = emitted.prepared.call_count != 0,
            .flags = tier1_emit_frame_flags(options, emitted.prepared.call_count != 0),
        };
        if (error == TIER1_EMIT_OK)
        {
            const Tier1FrameError framed = BUSTER_VECTOR_PATH(options->vector) ? tier1_frame_plan(&request, output, scratch, &emitted.frame) : tier1_frame_plan_scalar(&request, output, scratch, &emitted.frame);
            tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_FRAME, framed);
            error = framed == TIER1_FRAME_OK ? TIER1_EMIT_OK : framed == TIER1_FRAME_CAPACITY ? TIER1_EMIT_MEMORY : framed == TIER1_FRAME_OVERFLOW ? TIER1_EMIT_LIMIT : TIER1_EMIT_FRAME;
        }
    }
    if (error == TIER1_EMIT_OK && chain)
    {
        error = tier1_emit_records(plan, allowed_forms, BUSTER_VECTOR_PATH(options->vector), options->sources, output, scratch, &emitted, error_node);
    }
    if (error == TIER1_EMIT_OK && chain && options->sources)
    {
        error = tier1_debug_straight(plan, options, output, &emitted);
    }
    if (error == TIER1_EMIT_OK && !chain && (options->certify || options->sources))
    {
        // Decode the final CFG consumer's code, including a straight-line allocation that fell back to this
        // consumer. Debug definitions require the same independent proof even without --verify. Unsupported
        // shapes retain their existing reason and publish no definition claims; a refusal remains transactional.
        bool room = true;
        Tier1DebugDefinition* definitions = options->sources ? ir_allocate(output, plan->function->instruction_count, sizeof(Tier1DebugDefinition), &room) : NULL;
        Tier1DebugPhi* phis = options->sources ? ir_allocate(output, plan->function->instruction_count, sizeof(Tier1DebugPhi), &room) : NULL;
        Tier1CertReport report = { 0 };
        Tier1CertEmittedStats stats = { 0 };
        const Tier1CertEmittedReason reason = room ? tier1_certificate_emitted_check_debug(plan, &emitted, output, scratch, definitions, phis, &report, &stats) : TIER1_CERT_EMITTED_CAPACITY;
        emitted.code_checked = true;
        emitted.code_reason = reason;
        emitted.code_edits = stats.edits + stats.edge_edits;
        emitted.code_originals = stats.originals;
        emitted.debug_definitions = reason == TIER1_CERT_EMITTED_OK ? definitions : NULL;
        emitted.debug_phis = reason == TIER1_CERT_EMITTED_OK ? phis : NULL;
        if (reason >= TIER1_CERT_EMITTED_REFUSED)
        {
            tier1_emit_diagnose(options, TIER1_EMIT_CAUSE_CERTIFICATE, BUSTER_TIER1_EMIT_CODE_CERTIFICATE | reason);
            error = reason == TIER1_CERT_EMITTED_CAPACITY ? TIER1_EMIT_MEMORY : TIER1_EMIT_CERTIFICATE;
        }
    }
    if (arguments)
    {
        arena_pop_to(scratch, scratch_mark);
        if (error == TIER1_EMIT_OK)
        {
            emitted.retained_bytes = output->length - output_mark;
            *result = emitted;
        }
        else
        {
            arena_pop_to(output, output_mark);
        }
    }
    return error;
}
