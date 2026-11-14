// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// The C driver's --tier1 (#138's piece for #140): a body's typed stream compiled by Tier 1's pipeline rather than by
// Tier 0: lowered to the shared IR (ir_lower.c), promoted to SSA (ssa_ir.c), optimized (ir_optimize.c's SCCP, dead
// code and CFG cleanup), selected (tier1_select.c) using the optimizer's retained final verification analyses
// (ir_optimize_verified.c, #1055), and emitted (tier1_emit.c, whose CFG consumer allocates a general CFG's values
// over it, tier1_cfg.c), each step with --scalar by its scalar twin. The emission's check_program_emit_tier1 calls
// it for each body and gives Tier 0 only explicit unsupported bodies, so that a program of both tiers' functions is
// one System V program. Tier 1's code is the emission's tier-neutral result (EmittedCode, #1056) with its frame
// emitter's own frame events (check_tier1_frame), which the emission resolves into its unwind table as it does Tier
// 0's; and with -g, its source events at the function's granularity (the body's start and the epilogue:
// docs/tier1.md's "Debug information"). Include after tier1_emit.c and c_check_program_emit.c.

#include "ir_optimize_verified.c"
#include "ir_wide.c"
#include "ir_vector_split.c"
#include "ir_truth.c"
#include "ir_if_convert.c"
#include "ir_thread.c"
#include "tier1_schedule_ir.c"
#include "c_check_tier1_schedule.c"

_Static_assert(BUSTER_TIER1_ACCESS_DIRECT == BUSTER_TIER0_ACCESS_DIRECT && BUSTER_TIER1_ACCESS_GOT == BUSTER_TIER0_ACCESS_GOT && BUSTER_TIER1_ACCESS_LOCAL_EXEC == BUSTER_TIER0_ACCESS_LOCAL_EXEC &&
                   BUSTER_TIER1_ACCESS_INITIAL_EXEC == BUSTER_TIER0_ACCESS_INITIAL_EXEC,
               "Tier 1 reads Tier 0's access column");

// The emitter's nested cause domains, indexed by Tier1EmitCause. The public emitter error wraps these; the outcome
// keeps the nested cause and error too, and the full table is regression-tested below (test-check-lanes).
static const CheckTier1Cause check_tier1_emission_causes[] = {
    CHECK_TIER1_CAUSE_EMISSION,
    CHECK_TIER1_CAUSE_IR,
    CHECK_TIER1_CAUSE_LIVENESS,
    CHECK_TIER1_CAUSE_ALLOCATION,
    CHECK_TIER1_CAUSE_CFG_ALLOCATION,
    CHECK_TIER1_CAUSE_FRAME,
    CHECK_TIER1_CAUSE_CERTIFICATE,
};
_Static_assert(BUSTER_ARRAY_LENGTH(check_tier1_emission_causes) == TIER1_EMIT_CAUSE_CERTIFICATE + 1, "every Tier1EmitCause has a CheckTier1Cause");

// The test control's raw emitter handoff, from the outcome's cause domain. A forced nested failure uses the same
// outcome classification and wrapper path as a real Tier1EmitDiagnostic.
static Tier1EmitCause check_tier1_fault_emission_cause(CheckTier1Cause cause)
{
    switch (cause)
    {
        case CHECK_TIER1_CAUSE_IR: return TIER1_EMIT_CAUSE_IR;
        case CHECK_TIER1_CAUSE_LIVENESS: return TIER1_EMIT_CAUSE_LIVENESS;
        case CHECK_TIER1_CAUSE_ALLOCATION: return TIER1_EMIT_CAUSE_ALLOCATION;
        case CHECK_TIER1_CAUSE_CFG_ALLOCATION: return TIER1_EMIT_CAUSE_CFG_ALLOCATION;
        case CHECK_TIER1_CAUSE_FRAME: return TIER1_EMIT_CAUSE_FRAME;
        case CHECK_TIER1_CAUSE_CERTIFICATE: return TIER1_EMIT_CAUSE_CERTIFICATE;
        default: return TIER1_EMIT_CAUSE_NONE;
    }
}

static CheckTier1Outcome check_tier1_failure(CheckTier1Stage stage, CheckTier1Cause cause, uint32_t error, uint32_t instruction, uint32_t detail)
{
    CheckTier1Category category = CHECK_TIER1_INTERNAL;
    switch (cause)
    {
        case CHECK_TIER1_CAUSE_ADMISSION:
            category = error == BUSTER_CHECK_TIER1_NAKED || error == BUSTER_CHECK_TIER1_RETURNS_TWICE ? CHECK_TIER1_UNSUPPORTED : category;
            break;
        case CHECK_TIER1_CAUSE_IR:
            category = error == IR_ERROR_UNSUPPORTED ? CHECK_TIER1_UNSUPPORTED : error == IR_ERROR_MEMORY || error == IR_ERROR_LIMIT ? CHECK_TIER1_RESOURCE_FAILURE : category;
            break;
        case CHECK_TIER1_CAUSE_DOMINATORS:
            category = error == DOMINATOR_NO_MEMORY || error == DOMINATOR_MATRIX_LIMIT ? CHECK_TIER1_RESOURCE_FAILURE : category;
            break;
        case CHECK_TIER1_CAUSE_SELECTION:
            category = error == TIER1_SELECT_ERROR_UNSUPPORTED || error == TIER1_SELECT_ERROR_TARGET ? CHECK_TIER1_UNSUPPORTED :
                       error == TIER1_SELECT_ERROR_MEMORY || error == TIER1_SELECT_ERROR_LIMIT ? CHECK_TIER1_RESOURCE_FAILURE : category;
            break;
        case CHECK_TIER1_CAUSE_EMISSION:
            category = error == TIER1_EMIT_UNSUPPORTED || error == TIER1_EMIT_TARGET ? CHECK_TIER1_UNSUPPORTED :
                       error == TIER1_EMIT_MEMORY || error == TIER1_EMIT_LIMIT ? CHECK_TIER1_RESOURCE_FAILURE : category;
            break;
        case CHECK_TIER1_CAUSE_ARENA:
            category = error == BUSTER_CHECK_TIER1_ARENA_RESERVE || error == BUSTER_CHECK_TIER1_ARENA_OUTPUT || error == BUSTER_CHECK_TIER1_ARENA_SCHEDULE ?
                           CHECK_TIER1_RESOURCE_FAILURE : category;
            break;
        case CHECK_TIER1_CAUSE_LIVENESS:
            category = error == TIER1_LIVENESS_UNSUPPORTED ? CHECK_TIER1_UNSUPPORTED :
                       error == TIER1_LIVENESS_MEMORY || error == TIER1_LIVENESS_LIMIT ? CHECK_TIER1_RESOURCE_FAILURE : category;
            break;
        case CHECK_TIER1_CAUSE_ALLOCATION:
            category = error == TIER1_ALLOCATION_CAPACITY ? CHECK_TIER1_RESOURCE_FAILURE : category;
            break;
        case CHECK_TIER1_CAUSE_CFG_ALLOCATION:
            category = error == TIER1_CFG_CAPACITY ? CHECK_TIER1_RESOURCE_FAILURE : category;
            break;
        case CHECK_TIER1_CAUSE_FRAME:
            category = error == TIER1_FRAME_CAPACITY || error == TIER1_FRAME_OVERFLOW ? CHECK_TIER1_RESOURCE_FAILURE : category;
            break;
        case CHECK_TIER1_CAUSE_CERTIFICATE:
            // A refused certificate is an allocation the allocator got wrong, or code the emitter got wrong (its
            // Tier1CertEmittedReason plus BUSTER_TIER1_EMIT_CODE_CERTIFICATE, #939): internal, never Tier 0's to hide. Either
            // checker's capacity is a resource failure.
            category = error == TIER1_CERT_CAPACITY || error == (BUSTER_TIER1_EMIT_CODE_CERTIFICATE | TIER1_CERT_EMITTED_CAPACITY) ? CHECK_TIER1_RESOURCE_FAILURE : category;
            break;
        default:
            break;
    }
    const CheckTier1Outcome outcome = {
        .stage = stage,
        .category = category,
        .cause = cause,
        .error = error,
        .instruction = instruction,
        .detail = detail,
        .operation = IR_OP_COUNT,
    };
    return check_tier1_policy(outcome, false);
}

