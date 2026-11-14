// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #1055 regression tests. Included by ir_optimize_text_test.c after the existing optimizer fixtures.
// The control uses the unchanged public optimizer, public CFG builder, fresh scalar dominance analysis and public
// full verifier. It does not authorize itself with the retained product. No timings here establish performance.

#include "ir_optimize_verified.c"

#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)

static bool ir_verified_test_column(const uint32_t* a, const uint32_t* b, uint64_t count)
{
    const bool equal = count == 0 || memcmp(a, b, count * sizeof(uint32_t)) == 0;
    return equal;
}

static void ir_verified_test_rebuild(IrOptimizeTest* test, const IrFunction* function, const IrVerifiedAnalysis* analysis, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    IrCfg cfg = { 0 };
    bool ok = ir_build_cfg(function, output, scratch, &cfg) == IR_OK;
    ir_optimize_test_expect(test, ok, "verified handoff: independent CFG rebuild");
    if (ok)
    {
        const IrCfg* retained = &analysis->cfg;
        const uint32_t blocks = cfg.block_count;
        const uint32_t edges = cfg.edge_count;
        const bool same = blocks == retained->block_count && edges == retained->edge_count &&
                          ir_verified_test_column(cfg.successor_offsets, retained->successor_offsets, (uint64_t)blocks + 1) &&
                          ir_verified_test_column(cfg.predecessor_offsets, retained->predecessor_offsets, (uint64_t)blocks + 1) &&
                          ir_verified_test_column(cfg.successors, retained->successors, edges) &&
                          ir_verified_test_column(cfg.predecessors, retained->predecessors, edges) &&
                          ir_verified_test_column(cfg.instruction_blocks, retained->instruction_blocks, function->instruction_count);
        ir_optimize_test_expect(test, same, "verified handoff: all CFG columns match rebuild");
        const DominatorCfg graph = {
            .block_count = blocks,
            .edge_count = edges,
            .entry = blocks == 0 ? BUSTER_DOMINATOR_NONE : 0,
            .successor_offsets = cfg.successor_offsets,
            .successors = cfg.successors,
            .predecessor_offsets = cfg.predecessor_offsets,
            .predecessors = cfg.predecessors,
        };
        DominatorOptions options = dominators_options();
        options.outputs = 0;
        options.path = DOMINATOR_SCALAR;
        Dominators rebuilt = { 0 };
        ok = dominators_analyze(output, scratch, &graph, options, &rebuilt) == DOMINATOR_OK;
        ir_optimize_test_expect(test, ok, "verified handoff: independent dominator rebuild");
        if (ok)
        {
            bool same_dominance = true;
            for (uint32_t a = 0; a < blocks; a += 1)
            {
                for (uint32_t b = 0; b < blocks; b += 1)
                {
                    same_dominance &= dominators_dominates(&rebuilt, a, b) == dominators_dominates(&analysis->dominators, a, b);
                }
            }
            ir_optimize_test_expect(test, same_dominance, "verified handoff: every dominance relation matches rebuild");
            const IrDominance dominance = {
                .enter = rebuilt.preorder,
                .leave = rebuilt.subtree_end,
                .block_count = blocks,
            };
            const IrCheck check = ir_verify_scalar(function, scratch, &dominance);
            ir_optimize_test_expect(test, check.error == IR_OK && check.dominance_checked, "verified handoff: independent full SSA verification");
            // The driver consumes the retained intervals themselves, not only a rebuild's; feed them to the same
            // public verifier so a corrupted retained array cannot hide behind relation equality alone.
            const IrDominance retained = {
                .enter = analysis->dominators.preorder,
                .leave = analysis->dominators.subtree_end,
                .block_count = analysis->dominators.block_count,
            };
            const IrCheck retained_check = ir_verify_scalar(function, scratch, &retained);
            ir_optimize_test_expect(test, retained_check.error == IR_OK && retained_check.dominance_checked,
                                    "verified handoff: retained intervals pass full SSA verification");
        }
    }
    arena_pop_to(output, mark);
}

