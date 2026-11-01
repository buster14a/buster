// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// The C driver's --tier1 (#138's piece for #140): a body's typed stream compiled by Tier 1's pipeline rather than by
// Tier 0: lowered to the shared IR (ir_lower.c), promoted to SSA (ssa_ir.c), optimized (ir_optimize.c's SCCP, dead
// code and CFG cleanup), its CFG and dominators analyzed, selected (tier1_select.c) and emitted (tier1_emit.c, whose
// CFG consumer allocates a general CFG's values over it, tier1_cfg.c), each step with --scalar by its scalar twin. The
// emission's check_program_emit_tier1 calls it for each body and gives Tier 0 the bodies a step refuses, so that a
// program of both tiers' functions is one System V program. Tier 1's code publishes its frame as Tier 0's does
// (check_tier1_frame, #177), so that the emission writes its unwind tables as it writes Tier 0's. What Tier 1's code
// leaves out (-g's rows) the driver refuses with the option. Include after tier1_emit.c and c_check_program_emit.c.

_Static_assert(BUSTER_TIER1_ACCESS_DIRECT == BUSTER_TIER0_ACCESS_DIRECT && BUSTER_TIER1_ACCESS_GOT == BUSTER_TIER0_ACCESS_GOT, "Tier 1 reads Tier 0's access column");

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