static CheckTier1Outcome check_tier1_step(CheckTier1Stage stage, CheckTier1Cause cause, uint32_t error, uint32_t instruction, uint32_t detail, const CheckTier1Fault* fault)
{
    if (error == 0 && fault != NULL && fault->stage == stage)
    {
        cause = fault->cause;
        error = fault->error;
        instruction = fault->instruction;
        detail = fault->detail;
    }
    const CheckTier1Outcome success = { .category = CHECK_TIER1_SUCCESS, .instruction = BUSTER_IR_NONE, .operation = IR_OP_COUNT };
    return error == 0 ? success : check_tier1_failure(stage, cause, error, instruction, detail);
}

// The kinds of the call at `node` (CheckTier1CallKind's bits), for a selection refusal there (#1116): each shape of it
// that the selector's call boundary doesn't take, by the selector's own predicates (tier1_select_call_supported's), so
// that the report follows the boundary as it grows; and whether it is indirect or variadic, which the boundary takes.
// An ABI record the boundary refuses, the call's or an argument's, is `abi-record`; an argument without one is named by
// its type. Staging past the red zone is frame storage (#1450), not a selection refusal. Every argument is looked at,
// those past the boundary's limit too (the verifier bounds them all), so that a call past the limit still names its
// other kinds.
static uint32_t check_tier1_call_kinds(const IrFunction* ir, uint32_t node)
{
    const bool call = node < ir->instruction_count && (ir->op[node] == IR_CALL || ir->op[node] == IR_CALL_INDIRECT) && ir->b[node] < ir->call_count;
    uint32_t kinds = 0;
    if (call)
    {
        const IrCall* descriptor = &ir->calls[ir->b[node]];
        const uint32_t result = ir->type[node];
        kinds |= (ir->op[node] == IR_CALL_INDIRECT ? 1u : 0) << CHECK_TIER1_CALL_INDIRECT;
        kinds |= (descriptor->variadic ? 1u : 0) << CHECK_TIER1_CALL_VARIADIC;
        kinds |= (tier1_select_call_abi(ir, node, descriptor) ? 0 : 1u) << CHECK_TIER1_CALL_ABI_RECORD;
        kinds |= (descriptor->argument_count > BUSTER_TIER1_SELECT_MAX_ARGUMENTS ? 1u : 0) << CHECK_TIER1_CALL_STACK_ARGUMENTS;
        kinds |= (tier1_select_call_arity(descriptor) ? 0 : 1u) << CHECK_TIER1_CALL_ARGUMENT_COUNT;
        for (uint32_t argument = 0; argument < descriptor->argument_count; argument += 1)
        {
            const uint32_t type = tier1_select_argument_type(ir, descriptor, argument);
            const IrPassed* record = tier1_select_argument_record(ir, descriptor, argument);
            const bool taken = record != 0 ? tier1_select_record_supported(record, type) : tier1_select_call_argument(type);
            const uint32_t shape = record != 0 ? CHECK_TIER1_CALL_ABI_RECORD : type == IR_I1 || type == IR_I8 || type == IR_I16 ? CHECK_TIER1_CALL_NARROW_ARGUMENT : CHECK_TIER1_CALL_OTHER_ARGUMENT;
            kinds |= (taken ? 0 : 1u) << shape;
        }
        kinds |= (tier1_select_call_result(result) ? 0 : 1u) << CHECK_TIER1_CALL_OTHER_RESULT;
    }
    return kinds;
}

static uint32_t check_tier1_operation(CheckTier1Outcome outcome)
{
    // This is only the legacy bounded histogram. The outcome itself never truncates an error or a location.
    return outcome.cause != CHECK_TIER1_CAUSE_SELECTION ? BUSTER_CHECK_TIER1_OPERATIONS :
           outcome.error == TIER1_SELECT_ERROR_UNSUPPORTED ? outcome.operation :
           outcome.error < BUSTER_CHECK_TIER1_OPERATIONS - IR_OP_COUNT ? IR_OP_COUNT + outcome.error : BUSTER_CHECK_TIER1_OPERATIONS;
}

static void check_tier1_lane_release(CheckTier1Lane* lane)
{
    for (uint32_t k = 0; k < BUSTER_CHECK_TIER1_STEPS; k += 1)
    {
        arena_release(&lane->steps[k]);
    }
    arena_release(&lane->scratch);
    arena_release(&lane->forms);
    *lane = (CheckTier1Lane){ 0 };
}

_Static_assert(BUSTER_CHECK_TIER1_PASS_SCCP == BUSTER_IR_OPTIMIZE_SCCP && BUSTER_CHECK_TIER1_PASS_DCE == BUSTER_IR_OPTIMIZE_DCE &&
                   BUSTER_CHECK_TIER1_PASS_CFG == BUSTER_IR_OPTIMIZE_CFG,
               "--tier1-passes= passes the optimizer's own bits");

// The passes `lane`'s bodies run (BUSTER_CHECK_TIER1_PASS_*): the default pipeline's, turned over where --tier1-passes=
// turned them (CheckTier1Lane.passes, #139).
static uint32_t check_tier1_passes_on(const CheckTier1Lane* lane)
{
    return (BUSTER_CHECK_TIER1_PASSES_DEFAULT ^ lane->passes) & BUSTER_CHECK_TIER1_PASSES_ALL;
}

// The lane's arenas, reserved at its first body and emptied for each, and the forms `target` allows, built again only
// for a target other than the last one's. The optional passes' arenas (#866's solver's, step 7, and SROA's and LICM's
// from --tier1-passes=, steps 8 and 9) are reserved only where `optional` asks for them (bit k for step
// BUSTER_CHECK_TIER1_STEPS_ALWAYS + k), so that --tier1 alone maps no address space it never uses. Returns false if one
// can't be reserved.
static bool check_tier1_lane_prepare(CheckTier1Lane* lane, const X86Target* target, uint8_t optional)
{
    if (!lane->reserved || lane->optional_reserved != optional)
    {
        for (uint32_t k = 0; k < BUSTER_CHECK_TIER1_STEPS; k += 1)
        {
            arena_release(&lane->steps[k]);
        }
        arena_release(&lane->scratch);
        arena_release(&lane->forms);
        for (uint32_t k = 0; k < BUSTER_CHECK_TIER1_STEPS; k += 1)
        {
            const bool reserve = k < BUSTER_CHECK_TIER1_STEPS_ALWAYS || ((optional >> (k - BUSTER_CHECK_TIER1_STEPS_ALWAYS)) & 1) != 0;
            lane->steps[k] = reserve ? arena_reserve(UINT64_C(1) << 34) : (Arena){ 0 };
        }
        lane->scratch = arena_reserve(UINT64_C(1) << 36);
        lane->forms = arena_reserve_small(X86_FORM_COUNT, X86_FORM_COUNT);
        lane->allowed = BUSTER_ARENA_PUSH_ARRAY(&lane->forms, uint8_t, X86_FORM_COUNT);
        lane->reserved = true;
        lane->optional_reserved = optional;
        // The new forms arena is empty: the target's forms must be built again even for the same target.
        lane->targeted = false;
    }
    bool valid = lane->scratch.base != 0 && lane->allowed != 0;
    for (uint32_t k = 0; k < BUSTER_CHECK_TIER1_STEPS; k += 1)
    {
        const bool reserved = k < BUSTER_CHECK_TIER1_STEPS_ALWAYS || ((optional >> (k - BUSTER_CHECK_TIER1_STEPS_ALWAYS)) & 1) != 0;
        valid = valid && (!reserved || lane->steps[k].base != 0);
        arena_pop_to(&lane->steps[k], 0);
    }
    arena_pop_to(&lane->scratch, 0);
    const bool same = lane->targeted && lane->target.features == target->features && lane->target.extensions == target->extensions &&
                      lane->target.architecture == target->architecture && lane->target.native_znver5 == target->native_znver5;
    if (valid && !same)
    {
        x86_target_forms(target, lane->allowed);
        lane->target = *target;
        lane->targeted = true;
    }
    return valid;
}