static void ir_verified_test_case(IrOptimizeTest* test, const IrFunction* input, Arena* output, Arena* analyses, Arena* reference, Arena* scratch, uint32_t passes, uint32_t path, bool fallback, bool scalar_override)
{
    const uint64_t output_mark = output->length;
    const uint64_t analysis_mark = analyses->length;
    const uint64_t reference_mark = reference->length;
    const uint64_t scratch_mark = scratch->length;
    IrOptimizeOptions options = ir_optimize_options();
    options.passes = passes;
    options.path = path;
    options.scalar = scalar_override;
    options.sweep_limit = fallback ? 0 : options.sweep_limit;
    IrOptimizeResult expected = { 0 };
    IrOptimizeResult actual = { 0 };
    IrVerifiedRevision revision = { 0 };
    IrVerifiedAnalysis analysis = { 0 };
    const IrCheck before = ir_optimize(input, reference, scratch, options, &expected);
    const IrCheck after = ir_optimize_verified(input, output, analyses, scratch, options, &revision, &actual, &analysis);
    ir_optimize_test_expect(test, before.error == after.error && before.instruction == after.instruction && before.detail == after.detail,
                            "verified handoff: unchanged optimizer diagnosis");
    ir_optimize_test_expect(test, after.error == IR_OK, "verified handoff: fixture accepted");
    if (before.error == IR_OK && after.error == IR_OK)
    {
        // Dominator AUTO and the explicit scalar controls alike solve the final dominance by SEMI-NCA (#1352).
        ir_optimize_test_expect(test, analysis.dominators.path == DOMINATOR_SCALAR,
                                "verified handoff: final dominance solved by SEMI-NCA, whatever the pass heuristic");
        ir_optimize_test_expect(test, ir_optimize_test_equal(&expected.function, &actual.function), "verified handoff: unchanged optimized IR");
        ir_optimize_test_expect(test, expected.folded == actual.folded && expected.removed_blocks == actual.removed_blocks &&
                                expected.removed_instructions == actual.removed_instructions && expected.sccp_visits == actual.sccp_visits &&
                                expected.sccp_sweeps == actual.sccp_sweeps && expected.sccp_phi_incoming == actual.sccp_phi_incoming &&
                                expected.dce_visits == actual.dce_visits &&
                                expected.dce_sweeps == actual.dce_sweeps && expected.worklist_fallback == actual.worklist_fallback &&
                                expected.scratch_bytes == actual.scratch_bytes,
                                "verified handoff: unchanged transformation statistics");
        if (fallback)
        {
            ir_optimize_test_expect(test, actual.worklist_fallback && actual.sccp_sweeps == 0,
                                    "verified handoff: forced worklist fallback ran");
        }
        ir_optimize_test_expect(test, ir_verified_current(&analysis, &revision, &actual.function), "verified handoff: exact owner and revision accepted");
        ir_optimize_test_expect(test, analysis.shape_checks == 1 && analysis.row_checks == 1 && analysis.graph_checks == 1 &&
                                analysis.dominance_checks == 1 && analysis.cfg_builds == 1 && analysis.dominator_builds == 1 && after.dominance_checked &&
                                analysis.arena_bytes == analyses->length - analysis_mark,
                                "verified handoff: final verification builds exactly one CFG and one dominance analysis");
        // Reallocate and overwrite the retired optimizer workspace before reading the retained graph or IR.
        const uint64_t poison_bytes = actual.scratch_bytes + 4096;
        void* poison = arena_push(scratch, poison_bytes, 1);
        ir_optimize_test_expect(test, poison != 0, "verified handoff: scratch poison allocation");
        if (poison != 0)
        {
            memset(poison, 0xa5, poison_bytes);
        }
        arena_pop_to(scratch, scratch_mark);
        ir_verified_test_rebuild(test, &actual.function, &analysis, reference, scratch);
        const IrFunction copy = actual.function;
        const IrVerifiedRevision other = revision;
        ir_optimize_test_expect(test, !ir_verified_current(&analysis, &revision, &copy) &&
                                !ir_verified_current(&analysis, &other, &actual.function),
                                "verified handoff: copied descriptor and foreign scope rejected");
        const IrVerifiedAnalysis stale = analysis;
        const uint8_t* const old_op = actual.function.op;
        const uint32_t* const old_blocks = actual.function.block_offsets;
        ir_verified_invalidate(&revision);
        ir_optimize_test_expect(test, !ir_verified_current(&stale, &revision, &actual.function), "verified handoff: explicit invalidation");
        // Reuse the exact arena addresses and result descriptor. Counts/pointers alone would authorize stale state.
        arena_pop_to(output, output_mark);
        arena_pop_to(analyses, analysis_mark);
        const IrCheck again = ir_optimize_verified(input, output, analyses, scratch, options, &revision, &actual, &analysis);
        ir_optimize_test_expect(test, again.error == IR_OK && actual.function.op == old_op && actual.function.block_offsets == old_blocks &&
                                ir_verified_current(&analysis, &revision, &actual.function) &&
                                !ir_verified_current(&stale, &revision, &actual.function),
                                "verified handoff: same-address same-count ABA rejected");
        // In-place edits follow exactly the same protocol, even when neither storage nor counts change.
        ir_verified_invalidate(&revision);
        if (again.error == IR_OK && actual.function.instruction_count != 0)
        {
            const uint32_t operand = actual.function.a[0];
            actual.function.a[0] ^= 1;
            ir_optimize_test_expect(test, !ir_verified_current(&analysis, &revision, &actual.function), "verified handoff: in-place mutation invalidates");
            actual.function.a[0] = operand;
        }
    }
    ir_verified_invalidate(&revision);
    arena_pop_to(analyses, analysis_mark);
    arena_pop_to(output, output_mark);
    arena_pop_to(reference, reference_mark);
    ir_optimize_test_expect(test, scratch->length == scratch_mark, "verified handoff: scratch restored");
}