// The lane's arenas, reserved at its first body and emptied for each, and the forms `target` allows, built again only
// for a target other than the last one's. Returns false if one can't be reserved.
static bool check_tier1_lane_prepare(CheckTier1Lane* lane, const X86Target* target)
{
    if (!lane->reserved)
    {
        for (uint32_t k = 0; k < BUSTER_CHECK_TIER1_STEPS; k += 1)
        {
            lane->steps[k] = arena_reserve(UINT64_C(1) << 34);
        }
        lane->scratch = arena_reserve(UINT64_C(1) << 36);
        lane->forms = arena_reserve_small(X86_FORM_COUNT, X86_FORM_COUNT);
        lane->allowed = BUSTER_ARENA_PUSH_ARRAY(&lane->forms, uint8_t, X86_FORM_COUNT);
        lane->reserved = true;
    }
    bool valid = lane->scratch.base != 0 && lane->allowed != 0;
    for (uint32_t k = 0; k < BUSTER_CHECK_TIER1_STEPS; k += 1)
    {
        valid = valid && lane->steps[k].base != 0;
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

// Tier 1's frame as Tier 0 publishes its own (Tier0Code.frame_*, which unwind_tier0_frame_tails reads, #177): the
// callee-saved registers it pushes, by their bits in Tier 0's order (unwind_tier0_saved_registers), its subtraction,
// its frame pointer, its realignment and its base. Its early returns' rets are the code's tails, each right after its
// epilogue; and where the code's last record isn't its last return's ret (a loop's latch, a call that doesn't return,
// a trap), the frame is open and every return is a tail. A frameless function publishes no tails, its rules the CIE's
// throughout; it is open where its code doesn't end in a ret, which spares the reader's rule that a closed frameless
// function ends in a ret or a ud2, and its FDE is the closed one either way. A probe's loop
// (BUSTER_TIER1_FRAME_PROBE_STACK, which the driver doesn't ask for) moves rsp a page at a time, which the protocol
// doesn't describe; and every exit must be a ret, the last record exactly when the frame is closed. Returns false for
// a frame it can't publish so, whose function Tier 0 then compiles, rather than fail the emission's reader.
static bool check_tier1_frame(const Tier1EmitResult* emitted, uint32_t count, uint32_t* tails, Tier0Code* code)
{
    const Tier1Frame* frame = &emitted->frame;
    const X86Instruction* records = emitted->code.records;
    bool describable = true;
    for (uint32_t i = 0; i < frame->prologue_count; i += 1)
    {
        describable &= frame->prologue[i].kind != TIER1_FRAME_PROBE_ALLOCATE && frame->prologue[i].kind != TIER1_FRAME_PROBE_ALIGN;
    }
    uint32_t saved = 0;
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(unwind_tier0_saved_registers); r += 1)
    {
        saved |= ((frame->saved_registers >> unwind_tier0_saved_registers[r]) & 1u) << r;
    }
    describable &= (frame->saved_registers & ~BUSTER_ABI_CALLEE_SAVED) == 0;
    const bool frameless = saved == 0 && !frame->frame_pointer && frame->stack_adjust_bytes == 0;
    const bool closed = count != 0 && emitted->exit_count != 0 && emitted->exits[emitted->exit_count - 1] == count - 1;
    for (uint32_t k = 0; k < emitted->exit_count; k += 1)
    {
        describable &= emitted->exits[k] < count && records[emitted->exits[k]].form == X86_FORM_RET;
    }
    describable &= closed || count == 0 || records[count - 1].form != X86_FORM_RET;
    const uint32_t tail_count = frameless ? 0 : closed ? emitted->exit_count - 1 : emitted->exit_count;
    for (uint32_t k = 0; k < tail_count; k += 1)
    {
        tails[k] = emitted->exits[k];
    }
    code->frame_saved = saved;
    code->frame_size = frame->stack_adjust_bytes;
    code->frame_pointer = frame->frame_pointer;
    code->frame_alignment_log2 = frame->realigned ? (uint8_t)__builtin_ctz(frame->alignment) : 0;
    code->frame_base = frame->realigned && frame->base_register == BUSTER_TIER1_FRAME_REALIGN_BASE;
    code->frame_open = !closed;
    code->tail_records = tail_count != 0 ? tails : 0;
    code->tail_count = tail_count;
    return describable;
}

static CheckTier1Stage check_tier1_compile(CheckTier1Lane* lane, const Tier0Function* function, uint8_t result_class, const X86Target* target, bool scalar,
                                           bool rematerialize, Arena* output, Tier0Code* code, uint32_t* operation, CheckTier1Counts* counts)
{
    *operation = 0;
    *counts = (CheckTier1Counts){ 0 };
    CheckTier1Stage stage = check_tier1_lane_prepare(lane, target) ? CHECK_TIER1_COMPILED : CHECK_TIER1_RESOURCE;
    Arena* scratch = &lane->scratch;
    const IrType result_type = result_class == UINT8_MAX ? IR_VOID : (IrType)ir_typed_types[result_class];
    IrFunction lowered = { 0 };
    if (stage == CHECK_TIER1_COMPILED)
    {
        const IrCheck check = scalar ? ir_lower_typed_target_scalar(function, result_type, function->symbol_count, target->features, &lane->steps[0], scratch, &lowered) :
                                       ir_lower_typed_target(function, result_type, function->symbol_count, target->features, &lane->steps[0], scratch, &lowered);
        stage = check.error == IR_OK ? stage : CHECK_TIER1_LOWERING;
    }
    IrFunction promoted = { 0 };
    if (stage == CHECK_TIER1_COMPILED)
    {
        const IrCheck check = scalar ? ssa_ir_promote_scalar(&lowered, &lane->steps[1], scratch, &promoted) : ssa_ir_promote(&lowered, &lane->steps[1], scratch, &promoted);
        stage = check.error == IR_OK ? stage : CHECK_TIER1_PROMOTION;
    }
    IrOptimizeResult optimized = { 0 };
    if (stage == CHECK_TIER1_COMPILED)
    {
        IrOptimizeOptions options = ir_optimize_options();
        options.scalar = scalar;
        stage = ir_optimize(&promoted, &lane->steps[2], scratch, options, &optimized).error == IR_OK ? stage : CHECK_TIER1_OPTIMIZATION;
    }
    const IrFunction* ir = &optimized.function;
    IrCfg cfg = { 0 };
    Dominators dominators = { 0 };
    if (stage == CHECK_TIER1_COMPILED)
    {
        stage = ir_build_cfg(ir, &lane->steps[3], scratch, &cfg) == IR_OK ? stage : CHECK_TIER1_ANALYSIS;
    }
    if (stage == CHECK_TIER1_COMPILED)
    {
        const DominatorCfg graph = {
            .block_count = cfg.block_count,
            .edge_count = cfg.edge_count,
            .entry = 0,
            .successor_offsets = cfg.successor_offsets,
            .successors = cfg.successors,
            .predecessor_offsets = cfg.predecessor_offsets,
            .predecessors = cfg.predecessors,
        };
        DominatorOptions options = dominators_options();
        options.outputs = 0;
        options.path = scalar ? DOMINATOR_SCALAR : DOMINATOR_AUTO;
        stage = dominators_analyze(&lane->steps[3], scratch, &graph, options, &dominators) == DOMINATOR_OK ? stage : CHECK_TIER1_ANALYSIS;
    }
    Tier1SelectPlan plan = { 0 };
    if (stage == CHECK_TIER1_COMPILED)
    {
        const IrDominance dominance = {
            .enter = dominators.preorder,
            .leave = dominators.subtree_end,
            .block_count = dominators.block_count,
        };
        uint32_t node = BUSTER_IR_NONE;
        // ir_optimize verified its result, its uses dominated by their definitions, before it returned: the plan doesn't
        // verify it again (#141).
        const Tier1SelectError selected = tier1_select_plan_verified(ir, &dominance, &lane->steps[4], scratch, !scalar, &plan, &node);
        const uint32_t refused = node < ir->instruction_count ? ir->op[node] : IR_OP_COUNT;
        *operation = selected == TIER1_SELECT_OK ? 0 : selected == TIER1_SELECT_ERROR_UNSUPPORTED ? refused : IR_OP_COUNT + (uint32_t)selected;
        stage = selected == TIER1_SELECT_OK ? stage : CHECK_TIER1_SELECTION;
    }
    Tier1EmitResult emitted = { 0 };
    if (stage == CHECK_TIER1_COMPILED)
    {
        const Tier1EmitOptions options = {
            .register_mask = tier1_liveness_register_mask(),
            .vector = !scalar,
            .rematerialize = rematerialize,
            .symbol_access = function->symbol_access,
            .symbol_count = function->symbol_count,
        };
        uint32_t node = BUSTER_IR_NONE;
        const Tier1EmitError error = tier1_emit_function(&plan, &options, lane->allowed, &lane->steps[5], scratch, &emitted, &node);
        *operation = (uint32_t)error;
        stage = error == TIER1_EMIT_OK ? stage : CHECK_TIER1_EMISSION;
    }
    // The code in `output`, as Tier 0's is: its records, branch columns and references.
    const uint64_t mark = output->length;
    const Tier1SelectCode* selected = &emitted.code;
    X86Instruction* records = stage == CHECK_TIER1_COMPILED ? BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, (uint64_t)selected->count + 1) : 0;
    uint32_t* branches = stage == CHECK_TIER1_COMPILED ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)selected->branch_count + 1) : 0;
    uint32_t* targets = stage == CHECK_TIER1_COMPILED ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)selected->branch_count + 1) : 0;
    LinkReference* references = stage == CHECK_TIER1_COMPILED ? BUSTER_ARENA_PUSH_ARRAY(output, LinkReference, (uint64_t)emitted.reference_count + 1) : 0;
    uint32_t* tails = stage == CHECK_TIER1_COMPILED ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)emitted.exit_count + 1) : 0;
    if (stage == CHECK_TIER1_COMPILED && (records == 0 || branches == 0 || targets == 0 || references == 0 || tails == 0))
    {
        arena_pop_to(output, mark);
        stage = CHECK_TIER1_RESOURCE;
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
        };
        *code = (Tier0Code){
            .records = records,
            .count = selected->count,
            .branches = branches,
            .targets = targets,
            .branch_count = selected->branch_count,
            .references = references,
            .reference_count = emitted.reference_count,
            .error = TIER0_OK,
        };
        const bool published = check_tier1_frame(&emitted, selected->count, tails, code);
        counts->open_frames = code->frame_open && (code->frame_saved != 0 || code->frame_pointer || code->frame_size != 0);
        counts->return_tails = code->tail_count;
        if (!published)
        {
            arena_pop_to(output, mark);
            *code = (Tier0Code){ 0 };
            *counts = (CheckTier1Counts){ 0 };
            *operation = TIER1_EMIT_FRAME;
            stage = CHECK_TIER1_EMISSION;
        }
    }
    return stage;
}