// Tier 1's code's frame as the emission takes it (#1056): its frame emitter's own events (tier1_frame_emit, at the
// records it wrote: the prologue's, then each return's epilogue's, bracketed by the body's rules remembered and restored
// after its ret), checked against the exits it records: each a ret in the code, each bracket's restoring right after
// one, in order. A frame whose last record is its last return's ret is closed: the emission drops that epilogue's
// bracket (unwind_rows_canonical), so that its CFI is the fixed frame's where the frame is one. Also -g's source events
// (EmittedCode's): the body's start, generated code at the declaration's line, and the closing brace's row from the
// last epilogue's first record where the frame is closed. Tier 1's IR keeps no source positions yet: its rows are the
// function's alone. Returns false for a frame whose events aren't its exits': an internal publication failure, never an
// unsupported-input fallback.
static bool check_tier1_frame(const Tier1EmitResult* emitted, uint32_t count, uint32_t* epilogue)
{
    const X86Instruction* records = emitted->code.records;
    bool valid = emitted->frame_event_count == 0 || emitted->frame_events != 0;
    for (uint32_t k = 0; valid && k < emitted->exit_count; k += 1)
    {
        valid = emitted->exits[k] < count && records[emitted->exits[k]].form == X86_FORM_RET && (k == 0 || emitted->exits[k] > emitted->exits[k - 1]);
    }
    // Each bracket closes right after an exit's ret, in the exits' order: the epilogue its remembering opens.
    uint32_t exit = 0;
    uint32_t opened = UINT32_MAX;
    *epilogue = emitted->exit_count != 0 ? emitted->exits[emitted->exit_count - 1] : count;
    for (uint32_t i = 0; valid && i < emitted->frame_event_count; i += 1)
    {
        const EmissionFrameEvent event = emitted->frame_events[i];
        if (event.kind == EMISSION_FRAME_REMEMBER_STATE)
        {
            valid = opened == UINT32_MAX;
            opened = event.at.boundary;
        }
        else if (event.kind == EMISSION_FRAME_RESTORE_STATE)
        {
            while (exit < emitted->exit_count && emitted->exits[exit] + 1 < event.at.boundary)
            {
                exit += 1;
            }
            valid = opened != UINT32_MAX && exit < emitted->exit_count && emitted->exits[exit] + 1 == event.at.boundary;
            *epilogue = valid && exit + 1 == emitted->exit_count ? opened : *epilogue;
            opened = UINT32_MAX;
        }
    }
    valid = valid && opened == UINT32_MAX;
    // Open: the code doesn't end in its last return's ret, so no epilogue row.
    const bool closed = count != 0 && emitted->exit_count != 0 && emitted->exits[emitted->exit_count - 1] == count - 1;
    *epilogue = closed ? *epilogue : count;
    return valid;
}

#include "c_check_tier1_debug.c"

// A function's IR as Tier 1's passes take it, after its lowering, its calls' expansion (#2541) and #866's solver: its
// verified-ness, which promotion then doesn't check again, and the unexpanded lowering, which promotion takes where it
// refuses the expansion or finds it past its limits (null where nothing was expanded).
typedef struct CheckTier1PassesInput CheckTier1PassesInput;
struct CheckTier1PassesInput
{
    const IrFunction* function;
    bool verified;
    const IrFunction* unexpanded;
};

// What Tier 1's passes made of a function (check_tier1_passes): the optimized function and its verified analysis,
// --tier1-schedule=pre's counts, and whether promotion took the unexpanded lowering instead of the expansion.
typedef struct CheckTier1Passes CheckTier1Passes;
struct CheckTier1Passes
{
    IrOptimizeResult optimized;
    IrVerifiedAnalysis analysis;
    uint32_t pre_blocks;
    uint32_t pre_moved;
    // --tier1-passes=+licm's moved values (#139).
    uint32_t licm_moved;
    bool unexpanded;
};