static void ir_verified_test_owned(IrOptimizeTest* test, const IrFunction* function, Arena* input, Arena* output, Arena* analyses, Arena* reference, Arena* scratch)
{
    IrOptimizeResult expected = { 0 };
    IrOptimizeResult actual = { 0 };
    IrVerifiedRevision revision = { 0 };
    IrVerifiedAnalysis analysis = { 0 };
    const IrOptimizeOptions options = ir_optimize_options();
    const bool ok = ir_optimize(function, reference, scratch, options, &expected).error == IR_OK &&
                    ir_optimize_verified(function, output, analyses, scratch, options, &revision, &actual, &analysis).error == IR_OK;
    ir_optimize_test_expect(test, ok, "verified handoff: ownership fixture compiles");
    if (ok)
    {
        // All input storage is live while overwritten, including sparse ABI/call/label columns in those fixtures.
        // No access to function's input columns is permitted after this point.
        memset(input->base, 0xa5, input->length);
        arena_pop_to(input, 0);
        const IrCheck check = ir_verify_scalar(&actual.function, scratch, 0);
        ir_optimize_test_expect(test, check.error == IR_OK && ir_optimize_test_equal(&expected.function, &actual.function),
                                "verified handoff: rewritten columns survive poisoned retired input");
        if (check.error == IR_OK)
        {
            ir_verified_test_rebuild(test, &actual.function, &analysis, reference, scratch);
        }
    }
    ir_verified_invalidate(&revision);
    arena_pop_to(analyses, 0);
    arena_pop_to(output, 0);
    arena_pop_to(reference, 0);
}

static void ir_verified_test_failures(IrOptimizeTest* test, Arena* input, Arena* output, Arena* analyses, Arena* reference, Arena* scratch)
{
    IrFunction function = { 0 };
    const bool created = ir_optimize_test_diamond(input, &function, 2);
    ir_optimize_test_expect(test, created, "verified handoff: failure fixture allocation");
    if (created)
    {
        IrOptimizeOptions options = ir_optimize_options();
        IrOptimizeResult result = { 0 };
        IrVerifiedRevision revision = { 0 };
        IrVerifiedAnalysis analysis = { 0 };
        const IrCheck first = ir_optimize_verified(&function, output, analyses, scratch, options, &revision, &result, &analysis);
        ir_optimize_test_expect(test, first.error == IR_OK, "verified handoff: failure fixture baseline");
        if (first.error == IR_OK)
        {
            const uint64_t needed = analyses->length;
            ir_verified_invalidate(&revision);
            arena_pop_to(output, 0);
            arena_pop_to(analyses, 0);
            IrOptimizeResult saved_result;
            IrVerifiedAnalysis saved_analysis;
            memcpy(&saved_result, &result, sizeof(result));
            memcpy(&saved_analysis, &analysis, sizeof(analysis));
            // The successful run committed the backing storage. Shrink only the copied descriptor's logical limit,
            // exercising every smaller capacity, not only the first allocation, without retiring the real mapping.
            for (uint64_t capacity = 0; capacity < needed; capacity += 1)
            {
                Arena limited = *analyses;
                limited.capacity = capacity;
                const IrCheck check = ir_optimize_verified(&function, output, &limited, scratch, options, &revision, &result, &analysis);
                const bool untouched = check.error == IR_ERROR_MEMORY && output->length == 0 && limited.length == 0 && scratch->length == 0 &&
                                       memcmp(&result, &saved_result, sizeof(result)) == 0 && memcmp(&analysis, &saved_analysis, sizeof(analysis)) == 0 &&
                                       revision.function == 0;
                ir_optimize_test_expect(test, untouched, "verified handoff: every analysis allocation failure rolls back without publication");
            }
            Arena tiny = *output;
            tiny.capacity = tiny.length;
            const IrCheck output_failure = ir_optimize_verified(&function, &tiny, analyses, scratch, options, &revision, &result, &analysis);
            ir_optimize_test_expect(test, output_failure.error == IR_ERROR_MEMORY && tiny.length == 0 && analyses->length == 0 && scratch->length == 0 &&
                                    memcmp(&result, &saved_result, sizeof(result)) == 0 && memcmp(&analysis, &saved_analysis, sizeof(analysis)) == 0,
                                    "verified handoff: output failure rolls back");
            Arena no_scratch = *scratch;
            no_scratch.capacity = no_scratch.length;
            const IrCheck scratch_failure = ir_optimize_verified(&function, output, analyses, &no_scratch, options, &revision, &result, &analysis);
            ir_optimize_test_expect(test, scratch_failure.error == IR_ERROR_MEMORY && output->length == 0 && analyses->length == 0 && no_scratch.length == 0 &&
                                    memcmp(&result, &saved_result, sizeof(result)) == 0 && memcmp(&analysis, &saved_analysis, sizeof(analysis)) == 0,
                                    "verified handoff: scratch failure rolls back");
            options.passes = UINT32_MAX;
            const IrCheck bad_options = ir_optimize_verified(&function, output, analyses, scratch, options, &revision, &result, &analysis);
            ir_optimize_test_expect(test, bad_options.error == IR_ERROR_METADATA && revision.function == 0 &&
                                    memcmp(&result, &saved_result, sizeof(result)) == 0 && memcmp(&analysis, &saved_analysis, sizeof(analysis)) == 0,
                                    "verified handoff: invalid options cannot leave an authorized old product");
            options = ir_optimize_options();
            revision.number = UINT64_MAX - 1;
            const IrCheck exhausted = ir_optimize_verified(&function, output, analyses, scratch, options, &revision, &result, &analysis);
            ir_optimize_test_expect(test, exhausted.error == IR_ERROR_METADATA && revision.number == UINT64_MAX && revision.function == 0 &&
                                    output->length == 0 && analyses->length == 0 && scratch->length == 0,
                                    "verified handoff: revision exhaustion fails closed without wrap");
            IrVerifiedRevision malformed_revision = { 0 };
            // Same independently known malformed SSA fixture as the validating optimizer tests.
            function.a[4] = 6;
            const IrCheck malformed = ir_optimize_verified(&function, output, analyses, scratch, options, &malformed_revision, &result, &analysis);
            ir_optimize_test_expect(test, malformed.error == IR_ERROR_DOMINANCE && output->length == 0 && analyses->length == 0 && scratch->length == 0 &&
                                    memcmp(&result, &saved_result, sizeof(result)) == 0 && memcmp(&analysis, &saved_analysis, sizeof(analysis)) == 0,
                                    "verified handoff: malformed SSA still refused transactionally");
            // Bypass optimization to exercise the retained final verifier itself, not just preparation's refusal.
            IrVerifiedAnalysis final = { .revision = 1055 };
            IrVerifiedAnalysis saved_final;
            memcpy(&saved_final, &final, sizeof(final));
            const IrCheck final_bad = ir_verified_build(&function, analyses, scratch, true, &final);
            ir_optimize_test_expect(test, final_bad.error == IR_ERROR_DOMINANCE && analyses->length == 0 && scratch->length == 0 &&
                                    memcmp(&final, &saved_final, sizeof(final)) == 0, "verified handoff: final verifier still rejects nondominating uses");
            function.a[4] = 1;
            function.op[0] = IR_INVALID;
            const IrCheck row_bad = ir_verified_build(&function, analyses, scratch, false, &final);
            const IrCheck public_bad = ir_verify(&function, scratch, 0);
            ir_optimize_test_expect(test, row_bad.error == public_bad.error && row_bad.instruction == public_bad.instruction &&
                                    analyses->length == 0 && scratch->length == 0 && memcmp(&final, &saved_final, sizeof(final)) == 0,
                                    "verified handoff: final row diagnostics match the standalone verifier");
            // The reference arena must also be distinct from the graph's scratch owner.
            const IrCheck alias = ir_verified_build(&function, reference, reference, true, &final);
            ir_optimize_test_expect(test, alias.error == IR_ERROR_METADATA && reference->length == 0, "verified handoff: aliased analysis/scratch refused");
            // The public entry point refuses the same aliasing before preparing or publishing anything.
            IrVerifiedRevision alias_revision = { 0 };
            Arena alias_output = *analyses;
            const IrCheck output_analysis = ir_optimize_verified(&function, &alias_output, &alias_output, scratch, options, &alias_revision, &result, &analysis);
            ir_optimize_test_expect(test, output_analysis.error == IR_ERROR_METADATA && alias_output.length == 0 && alias_revision.function == 0,
                                    "verified handoff: aliased output/analysis refused");
            alias_revision = (IrVerifiedRevision){ 0 };
            Arena alias_scratch = *scratch;
            const IrCheck output_scratch = ir_optimize_verified(&function, &alias_scratch, analyses, &alias_scratch, options, &alias_revision, &result, &analysis);
            ir_optimize_test_expect(test, output_scratch.error == IR_ERROR_METADATA && alias_scratch.length == 0 && alias_revision.function == 0,
                                    "verified handoff: aliased output/scratch refused");
        }
        ir_verified_invalidate(&revision);
    }
    arena_pop_to(input, 0);
}