static const char* const check_tier1_stage_names[CHECK_TIER1_STAGE_COUNT] = { "tier1", "naked", "lowering", "promotion", "optimization", "analysis", "selection", "emission", "resource" };

static void check_tier1_print_refusal(FILE* stream, const uint8_t* name, uint32_t length, uint32_t outcome)
{
    const uint32_t stage = outcome & 0xff;
    const uint32_t operation = outcome >> 8;
    const char* refused = stage == CHECK_TIER1_SELECTION && operation < IR_OP_COUNT ? ir_op_names[operation] : stage == CHECK_TIER1_SELECTION && operation == IR_OP_COUNT ? "function" : "";
    if (refused[0] != 0)
    {
        fprintf(stream, "tier1-refused\t%.*s\t%s\t%s\n", (int)length, (const char*)name, check_tier1_stage_names[stage], refused);
    }
    else
    {
        fprintf(stream, "tier1-refused\t%.*s\t%s\terror%" PRIu32 "\n", (int)length, (const char*)name, check_tier1_stage_names[stage], stage == CHECK_TIER1_SELECTION ? operation - IR_OP_COUNT : operation);
    }
}

static void check_tier1_print_report(FILE* stream, const CheckTier1Report* report)
{
    const char* const* stages = check_tier1_stage_names;
    fprintf(stream, "tier1\tfunctions=%" PRIu32, report->functions);
    for (uint32_t s = 0; s < CHECK_TIER1_STAGE_COUNT; s += 1)
    {
        fprintf(stream, "\t%s=%" PRIu32, stages[s], report->stages[s]);
    }
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
    for (uint32_t k = 0; k < BUSTER_CHECK_TIER1_OPERATIONS; k += 1)
    {
        if (report->emission[k] != 0)
        {
            fprintf(stream, "\temission.error%" PRIu32 "=%" PRIu32, k, report->emission[k]);
        }
    }
    fprintf(stream, "\trecords=%" PRIu64 "\tspill_stores=%" PRIu64 "\tspill_loads=%" PRIu64 "\tcall_stores=%" PRIu64 "\tcall_loads=%" PRIu64 "\trematerializations=%" PRIu64
                    "\tdropped_definitions=%" PRIu64 "\tedge_moves=%" PRIu64 "\tspill_slots=%" PRIu64 "\tframe_bytes=%" PRIu64 "\topen_frames=%" PRIu64
                    "\treturn_tails=%" PRIu64 "\tred_zones=%" PRIu64 "\n",
            report->records, report->spill_stores, report->spill_loads, report->call_stores, report->call_loads, report->rematerializations,
            report->dropped_definitions, report->edge_moves, report->spill_slots, report->frame_bytes, report->open_frames, report->return_tails, report->red_zones);
}