// Tier 1's passes from SSA promotion to the optimizer's verified result, which selection reads: promotion in `promotion`,
// the 128-bit integers' halves, the wide vectors' pieces, truth simplification, threading and if-conversion in steps[6],
// --tier1-schedule=pre's order, and the optimizer, whose function goes to `output` and its analysis to `analyses` under
// `revision`. A function's own pipeline runs them (check_tier1_compile) and so does a candidate's summary, once for the
// unit (c_check_tier1_inline.c's check_tier1_inline_build), whose callers copy its result and whose own code goes on
// from it. The outcome is the first failing pass's, as check_tier1_step classifies it.
static CheckTier1Outcome check_tier1_passes(CheckTier1Lane* lane, const CheckTier1PassesInput* input, const X86Target* target, bool scalar, bool certify, const CheckTier1Fault* fault,
                                            Arena* promotion, Arena* output, Arena* analyses, IrVerifiedRevision* revision, CheckTier1Passes* passes)
{
    *passes = (CheckTier1Passes){ 0 };
    Arena* scratch = &lane->scratch;
    CheckTier1Outcome outcome = check_tier1_step(CHECK_TIER1_COMPILED, CHECK_TIER1_CAUSE_IR, 0, BUSTER_IR_NONE, 0, NULL);
    CheckTier1Stage stage = outcome.stage;
    IrFunction promoted = { 0 };
    {
        const IrFunction* unssa = input->function;
        const uint64_t mark = promotion->length;
        IrCheck check = input->verified ? ssa_ir_promote_verified_input(unssa, !scalar, promotion, scratch, &promoted) :
                        scalar          ? ssa_ir_promote_scalar(unssa, promotion, scratch, &promoted) :
                                          ssa_ir_promote(unssa, promotion, scratch, &promoted);
        // An expansion promotion refuses or finds past its limits keeps the function's calls: the lowering is promoted
        // instead, so that inlining is never a refusal. Any other error is the expansion's.
        passes->unexpanded = input->unexpanded != NULL && (check.error == IR_ERROR_UNSUPPORTED || check.error == IR_ERROR_LIMIT);
        if (passes->unexpanded)
        {
            arena_pop_to(promotion, mark);
            unssa = input->unexpanded;
            check = ssa_ir_promote_verified_input(unssa, !scalar, promotion, scratch, &promoted);
        }
        outcome = check_tier1_step(CHECK_TIER1_PROMOTION, CHECK_TIER1_CAUSE_IR, check.error, check.instruction, check.detail, fault);
        outcome.dominance_checked = check.dominance_checked;
        outcome.operation = check.instruction < unssa->instruction_count ? unssa->op[check.instruction] : IR_OP_COUNT;
        stage = outcome.stage;
    }
    // The 128-bit integers' halves (#141, ir_wide.c), which the optimizer then takes: a function without any is the
    // promoted one itself, borrowed.
    IrWideResult wide = { 0 };
    if (stage == CHECK_TIER1_COMPILED)
    {
        const IrCheck check = scalar ? ir_wide_split_scalar(&promoted, &lane->steps[6], scratch, &wide) : ir_wide_split(&promoted, &lane->steps[6], scratch, &wide);
        outcome = check_tier1_step(CHECK_TIER1_WIDE, CHECK_TIER1_CAUSE_IR, check.error, check.instruction, check.detail, fault);
        outcome.operation = check.instruction < promoted.instruction_count ? promoted.op[check.instruction] : IR_OP_COUNT;
        stage = outcome.stage;
    }
    // The vectors wider than the target moves whole, as its widest vectors' pieces (#1624, ir_vector_split.c), in the same
    // step's arena, after the halves: a function without any is the halves' (or the promoted one) itself, borrowed.
    if (stage == CHECK_TIER1_COMPILED)
    {
        IrVectorSplitResult pieces = { 0 };
        const uint32_t piece = ir_typed_vector_piece(target->features);
        const IrCheck check = scalar ? ir_vector_split_scalar(&wide.function, piece, &lane->steps[6], scratch, &pieces) : ir_vector_split(&wide.function, piece, &lane->steps[6], scratch, &pieces);
        outcome = check_tier1_step(CHECK_TIER1_WIDE, CHECK_TIER1_CAUSE_IR, check.error, check.instruction, check.detail, fault);
        outcome.operation = check.instruction < wide.function.instruction_count ? wide.function.op[check.instruction] : IR_OP_COUNT;
        stage = outcome.stage;
        wide.function = stage == CHECK_TIER1_COMPILED ? pieces.function : wide.function;
    }
    if (stage == CHECK_TIER1_COMPILED)
    {
        // The lane owns these columns, including the borrowed promoted columns when no wide split was needed.
        // No later consumer reads their old operands and no analysis revision has been published. Simplify before
        // optimizer preparation builds uses, then let its DCE and final SSA verification consume the new operands.
        ir_truth_simplify(&wide.function, scalar);
    }
    if (stage == CHECK_TIER1_COMPILED)
    {
        // #2554's truth values' blocks threaded into their predecessors (ir_thread.c), in the same step's arena, before
        // if-conversion would make some of their phis selects; then the copies' tests of extended comparisons simplified
        // as the originals were, so that a copied `icmp ne (zext c), 0` is c, the branch's comparison. The result
        // borrows the input's tables, which stay live until the optimizer copies them.
        IrThreadResult threaded = { 0 };
        const IrCheck check = ir_thread(&wide.function, &lane->steps[6], scratch, scalar, &threaded);
        outcome = check_tier1_step(CHECK_TIER1_OPTIMIZATION, CHECK_TIER1_CAUSE_IR, check.error, check.instruction, check.detail, fault);
        outcome.operation = check.instruction < wide.function.instruction_count ? wide.function.op[check.instruction] : IR_OP_COUNT;
        stage = outcome.stage;
        if (stage == CHECK_TIER1_COMPILED && threaded.rounds != 0)
        {
            wide.function = threaded.function;
            ir_truth_simplify(&wide.function, scalar);
        }
    }
    if (stage == CHECK_TIER1_COMPILED)
    {
        // #1382's bounded, total scalar arms become selects before any retained analysis is built. The result
        // borrows immutable tables from the promoted/wide input; all stay live until the optimizer copies them.
        IrIfConvertResult converted = { 0 };
        const IrCheck check = ir_if_convert(&wide.function, &lane->steps[6], scratch, scalar, &converted);
        outcome = check_tier1_step(CHECK_TIER1_OPTIMIZATION, CHECK_TIER1_CAUSE_IR, check.error, check.instruction, check.detail, fault);
        outcome.operation = check.instruction < wide.function.instruction_count ? wide.function.op[check.instruction] : IR_OP_COUNT;
        stage = outcome.stage;
        wide.function = stage == CHECK_TIER1_COMPILED ? converted.function : wide.function;
    }
    // --tier1-passes=+licm (#139's measurement): loop-invariant motion, in steps[9], before the optimizer, whose dead-code
    // removal, CFG cleanup and final verification then take its result. A function it refuses (IR_ERROR_UNSUPPORTED:
    // vectors, and the constructs ir_optimize_licm.c names) goes on as it was; any other error is the function's.
    const uint32_t on = check_tier1_passes_on(lane);
    if (stage == CHECK_TIER1_COMPILED && (on & BUSTER_CHECK_TIER1_PASS_LICM) != 0)
    {
        IrLicmOptions options = ir_licm_options();
        options.scalar = scalar;
        IrLicmResult moved = { 0 };
        const IrCheck check = ir_optimize_licm(&wide.function, &lane->steps[9], scratch, options, &moved);
        const IrError error = check.error == IR_ERROR_UNSUPPORTED ? IR_OK : check.error;
        outcome = check_tier1_step(CHECK_TIER1_OPTIMIZATION, CHECK_TIER1_CAUSE_IR, error, check.instruction, check.detail, NULL);
        outcome.operation = check.instruction < wide.function.instruction_count ? wide.function.op[check.instruction] : IR_OP_COUNT;
        stage = outcome.stage;
        wide.function = stage == CHECK_TIER1_COMPILED && check.error == IR_OK ? moved.function : wide.function;
        passes->licm_moved = check.error == IR_OK ? moved.moved : 0;
    }
    // --tier1-schedule=pre (#172): each block's instructions reordered on the same owned columns, before the optimizer's
    // final verification, which then covers the new order. No room for the pass's scratch is a resource failure.
    if (stage == CHECK_TIER1_COMPILED && lane->schedule == BUSTER_CHECK_TIER1_SCHEDULE_PRE)
    {
        // With --verify, the original columns and the permutation, which the scalar reference checks.
        const uint64_t mark = scratch->length;
        Tier1ScheduleIrOriginal original = { 0 };
        uint32_t* order = certify ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)wide.function.instruction_count + 1) : 0;
        uint32_t* place = certify ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)wide.function.instruction_count + 1) : 0;
        const bool copied = !certify || (order != 0 && place != 0 && tier1_schedule_ir_copy(&wide.function, scratch, &original));
        Tier1ScheduleIrStats schedule_stats = { 0 };
        IrError error = copied ? tier1_schedule_ir_function(&wide.function, scratch, order, &schedule_stats) : IR_ERROR_MEMORY;
        error = error == IR_OK && certify && !tier1_schedule_ir_verify(&original, &wide.function, order, place) ? IR_ERROR_STRUCTURE : error;
        arena_pop_to(scratch, mark);
        outcome = check_tier1_step(CHECK_TIER1_OPTIMIZATION, CHECK_TIER1_CAUSE_IR, error, BUSTER_IR_NONE, 0, NULL);
        stage = outcome.stage;
        passes->pre_blocks = schedule_stats.segments;
        passes->pre_moved = schedule_stats.moved;
    }
    if (stage == CHECK_TIER1_COMPILED)
    {
        // SCCP, dead-code removal and CFG cleanup as --tier1-passes= leaves them; the rewrite and its verification run
        // whatever they are.
        IrOptimizeOptions options = ir_optimize_options();
        options.passes = (on & (BUSTER_IR_OPTIMIZE_SCCP | BUSTER_IR_OPTIMIZE_DCE | BUSTER_IR_OPTIMIZE_CFG)) | BUSTER_IR_OPTIMIZE_LAYOUT | BUSTER_IR_OPTIMIZE_IDENTITY;
        options.scalar = scalar;
        const IrCheck check = ir_optimize_verified(&wide.function, output, analyses, scratch, options, revision, &passes->optimized, &passes->analysis);
        outcome = check_tier1_step(CHECK_TIER1_OPTIMIZATION, CHECK_TIER1_CAUSE_IR, check.error, check.instruction, check.detail, fault);
        outcome.dominance_checked = check.dominance_checked;
        outcome.operation = check.instruction < wide.function.instruction_count ? wide.function.op[check.instruction] : IR_OP_COUNT;
        stage = outcome.stage;
    }
    return outcome;
}