static bool ir_verified_test_diagnosis(IrCheck a, IrCheck b)
{
    return a.error == b.error && a.instruction == b.instruction && a.detail == b.detail && a.dominance_checked == b.dominance_checked;
}

// The intervals come from the fixture's independently known graph, never a retained product. Structural errors
// precede cross-block dominance errors at these boundaries, even when the latter use appears earlier in the IR.
static void ir_verified_test_boundaries(IrOptimizeTest* test, const IrFunction* function, const IrDominance* dominance, IrError error, uint32_t instruction, IrError prepare_error, Arena* analyses, Arena* reference, Arena* scratch)
{
    const uint64_t analysis_mark = analyses->length;
    const uint64_t reference_mark = reference->length;
    const uint64_t scratch_mark = scratch->length;
    const IrCheck structural = ir_verify_scalar(function, scratch, 0);
    const IrCheck expected = structural.error == IR_OK ? ir_verify_scalar(function, scratch, dominance) : structural;
    ir_optimize_test_expect(test, expected.error == error && expected.instruction == instruction, "verification reuse: independently known fixture diagnosis");
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        IrCfg cfg = { .edge_count = 1379 };
        IrCfg saved_cfg;
        memcpy(&saved_cfg, &cfg, sizeof(cfg));
        const IrCheck retained_cfg = ir_verify_cfg(function, reference, scratch, scalar == 0, &cfg);
        ir_optimize_test_expect(test, ir_verified_test_diagnosis(retained_cfg, structural) && scratch->length == scratch_mark,
                                "verification reuse: retained CFG preserves full structural diagnosis and scratch");
        if (retained_cfg.error == IR_OK)
        {
            const IrCheck remaining = ir_verify_dominance(function, &cfg, dominance, scalar == 0);
            ir_optimize_test_expect(test, ir_verified_test_diagnosis(remaining, expected), "verification reuse: remaining dominance matches fresh full verifier");
        }
        else
        {
            ir_optimize_test_expect(test, reference->length == reference_mark && memcmp(&cfg, &saved_cfg, sizeof(cfg)) == 0,
                                    "verification reuse: failed CFG validation restores storage without publication");
        }
        arena_pop_to(reference, reference_mark);
        const IrCheck ordinary = ir_optimize_verify_twins(function, reference, scratch, scalar != 0, scalar != 0);
        ir_optimize_test_expect(test, ir_verified_test_diagnosis(ordinary, expected) && reference->length == reference_mark && scratch->length == scratch_mark,
                                "verification reuse: ordinary final verification preserves exact diagnosis and arena prefixes");
        IrVerifiedAnalysis analysis = { .revision = 1379 };
        IrVerifiedAnalysis saved_analysis;
        memcpy(&saved_analysis, &analysis, sizeof(analysis));
        const IrCheck retained = ir_verified_build(function, analyses, scratch, scalar != 0, &analysis);
        ir_optimize_test_expect(test, ir_verified_test_diagnosis(retained, expected) && scratch->length == scratch_mark,
                                "verification reuse: retained final verification preserves exact diagnosis and scratch prefix");
        if (retained.error != IR_OK)
        {
            ir_optimize_test_expect(test, analyses->length == analysis_mark && memcmp(&analysis, &saved_analysis, sizeof(analysis)) == 0,
                                    "verification reuse: malformed final IR cannot publish an analysis");
        }
        arena_pop_to(analyses, analysis_mark);
        IrOptimizeWork work = { 0 };
        const IrCheck prepared = ir_optimize_prepare_twins(&work, function, reference, scratch, scalar != 0, scalar != 0, true);
        ir_optimize_test_expect(test, prepared.error == prepare_error, "verification reuse: preparation keeps its reachable-use contract");
        arena_pop_to(scratch, scratch_mark);
        arena_pop_to(reference, reference_mark);
    }
}