#include "c_check_tier1_inline.c"

static CheckTier1Outcome check_tier1_compile(CheckTier1Lane* lane, const Tier0Function* function, uint8_t result_class, const X86Target* target, bool scalar,
                                             bool rematerialize, bool gvn, bool certify, bool sources, Arena* output, EmittedCode* code,
                                             CheckTier1Counts* counts, const CheckTier1Fault* fault)
{
    *code = (EmittedCode){ 0 };
    *counts = (CheckTier1Counts){ 0 };
    // --tier1-passes='s SROA on every body, and LICM, each with an arena of its own.
    const uint32_t on = check_tier1_passes_on(lane);
    const bool sroa = (on & BUSTER_CHECK_TIER1_PASS_SROA) != 0;
    const uint8_t optional = (uint8_t)((gvn ? 1u : 0) | (sroa ? 2u : 0) | ((on & BUSTER_CHECK_TIER1_PASS_LICM) != 0 ? 4u : 0));
    CheckTier1Outcome outcome = check_tier1_step(CHECK_TIER1_RESOURCE, CHECK_TIER1_CAUSE_ARENA,
                                                check_tier1_lane_prepare(lane, target, optional) ? 0 : BUSTER_CHECK_TIER1_ARENA_RESERVE, BUSTER_IR_NONE, 0, fault);
    CheckTier1Stage stage = outcome.stage;
    Arena* scratch = &lane->scratch;
    const IrType result_type = result_class == UINT8_MAX ? IR_VOID : (IrType)ir_typed_types[result_class];
    IrFunction lowered = { 0 };
    // A caller of a function that returns twice (#1847, Tier0Function.calls_returns_twice) is Tier 0's: Tier 1 shares
    // call-save rows and spill slots among values whose ranges don't meet on the CFG, which has no edge from a longjmp
    // back to the call, so a second return would read another value's bytes. Its IR lowers as any other's.
    if (stage == CHECK_TIER1_COMPILED && function->calls_returns_twice)
    {
        outcome = check_tier1_failure(CHECK_TIER1_LOWERING, CHECK_TIER1_CAUSE_ADMISSION, BUSTER_CHECK_TIER1_RETURNS_TWICE, UINT32_MAX, 0);
        stage = outcome.stage;
    }
    // A candidate whose summary is ready (#2541, c_check_tier1_inline.c's check_tier1_inline_own) goes on from it to
    // selection: the summary was its lowering, its calls' expansion and Tier 1's passes (check_tier1_passes), done once
    // for the unit. Not with source rows, which the summary has none of, the solver or --tier1-passes=+sroa, which run
    // before promotion, or a fault, which may be any pass's.
    const CheckTier1Inline* inlining = lane->inlining;
    const uint64_t body = inlining != NULL ? (uint64_t)(function - inlining->lowered->functions) : UINT64_MAX;
    const bool member = inlining != NULL && function >= inlining->lowered->functions && body < inlining->program->body_count;
    CheckTier1Passes passes = { 0 };
    uint32_t inlined_calls = 0;
    const bool own = stage == CHECK_TIER1_COMPILED && member && !function->calls_returns_twice && !sources && !gvn && !sroa && fault == NULL &&
                     check_tier1_inline_own(lane, (uint32_t)body, scalar, certify, &passes, &inlined_calls);
    if (stage == CHECK_TIER1_COMPILED && !own)
    {
        // A vector wider than the target moves whole is taken, and split below (#1624).
        const IrCheck check = sources ? ir_lower_typed_target_pieces_sources(function, result_type, function->symbol_count, target->features, &lane->steps[0], scratch, &lowered, scalar) :
                                       ir_lower_typed_target_pieces(function, result_type, function->symbol_count, target->features, &lane->steps[0], scratch, &lowered, scalar);
        outcome = check_tier1_step(CHECK_TIER1_LOWERING, CHECK_TIER1_CAUSE_IR, check.error, check.instruction, check.detail, fault);
        outcome.dominance_checked = check.dominance_checked;
        stage = outcome.stage;
    }
    // The calls of the unit's candidates expanded from their offers, and the aggregates' slots split (#1282, #2541,
    // c_check_tier1_inline.c), in steps[4] and steps[5]: what the solver and promotion take in place of the lowering.
    IrFunction expanded = { 0 };
    // Whether `lowered` is verified: the lowering is as it is made, and an expansion SROA took is by SROA; another
    // expansion isn't until promotion.
    bool lowered_verified = true;
    const IrFunction original = lowered;
    bool expanded_verified = true;
    if (stage == CHECK_TIER1_COMPILED && !own && member && check_tier1_inline(lane, (uint32_t)body, &lowered, target, scalar, certify, &expanded, &inlined_calls, &expanded_verified))
    {
        lowered = expanded;
        lowered_verified = expanded_verified;
    }
    // --tier1-passes=+sroa (#139's measurement): SROA on every body, the expansion where there is one, in steps[8], with
    // the inlining's options (#1282's simd and flagged), before the solver and promotion. A body it refuses
    // (IR_ERROR_UNSUPPORTED) goes on as it was; any other error is the body's, as the solver's are.
    uint32_t sroa_split = 0;
    if (stage == CHECK_TIER1_COMPILED && sroa)
    {
        IrSroaOptions options = ir_sroa_options();
        options.scalar = scalar;
        options.simd = true;
        options.flagged = true;
        IrSroaResult split = { 0 };
        const IrCheck check = ir_optimize_sroa(&lowered, &lane->steps[8], scratch, options, &split);
        const IrError error = check.error == IR_ERROR_UNSUPPORTED ? IR_OK : check.error;
        outcome = check_tier1_step(CHECK_TIER1_LOWERING, CHECK_TIER1_CAUSE_IR, error, check.instruction, check.detail, NULL);
        outcome.operation = check.instruction < lowered.instruction_count ? lowered.op[check.instruction] : IR_OP_COUNT;
        stage = outcome.stage;
        if (check.error == IR_OK)
        {
            // SROA verifies its input as it prepares and its output as it ends.
            lowered = split.function;
            lowered_verified = true;
            sroa_split = split.split_slots;
        }
    }
    // #866's congruence solver, before promotion, so that promotion's phi placement decides what its merged values
    // need: on promoted SSA its class representatives lengthen live ranges across blocks, which Tier 1's allocation then
    // pays for in moves and spills. Its rewritten function is verified. Like the other stages, only an explicit
    // capability refusal permits Tier 0 fallback; resource or invariant failures retain their original IrCheck.
    IrGvnResult gvn_result = { 0 };
    bool gvn_applied = false;
    if (stage == CHECK_TIER1_COMPILED && gvn)
    {
        IrGvnOptions options = ir_gvn_options();
        options.scalar = scalar;
        const IrCheck check = ir_optimize_gvn(&lowered, &lane->steps[7], scratch, options, &gvn_result);
        outcome = check_tier1_step(CHECK_TIER1_VALUE_NUMBERING, CHECK_TIER1_CAUSE_IR, check.error, check.instruction, check.detail, fault);
        outcome.dominance_checked = check.dominance_checked;
        outcome.operation = check.instruction < lowered.instruction_count ? lowered.op[check.instruction] : IR_OP_COUNT;
        stage = outcome.stage;
        gvn_applied = stage == CHECK_TIER1_COMPILED;
    }
    const IrFunction* unssa = gvn_applied ? &gvn_result.function : &lowered;
    // The passes from promotion to the optimizer (check_tier1_passes), or a candidate's own summary's, which ran them.
    IrVerifiedRevision revision = { 0 };
    if (stage == CHECK_TIER1_COMPILED && !own)
    {
        // GVN's result and the lowering's are each verified whole as they're made, so promotion doesn't verify them again
        // (#1379); an inlined expansion it does, but where SROA took and verified it.
        const CheckTier1PassesInput input = {
            .function = unssa,
            .verified = gvn_applied || lowered_verified,
            .unexpanded = !gvn_applied && inlined_calls != 0 ? &original : NULL,
        };
        outcome = check_tier1_passes(lane, &input, target, scalar, certify, fault, &lane->steps[1], &lane->steps[2], &lane->steps[3], &revision, &passes);
        stage = outcome.stage;
        inlined_calls = passes.unexpanded ? 0 : inlined_calls;
    }
    else if (stage == CHECK_TIER1_COMPILED)
    {
        // The summary's function and analysis, which nothing writes: this revision is theirs.
        revision.function = &passes.optimized.function;
        revision.number = 1;
        passes.analysis.owner = &revision;
        passes.analysis.revision = revision.number;
    }
    const IrVerifiedAnalysis analysis = passes.analysis;
    const uint32_t pre_blocks = passes.pre_blocks;
    const uint32_t pre_moved = passes.pre_moved;
    // #866's congruence solver's result, if it ran, is `unssa`; `ir` is what the plan below reads.
    const IrFunction* ir = &passes.optimized.function;
    if (stage == CHECK_TIER1_COMPILED)
    {
        const bool current = ir_verified_current(&analysis, &revision, ir);
        outcome = check_tier1_step(CHECK_TIER1_ANALYSIS, CHECK_TIER1_CAUSE_IR, current ? 0 : IR_ERROR_METADATA, BUSTER_IR_NONE, 0,
                                   fault != NULL && fault->cause == CHECK_TIER1_CAUSE_IR ? fault : NULL);
        stage = outcome.stage;
    }
    if (stage == CHECK_TIER1_COMPILED)
    {
        // The optimizer's rewrite owns all supported columns, including labels, calls and ABI side tables. Lowered,
        // GVN's rewritten function (steps[7], when enabled), promoted IR and its 128-bit integers' halves (steps[6]) may
        // borrow from each other, but none is a dependency of the optimized function. Reuse lowered and promoted arenas
        // for the plan and emitted records; retain optimized IR and analyses through emission.
        arena_pop_to(&lane->steps[0], 0);
        arena_pop_to(&lane->steps[1], 0);
    }
    Tier1SelectPlan plan = { 0 };
    if (stage == CHECK_TIER1_COMPILED)
    {
        const IrDominance dominance = {
            .enter = analysis.dominators.preorder,
            .leave = analysis.dominators.subtree_end,
            .block_count = analysis.dominators.block_count,
        };
        uint32_t node = BUSTER_IR_NONE;
        // Full final SSA verification produced this exact revision and these intervals. No intervening pass mutates
        // it; the revision and both owners remain live until selection and emission have finished (#1055).
        const Tier1SelectError selected = tier1_select_plan_verified(ir, &dominance, function->symbol_access, function->symbol_count, &lane->steps[0], scratch, !scalar, &plan, &node);
        const uint32_t refused = node < ir->instruction_count ? ir->op[node] : IR_OP_COUNT;
        outcome = check_tier1_step(CHECK_TIER1_SELECTION, CHECK_TIER1_CAUSE_SELECTION, selected, node, 0, fault);
        outcome.operation = refused;
        outcome.call_kinds = selected == TIER1_SELECT_ERROR_UNSUPPORTED ? check_tier1_call_kinds(ir, node) : 0;
        stage = outcome.stage;
    }
    Tier1EmitResult emitted = { 0 };
    if (stage == CHECK_TIER1_COMPILED)
    {
        Tier1EmitDiagnostic diagnostic = { 0 };
        // The frame pointer the options ask for (#177, Tier0Function.frame_pointer), as Tier 0's frame keeps it, and
        // stack-clash protection's probes (Tier0Function.probe_stack): the prologue's and the dynamic allocations'.
        const Tier1EmitOptions options = {
            .diagnostic = &diagnostic,
            .register_mask = tier1_liveness_register_mask(),
            .frame_flags = (function->frame_pointer != BUSTER_TIER0_FRAME_POINTER_NONE ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0) |
                           (function->probe_stack ? BUSTER_TIER1_FRAME_PROBE_STACK : 0),
            .omit_leaf_frame_pointer = function->frame_pointer == BUSTER_TIER0_FRAME_POINTER_NON_LEAF,
            .vector = !scalar,
            .rematerialize = rematerialize,
            .symbol_access = function->symbol_access,
            .symbol_count = function->symbol_count,
            .certify = certify,
            .sources = sources,
        };
        uint32_t node = BUSTER_IR_NONE;
        Tier1EmitError error = tier1_emit_function(&plan, &options, lane->allowed, &lane->steps[1], scratch, &emitted, &node);
        bool forced_nested = false;
        if (error == TIER1_EMIT_OK && fault != NULL && fault->stage == CHECK_TIER1_EMISSION && fault->cause != CHECK_TIER1_CAUSE_EMISSION)
        {
            // The regression control's nested cause: replace the successful handoff with the raw diagnostic and its
            // wrapper, as a real nested liveness/IR/allocation/CFG/frame failure would leave them.
            diagnostic.cause = check_tier1_fault_emission_cause(fault->cause);
            diagnostic.error = fault->error;
            error = TIER1_EMIT_MEMORY;
            forced_nested = true;
        }
        const bool nested = error != TIER1_EMIT_OK && diagnostic.cause != TIER1_EMIT_CAUSE_NONE;
        outcome = check_tier1_step(CHECK_TIER1_EMISSION, nested ? check_tier1_emission_causes[diagnostic.cause] : CHECK_TIER1_CAUSE_EMISSION,
                                   nested ? diagnostic.error : (uint32_t)error, node, 0, fault);
        if (forced_nested)
        {
            outcome.instruction = fault->instruction;
            outcome.detail = fault->detail;
        }
        outcome.emission_error = error != TIER1_EMIT_OK ? (uint32_t)error : outcome.cause == CHECK_TIER1_CAUSE_EMISSION ? outcome.error : 0;
        outcome.operation = node < ir->instruction_count ? ir->op[node] : IR_OP_COUNT;
        stage = outcome.stage;
    }
    // The code in `output`, as Tier 0's is: its records, branch columns and references.
    const uint64_t mark = output->length;
    const Tier1SelectCode* selected = &emitted.code;
    X86Instruction* records = stage == CHECK_TIER1_COMPILED ? BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, (uint64_t)selected->count + 1) : 0;
    uint32_t* branches = stage == CHECK_TIER1_COMPILED ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)selected->branch_count + 1) : 0;
    uint32_t* targets = stage == CHECK_TIER1_COMPILED ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)selected->branch_count + 1) : 0;
    LinkReference* references = stage == CHECK_TIER1_COMPILED ? BUSTER_ARENA_PUSH_ARRAY(output, LinkReference, (uint64_t)emitted.reference_count + 1) : 0;
    // Each label's first record, where the emission defines its symbol (#789).
    uint32_t* labels = stage == CHECK_TIER1_COMPILED ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)emitted.label_count + 1) : 0;
    // Its frame's events (#1056); with -g, source/storage ranges are published only after final record scheduling.
    EmissionFrameEvent* frames = stage == CHECK_TIER1_COMPILED ? BUSTER_ARENA_PUSH_ARRAY(output, EmissionFrameEvent, (uint64_t)emitted.frame_event_count + 1) : 0;
    if (stage == CHECK_TIER1_COMPILED && (records == 0 || branches == 0 || targets == 0 || references == 0 || labels == 0 || frames == 0))
    {
        arena_pop_to(output, mark);
        outcome = check_tier1_failure(CHECK_TIER1_PUBLICATION, CHECK_TIER1_CAUSE_ARENA, BUSTER_CHECK_TIER1_ARENA_OUTPUT, BUSTER_IR_NONE, 0);
        stage = outcome.stage;
    }
    if (stage == CHECK_TIER1_COMPILED)
    {
        // An empty column's producer may hold no pointer at all: copying nothing from null is still undefined behavior.
        if (selected->count != 0)
        {
            memcpy(records, selected->records, (uint64_t)selected->count * sizeof(X86Instruction));
        }
        if (selected->branch_count != 0)
        {
            memcpy(branches, selected->branches, (uint64_t)selected->branch_count * sizeof(uint32_t));
            memcpy(targets, selected->targets, (uint64_t)selected->branch_count * sizeof(uint32_t));
        }
        if (emitted.reference_count != 0)
        {
            memcpy(references, emitted.references, (uint64_t)emitted.reference_count * sizeof(LinkReference));
        }
        if (emitted.label_count != 0)
        {
            memcpy(labels, emitted.label_records, (uint64_t)emitted.label_count * sizeof(uint32_t));
        }
        *counts = (CheckTier1Counts){
            .records = selected->count,
            .spill_stores = emitted.spill_stores,
            .spill_loads = emitted.spill_loads,
            .call_stores = emitted.call_stores,
            .call_loads = emitted.call_loads,
            .rematerializations = emitted.rematerializations,
            .dropped_definitions = emitted.dropped_definitions,
            .edge_moves = emitted.edge_moves,
            .spill_slots = emitted.allocation.spill_count,
            .frame_bytes = emitted.frame.maximum_stack_bytes,
            .red_zones = emitted.frame.red_zone && emitted.frame.red_zone_bytes != 0,
            .code_certified = emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK,
            .code_uncertified = emitted.code_checked && emitted.code_reason != TIER1_CERT_EMITTED_OK,
            .machine_rows = emitted.machine_rows,
            .row_moves = emitted.row_moves,
            .inlined_calls = inlined_calls,
            .schedule_blocks = pre_blocks,
            .schedule_moved = pre_moved,
            .folded = passes.optimized.folded,
            .removed_instructions = passes.optimized.removed_instructions,
            .removed_blocks = passes.optimized.removed_blocks,
            .licm_moved = passes.licm_moved,
            .sroa_split = sroa_split,
            .gvn_removed = gvn_applied ? gvn_result.removed : 0,
        };
        if (emitted.frame_event_count != 0)
        {
            memcpy(frames, emitted.frame_events, (uint64_t)emitted.frame_event_count * sizeof(EmissionFrameEvent));
        }
        uint32_t epilogue = selected->count;
        const bool published = check_tier1_frame(&emitted, selected->count, &epilogue);
        // --tier1-schedule=post (#172): each block's records reordered, within the cuts the code's columns and events
        // make; the body's start and the epilogue's are cuts with or without -g, so that -g doesn't change the code.
        CheckTier1ScheduleResult scheduled = { .verified = true, .ready = true };
        uint32_t* record_order = NULL;
        if (lane->schedule == BUSTER_CHECK_TIER1_SCHEDULE_POST)
        {
            record_order = sources ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)selected->count + 1) : NULL;
            const uint32_t anchors[2] = { emitted.body_record, epilogue };
            const CheckTier1ScheduleCode schedule = {
                .records = records,
                .count = selected->count,
                .branches = branches,
                .targets = targets,
                .branch_count = selected->branch_count,
                .references = references,
                .reference_count = emitted.reference_count,
                .labels = labels,
                .label_count = emitted.label_count,
                .frames = frames,
                .frame_count = emitted.frame_event_count,
                .anchors = anchors,
                .anchor_count = 2,
                .record_order = record_order,
            };
            scheduled = sources && record_order == NULL ? (CheckTier1ScheduleResult){ 0 } : check_tier1_schedule_code(scratch, &schedule, !scalar, certify || sources);
            counts->schedule_blocks = scheduled.blocks;
            counts->schedule_moved = scheduled.moved;
        }
        CheckTier1Debug debug = { 0 };
        const bool debug_ready = !sources || !published || !scheduled.ready || !scheduled.verified ||
                                 check_tier1_debug(function, ir, &emitted, record_order, epilogue, !scalar, output, scratch, &debug);
        *code = (EmittedCode){
            .records = records,
            .count = selected->count,
            .branches = branches,
            .targets = targets,
            .branch_count = selected->branch_count,
            .references = references,
            .reference_count = emitted.reference_count,
            .label_records = labels,
            .label_count = emitted.label_count,
            .local_count = sources ? function->local_count : 0,
            .local_ranges = debug.local_ranges,
            .local_range_count = debug.local_range_count,
            .source_records = debug.source_records,
            .source_origins = debug.source_origins,
            .source_count = debug.source_count,
            .frames = emitted.frame_event_count != 0 ? frames : 0,
            .frame_count = emitted.frame_event_count,
            .frame_status = EMITTED_FRAME_DESCRIBED,
            // An asm body that moves a value into rsp (#1846) leaves the frame to the program, which the verifier doesn't read.
            .assembly = tier0_asm_stack(function),
        };
        // Its unwind table's shape (#177): open where its code doesn't end in its last return's ret, and the returns
        // whose epilogues' rules are brackets of their own (all of an open frame's, all but the last of a closed one's).
        const bool closed = selected->count != 0 && emitted.exit_count != 0 && emitted.exits[emitted.exit_count - 1] == selected->count - 1;
        const bool framed = emitted.frame_event_count != 0;
        counts->open_frames = framed && !closed;
        counts->return_tails = !framed ? 0 : closed ? emitted.exit_count - 1 : emitted.exit_count;
        outcome = !debug_ready ? check_tier1_failure(CHECK_TIER1_PUBLICATION, CHECK_TIER1_CAUSE_ARENA, BUSTER_CHECK_TIER1_ARENA_OUTPUT, BUSTER_IR_NONE, 0) :
                  !scheduled.ready ? check_tier1_failure(CHECK_TIER1_PUBLICATION, CHECK_TIER1_CAUSE_ARENA, BUSTER_CHECK_TIER1_ARENA_SCHEDULE, BUSTER_IR_NONE, 0) :
                                     check_tier1_step(CHECK_TIER1_PUBLICATION, CHECK_TIER1_CAUSE_PUBLICATION,
                                                      !published            ? BUSTER_CHECK_TIER1_FRAME_PUBLICATION :
                                                      !scheduled.verified ? BUSTER_CHECK_TIER1_SCHEDULE : 0,
                                                      BUSTER_IR_NONE, 0, fault);
        if (outcome.category != CHECK_TIER1_SUCCESS)
        {
            arena_pop_to(output, mark);
            *code = (EmittedCode){ 0 };
            *counts = (CheckTier1Counts){ 0 };
            stage = outcome.stage;
        }
    }
    const bool success = outcome.category == CHECK_TIER1_SUCCESS;
    if (!success)
    {
        arena_pop_to(output, mark);
        *code = (EmittedCode){ 0 };
        *counts = (CheckTier1Counts){ 0 };
    }
    // Only the copies in output may escape this invocation, on success as on a refusal. Invalidate before retiring
    // the optimized columns or graph, so no live capability authorizes recycled storage.
    ir_verified_invalidate(&revision);
    for (uint32_t k = 0; k < BUSTER_CHECK_TIER1_STEPS; k += 1)
    {
        arena_pop_to(&lane->steps[k], 0);
    }
    if (!success)
    {
        arena_pop_to(&lane->scratch, 0);
        if (outcome.stage == CHECK_TIER1_RESOURCE)
        {
            check_tier1_lane_release(lane);
        }
    }
    return outcome;
}