static void ir_verified_test_malformed(IrOptimizeTest* test, Arena* input, Arena* analyses, Arena* reference, Arena* scratch)
{
    const uint64_t analysis_mark = analyses->length;
    const uint64_t reference_mark = reference->length;
    const uint64_t scratch_mark = scratch->length;
    // Retain nonempty caller prefixes while every path succeeds or rolls back.
    const bool prefixed = arena_push(analyses, 17, 1) != 0 && arena_push(reference, 19, 1) != 0 && arena_push(scratch, 23, 1) != 0;
    ir_optimize_test_expect(test, prefixed, "verification reuse: caller-prefix allocation");
    static const uint32_t enter[] = { 0, 1, 2, 3 };
    static const uint32_t leave[] = { 4, 2, 3, 4 };
    const IrDominance diamond = {
        .enter = enter,
        .leave = leave,
        .block_count = 4,
    };
    // An unreachable backedge supplies a syntactically complete entry phi, but initial entry has no incoming value.
    // Both final-verification paths must retain the shared structural rejection before publishing analysis.
    if (prefixed)
    {
        const uint64_t input_mark = input->length;
        const IrSizes sizes = { .instruction_count = 4, .block_count = 2, .constant_count = 1, .extra_count = 2 };
        IrFunction function = { 0 };
        const bool created = ir_create(input, &sizes, &function) == IR_OK;
        ir_optimize_test_expect(test, created, "verification reuse: entry phi allocation");
        if (created)
        {
            function.result_type = IR_I64;
            function.block_offsets[1] = 2;
            function.block_offsets[2] = 4;
            function.constants[0].low = 7;
            function.extra[0] = 1;
            function.extra[1] = 2;
            ir_optimize_test_instruction(&function, 0, IR_PHI, IR_I64, 0, 1, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 1, IR_RET, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 2, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 3, IR_BR, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t entry[] = { 0, 0 };
            const uint32_t exit[] = { 1, 0 };
            const IrDominance dominance = { .enter = entry, .leave = exit, .block_count = 2 };
            ir_verified_test_boundaries(test, &function, &dominance, IR_ERROR_PHI, 0, IR_ERROR_PHI, analyses, reference, scratch);
        }
        arena_pop_to(input, input_mark);
    }
    for (uint32_t kind = 0; prefixed && kind < 10; kind += 1)
    {
        const uint64_t input_mark = input->length;
        IrFunction function = { 0 };
        const bool created = ir_optimize_test_diamond(input, &function, 2);
        ir_optimize_test_expect(test, created, "verification reuse: malformed diamond allocation");
        if (created)
        {
            IrError error = IR_OK;
            uint32_t instruction = BUSTER_IR_NONE;
            switch (kind)
            {
                case 1:
                {
                    function.op[0] = IR_INVALID;
                    error = IR_ERROR_OPCODE;
                    instruction = 0;
                } break;
                case 2:
                {
                    function.a[4] = function.instruction_count;
                    error = IR_ERROR_OPERAND;
                    instruction = 4;
                } break;
                case 3:
                {
                    function.extra = 0;
                    error = IR_ERROR_STRUCTURE;
                } break;
                case 4:
                {
                    function.extra[0] = 0;
                    error = IR_ERROR_PHI;
                    instruction = 8;
                } break;
                case 5:
                {
                    function.extra[2] = 1;
                    error = IR_ERROR_PHI;
                    instruction = 8;
                } break;
                case 6:
                {
                    function.extra[1] = 0;
                    error = IR_ERROR_PHI;
                    instruction = 8;
                } break;
                case 7:
                {
                    function.a[4] = 6;
                    error = IR_ERROR_DOMINANCE;
                    instruction = 4;
                } break;
                case 8:
                {
                    function.a[4] = 6;
                    function.a[9] = 9;
                    error = IR_ERROR_DOMINANCE;
                    instruction = 9;
                } break;
                case 9:
                {
                    function.a[4] = 6;
                    function.extra[2] = 1;
                    error = IR_ERROR_PHI;
                    instruction = 8;
                } break;
                default: break;
            }
            ir_verified_test_boundaries(test, &function, &diamond, error, instruction, error, analyses, reference, scratch);
        }
        arena_pop_to(input, input_mark);
    }
    for (uint32_t kind = 0; prefixed && kind < 4; kind += 1)
    {
        const uint64_t input_mark = input->length;
        IrFunction function = { 0 };
        const bool created = ir_optimize_test_switch(input, &function, false);
        ir_optimize_test_expect(test, created, "verification reuse: malformed switch allocation");
        if (created)
        {
            IrError error = IR_OK;
            uint32_t instruction = BUSTER_IR_NONE;
            if (kind == 1)
            {
                function.extra[3] = function.extra[1];
                error = IR_ERROR_METADATA;
                instruction = 1;
            }
            else if (kind == 2)
            {
                function.c[1] = UINT32_MAX;
                error = IR_ERROR_TYPE;
                instruction = 1;
            }
            else if (kind == 3)
            {
                function.extra[2] = function.block_count;
                error = IR_ERROR_TYPE;
                instruction = 1;
            }
            ir_verified_test_boundaries(test, &function, &diamond, error, instruction, error, analyses, reference, scratch);
        }
        arena_pop_to(input, input_mark);
    }
    const uint32_t single_enter[] = { 0 };
    const uint32_t single_leave[] = { 1 };
    const IrDominance single = {
        .enter = single_enter,
        .leave = single_leave,
        .block_count = 1,
    };
    for (uint32_t kind = 0; prefixed && kind < 3; kind += 1)
    {
        const uint64_t input_mark = input->length;
        IrFunction function = { 0 };
        const bool created = ir_optimize_test_aggregate_call(input, &function);
        ir_optimize_test_expect(test, created, "verification reuse: malformed call allocation");
        if (created)
        {
            if (kind == 1)
            {
                function.calls[0].argument_first = function.extra_count;
            }
            else if (kind == 2)
            {
                function.calls[0].abi = function.abi_call_count + 1;
            }
            const IrError error = kind == 0 ? IR_OK : IR_ERROR_METADATA;
            ir_verified_test_boundaries(test, &function, &single, error, BUSTER_IR_NONE, error, analyses, reference, scratch);
        }
        arena_pop_to(input, input_mark);
    }
    for (uint32_t reachable = 0; prefixed && reachable < 2; reachable += 1)
    {
        const uint64_t input_mark = input->length;
        const IrSizes sizes = {
            .instruction_count = 6,
            .block_count = 3,
            .constant_count = 1,
        };
        IrFunction function = { 0 };
        const bool created = ir_create(input, &sizes, &function) == IR_OK;
        ir_optimize_test_expect(test, created, "verification reuse: unreachable-region allocation");
        if (created)
        {
            // Entry returns immediately. The two dead blocks have a cross-block use, allowed at preparation but
            // forbidden by the strict final boundary. The second case also uses that dead definition at entry.
            function.result_type = IR_I64;
            function.constants[0].low = 7;
            function.block_offsets[1] = 2;
            function.block_offsets[2] = 4;
            function.block_offsets[3] = 6;
            ir_optimize_test_instruction(&function, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 1, IR_RET, IR_VOID, reachable != 0 ? 2 : 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 2, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 3, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 4, IR_COPY, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 5, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t dead_enter[] = { 0, 0, 0 };
            const uint32_t dead_leave[] = { 1, 0, 0 };
            const IrDominance dead = {
                .enter = dead_enter,
                .leave = dead_leave,
                .block_count = 3,
            };
            ir_verified_test_boundaries(test, &function, &dead, IR_ERROR_DOMINANCE, reachable != 0 ? 1 : 4,
                                        reachable != 0 ? IR_ERROR_DOMINANCE : IR_OK, analyses, reference, scratch);
        }
        arena_pop_to(input, input_mark);
    }
    if (prefixed)
    {
        const uint64_t input_mark = input->length;
        const uint64_t retained_mark = analyses->length;
        const uint64_t work_mark = scratch->length;
        const IrSizes sizes = {
            .instruction_count = 8,
            .block_count = 4,
            .constant_count = 1,
        };
        IrFunction function = { 0 };
        const bool created = ir_create(input, &sizes, &function) == IR_OK;
        ir_optimize_test_expect(test, created, "verification reuse: mixed reachable/dead errors allocation");
        if (created)
        {
            // Entry jumps over dead block 1 to reachable block 2. Both use dead block 3's definition. Ordinary
            // final verification historically checked reachable uses first; retained verification used row order.
            function.result_type = IR_I64;
            function.constants[0].low = 7;
            for (uint32_t block = 1; block <= 4; block += 1)
            {
                function.block_offsets[block] = 2 * block;
            }
            ir_optimize_test_instruction(&function, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 1, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 2, IR_COPY, IR_I64, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 3, IR_RET, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 4, IR_COPY, IR_I64, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 5, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 6, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&function, 7, IR_RET, IR_VOID, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t mixed_enter[] = { 0, 0, 1, 0 };
            const uint32_t mixed_leave[] = { 2, 0, 2, 0 };
            const IrDominance mixed = {
                .enter = mixed_enter,
                .leave = mixed_leave,
                .block_count = 4,
            };
            const IrCheck first = ir_verify_scalar(&function, scratch, &mixed);
            ir_optimize_test_expect(test, first.error == IR_ERROR_DOMINANCE && first.instruction == 2 && first.detail == 0,
                                    "verification reuse: fresh full verifier finds the earlier dead use");
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                IrVerifiedAnalysis analysis = { 0 };
                const IrCheck retained = ir_verified_build(&function, analyses, scratch, scalar != 0, &analysis);
                const IrCheck ordinary = ir_optimize_verify_twins(&function, reference, scratch, scalar != 0, scalar != 0);
                ir_optimize_test_expect(test, ir_verified_test_diagnosis(retained, first) && analyses->length == retained_mark && scratch->length == work_mark,
                                        "verification reuse: retained final verifier preserves first-use order");
                ir_optimize_test_expect(test, ordinary.error == IR_ERROR_DOMINANCE && ordinary.instruction == 4 && ordinary.detail == 0,
                                        "verification reuse: ordinary final verifier preserves reachable-error precedence");
            }
        }
        arena_pop_to(input, input_mark);
    }
    arena_pop_to(scratch, scratch_mark);
    arena_pop_to(reference, reference_mark);
    arena_pop_to(analyses, analysis_mark);
}

static void ir_verified_test_dominance_views(IrOptimizeTest* test, Arena* input, Arena* output, Arena* scratch)
{
    const uint64_t input_mark = input->length;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrFunction function = { 0 };
    const bool created = ir_optimize_test_diamond(input, &function, 2);
    ir_optimize_test_expect(test, created, "verification reuse: dominance-view allocation");
    for (uint32_t scalar = 0; created && scalar < 2; scalar += 1)
    {
        IrCfg cfg = { 0 };
        const IrCheck ready = ir_verify_cfg(&function, output, scratch, scalar == 0, &cfg);
        ir_optimize_test_expect(test, ready.error == IR_OK, "verification reuse: dominance-view structural prerequisite");
        for (uint32_t kind = 0; ready.error == IR_OK && kind < 6; kind += 1)
        {
            uint32_t enter[] = { 0, 1, 2, 3 };
            uint32_t leave[] = { 4, 2, 3, 4 };
            IrDominance dominance = {
                .enter = enter,
                .leave = leave,
                .block_count = 4,
            };
            switch (kind)
            {
                case 1: dominance.block_count -= 1; break;
                case 2: dominance.enter = 0; break;
                case 3: dominance.leave = 0; break;
                case 4: leave[1] = enter[1]; break;
                case 5: leave[0] = 0; break;
                default: break;
            }
            const IrCheck expected = ir_verify_scalar(&function, scratch, &dominance);
            const IrCheck actual = ir_verify_dominance(&function, &cfg, &dominance, scalar == 0);
            ir_optimize_test_expect(test, expected.error == (kind == 0 ? IR_OK : IR_ERROR_DOMINANCE) && ir_verified_test_diagnosis(actual, expected),
                                    "verification reuse: bad dominance views cannot bypass the full verifier's checks");
        }
        arena_pop_to(output, output_mark);
        IrCfg unpublished = { .edge_count = 1379 };
        IrCfg saved;
        memcpy(&saved, &unpublished, sizeof(saved));
        Arena limited = *output;
        limited.capacity = limited.length;
        const IrCheck capacity = ir_verify_cfg(&function, &limited, scratch, scalar == 0, &unpublished);
        ir_optimize_test_expect(test, capacity.error == IR_ERROR_MEMORY && limited.length == output_mark && scratch->length == scratch_mark &&
                                memcmp(&unpublished, &saved, sizeof(saved)) == 0, "verification reuse: CFG allocation failure is transactional");
        const IrCheck alias = ir_verify_cfg(&function, scratch, scratch, scalar == 0, &unpublished);
        ir_optimize_test_expect(test, alias.error != IR_OK && scratch->length == scratch_mark && memcmp(&unpublished, &saved, sizeof(saved)) == 0,
                                "verification reuse: aliased retained CFG arenas are refused without publication");
    }
    arena_pop_to(scratch, scratch_mark);
    arena_pop_to(output, output_mark);
    arena_pop_to(input, input_mark);
}

static void ir_verified_test_dominator_policy(IrOptimizeTest* test, Arena* input, Arena* output, Arena* scratch)
{
    IrFunction function = { 0 };
    const bool created = ir_optimize_test_diamond(input, &function, 2);
    ir_optimize_test_expect(test, created, "dominator policy: branchy fixture allocation");
    if (created)
    {
        ir_optimize_test_expect(test, ir_optimize_auto_scalar(&function), "dominator policy: fixture chooses scalar transformation passes");
        for (uint32_t explicit_scalar = 0; explicit_scalar < 2; explicit_scalar += 1)
        {
            IrOptimizeWork work = { 0 };
            const uint64_t output_mark = output->length;
            const uint64_t scratch_mark = scratch->length;
            const IrCheck check = ir_optimize_prepare_twins(&work, &function, output, scratch, true, explicit_scalar != 0, true);
            ir_optimize_test_expect(test, check.error == IR_OK, "dominator policy: preparation accepted");
            if (check.error == IR_OK)
            {
                ir_optimize_test_expect(test, work.scalar && work.dominators.path == DOMINATOR_SCALAR,
                                        "dominator policy: AUTO and explicit scalar dominance both solve by SEMI-NCA");
                // Independently known diamond: entry dominates every block, neither arm dominates the join.
                const uint32_t join = function.block_count - 1;
                ir_optimize_test_expect(test, dominators_dominates(&work.dominators, 0, join) &&
                                        !dominators_dominates(&work.dominators, 1, join) &&
                                        !dominators_dominates(&work.dominators, 2, join),
                                        "dominator policy: diamond relation is unchanged");
            }
            arena_pop_to(scratch, scratch_mark);
            arena_pop_to(output, output_mark);
        }
    }
    arena_pop_to(input, 0);
}

static void ir_verified_tests(IrOptimizeTest* test)
{
    Arena input = arena_reserve(8ull << 20);
    Arena output = arena_reserve(8ull << 20);
    Arena analyses = arena_reserve(8ull << 20);
    Arena reference = arena_reserve(16ull << 20);
    Arena scratch = arena_reserve(32ull << 20);
    const bool ready = input.base != 0 && output.base != 0 && analyses.base != 0 && reference.base != 0 && scratch.base != 0;
    ir_optimize_test_expect(test, ready, "verified handoff: arenas");
    static const uint32_t lengths[] = { 0, 1, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129 };
    const uint32_t fixed = 10;
    const uint32_t cases = fixed + sizeof(lengths) / sizeof(lengths[0]);
    for (uint32_t kind = 0; ready && kind < cases; kind += 1)
    {
        IrFunction function = { 0 };
        bool created;
        if (kind < 4)
        {
            created = ir_optimize_test_diamond(&input, &function, kind);
        }
        else if (kind == 4)
        {
            created = ir_optimize_test_loop(&input, &function);
        }
        else if (kind < 8)
        {
            created = ir_optimize_test_labels(&input, &function, kind - 5);
        }
        else if (kind == 8)
        {
            created = ir_optimize_test_aggregate_call(&input, &function);
        }
        else if (kind == 9)
        {
            const IrSizes sizes = { 0 };
            created = ir_create(&input, &sizes, &function) == IR_OK;
        }
        else
        {
            created = ir_optimize_test_random_dag(&input, &function, test, lengths[kind - fixed]);
        }
        ir_optimize_test_expect(test, created, "verified handoff: original fixture allocation");
        for (uint32_t path = BUSTER_IR_OPTIMIZE_AUTO; created && path <= BUSTER_IR_OPTIMIZE_SCALAR; path += 1)
        {
            for (uint32_t passes = 0; passes < 32; passes += 1)
            {
                ir_verified_test_case(test, &function, &output, &analyses, &reference, &scratch, passes, path, false, false);
            }
        }
        if (created)
        {
            ir_verified_test_case(test, &function, &output, &analyses, &reference, &scratch, 7, BUSTER_IR_OPTIMIZE_VECTOR, true, false);
            ir_verified_test_case(test, &function, &output, &analyses, &reference, &scratch, 0, BUSTER_IR_OPTIMIZE_AUTO, false, true);
            ir_verified_test_case(test, &function, &output, &analyses, &reference, &scratch, 0, BUSTER_IR_OPTIMIZE_VECTOR, false, true);
            ir_verified_test_owned(test, &function, &input, &output, &analyses, &reference, &scratch);
        }
        arena_pop_to(&input, 0);
    }
    if (ready)
    {
        ir_verified_test_malformed(test, &input, &analyses, &reference, &scratch);
        ir_verified_test_dominance_views(test, &input, &output, &scratch);
        ir_verified_test_dominator_policy(test, &input, &output, &scratch);
        ir_verified_test_failures(test, &input, &output, &analyses, &reference, &scratch);
    }
    arena_release(&scratch);
    arena_release(&reference);
    arena_release(&analyses);
    arena_release(&output);
    arena_release(&input);
}

#endif