static void check_tier1_print_refusal(FILE* stream, const uint8_t* name, uint32_t length, CheckTier1Outcome outcome)
{
    const char* operation = outcome.stage == CHECK_TIER1_SELECTION && outcome.operation < IR_OP_COUNT ? ir_op_names[outcome.operation] :
                            outcome.stage == CHECK_TIER1_SELECTION && outcome.operation == IR_OP_COUNT ? "function" : "none";
    // A refused call's kinds, comma-separated, past the fields every refusal has (#1116).
    char kinds[256] = { 0 };
    uint32_t used = 0;
    for (uint32_t k = 0; k < CHECK_TIER1_CALL_KIND_COUNT; k += 1)
    {
        const int written = ((outcome.call_kinds >> k) & 1) != 0 ? snprintf(kinds + used, sizeof(kinds) - used, "%s%s", used == 0 ? "\tcall=" : ",", check_tier1_call_kind_names[k]) : 0;
        used += written > 0 && used + (uint32_t)written < sizeof(kinds) ? (uint32_t)written : 0;
    }
    fprintf(stream, "tier1-refused\t%.*s\t%s\tcategory=%s\tcause=%s\terror=%" PRIu32 "\tinstruction=%" PRIu32 "\tdetail=%" PRIu32
                    "\toperation=%s\temission_error=%" PRIu32 "\tdominance_checked=%" PRIu32 "\tfallback=%s%s\n",
            (int)length, (const char*)name, check_tier1_stage_names[outcome.stage], check_tier1_category_names[outcome.category],
            check_tier1_cause_names[outcome.cause], outcome.error, outcome.instruction, outcome.detail, operation, outcome.emission_error,
            (uint32_t)outcome.dominance_checked, outcome.fallback == CHECK_TIER1_FALLBACK_TIER0 ? "tier0" : "forbidden", kinds);
}

static void check_tier1_print_report(FILE* stream, const CheckTier1Report* report)
{
    const char* const* stages = check_tier1_stage_names;
    fprintf(stream, "tier1\tfunctions=%" PRIu32, report->functions);
    for (uint32_t s = 0; s < CHECK_TIER1_STAGE_COUNT; s += 1)
    {
        fprintf(stream, "\t%s=%" PRIu32, stages[s], report->stages[s]);
    }
    fprintf(stream, "\trequested=%" PRIu32 "\tadmitted=%" PRIu32 "\tcompiled=%" PRIu32 "\tunsupported=%" PRIu32 "\tresource_failures=%" PRIu32
                    "\tresource_retries=0\tinternal_failures=%" PRIu32 "\tfallbacks=%" PRIu32,
            report->requested, report->admitted, report->categories[CHECK_TIER1_SUCCESS], report->categories[CHECK_TIER1_UNSUPPORTED],
            report->categories[CHECK_TIER1_RESOURCE_FAILURE], report->categories[CHECK_TIER1_INTERNAL], report->fallbacks);
    for (uint32_t k = 0; k < BUSTER_CHECK_TIER1_OPERATIONS; k += 1)
    {
        if (report->operations[k] != 0 && k < IR_OP_COUNT)
        {
            fprintf(stream, "\tselection.%s=%" PRIu32, ir_op_names[k], report->operations[k]);
        }
        else if (report->operations[k] != 0 && k == IR_OP_COUNT)
        {
            fprintf(stream, "\tselection.function=%" PRIu32, report->operations[k]);
        }
        else if (report->operations[k] != 0)
        {
            fprintf(stream, "\tselection.error%" PRIu32 "=%" PRIu32, k - IR_OP_COUNT, report->operations[k]);
        }
    }
    for (uint32_t k = 0; k < CHECK_TIER1_CALL_KIND_COUNT; k += 1)
    {
        if (report->calls[k] != 0)
        {
            fprintf(stream, "\tselection.call.%s=%" PRIu32, check_tier1_call_kind_names[k], report->calls[k]);
        }
    }
    for (uint32_t k = 0; k < BUSTER_CHECK_TIER1_OPERATIONS; k += 1)
    {
        if (report->emission[k] != 0)
        {
            fprintf(stream, "\temission.error%" PRIu32 "=%" PRIu32, k, report->emission[k]);
        }
    }
    fprintf(stream, "\trecords=%" PRIu64 "\tspill_stores=%" PRIu64 "\tspill_loads=%" PRIu64 "\tcall_stores=%" PRIu64 "\tcall_loads=%" PRIu64 "\trematerializations=%" PRIu64
                    "\tdropped_definitions=%" PRIu64 "\tedge_moves=%" PRIu64 "\tspill_slots=%" PRIu64 "\tframe_bytes=%" PRIu64 "\topen_frames=%" PRIu64
                    "\treturn_tails=%" PRIu64 "\tred_zones=%" PRIu64 "\tcode_certified=%" PRIu64 "\tcode_uncertified=%" PRIu64 "\tmachine_rows=%" PRIu64 "\trow_moves=%" PRIu64 "\tinlined=%" PRIu64
                    "\tschedule_blocks=%" PRIu64 "\tschedule_moved=%" PRIu64 "\tfolded=%" PRIu64 "\tremoved_instructions=%" PRIu64 "\tremoved_blocks=%" PRIu64
                    "\tlicm_moved=%" PRIu64 "\tsroa_split=%" PRIu64 "\tgvn_removed=%" PRIu64 "\n",
            report->records, report->spill_stores, report->spill_loads, report->call_stores, report->call_loads, report->rematerializations,
            report->dropped_definitions, report->edge_moves, report->spill_slots, report->frame_bytes, report->open_frames, report->return_tails, report->red_zones,
            report->code_certified, report->code_uncertified, report->machine_rows, report->row_moves, report->inlined_calls, report->schedule_blocks,
            report->schedule_moved, report->folded, report->removed_instructions, report->removed_blocks, report->licm_moved, report->sroa_split, report->gvn_removed);
}
