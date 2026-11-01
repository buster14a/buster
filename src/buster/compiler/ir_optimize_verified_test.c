// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #1055 regression tests. Included by ir_optimize_text_test.c after the existing optimizer fixtures.
// The control uses the unchanged public optimizer, public CFG builder, fresh scalar dominance analysis and public
// full verifier. It does not authorize itself with the retained product. No timings here establish performance.

#include "ir_optimize_verified.c"

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
        }
    }
    arena_pop_to(output, mark);
}

static void ir_verified_test_case(IrOptimizeTest* test, const IrFunction* input, Arena* output, Arena* analyses, Arena* reference, Arena* scratch, uint32_t passes, uint32_t path, bool fallback)
{
    const uint64_t output_mark = output->length;
    const uint64_t analysis_mark = analyses->length;
    const uint64_t reference_mark = reference->length;
    const uint64_t scratch_mark = scratch->length;
    IrOptimizeOptions options = ir_optimize_options();
    options.passes = passes;
    options.path = path;
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
        ir_optimize_test_expect(test, ir_optimize_test_equal(&expected.function, &actual.function), "verified handoff: unchanged optimized IR");
        ir_optimize_test_expect(test, expected.folded == actual.folded && expected.removed_blocks == actual.removed_blocks &&
                                expected.removed_instructions == actual.removed_instructions && expected.sccp_visits == actual.sccp_visits &&
                                expected.sccp_sweeps == actual.sccp_sweeps && expected.dce_visits == actual.dce_visits &&
                                expected.dce_sweeps == actual.dce_sweeps && expected.worklist_fallback == actual.worklist_fallback,
                                "verified handoff: unchanged transformation statistics");
        ir_optimize_test_expect(test, ir_verified_current(&analysis, &revision, &actual.function), "verified handoff: exact owner and revision accepted");
        ir_optimize_test_expect(test, analysis.shape_checks == 1 && analysis.row_checks == 1 && analysis.graph_checks == 2 &&
                                analysis.cfg_builds == 1 && analysis.dominator_builds == 1 && after.dominance_checked &&
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
        }
        ir_verified_invalidate(&revision);
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
            for (uint32_t passes = 0; passes < 8; passes += 1)
            {
                ir_verified_test_case(test, &function, &output, &analyses, &reference, &scratch, passes, path, false);
            }
        }
        if (created)
        {
            ir_verified_test_case(test, &function, &output, &analyses, &reference, &scratch, 7, BUSTER_IR_OPTIMIZE_VECTOR, true);
            ir_verified_test_owned(test, &function, &input, &output, &analyses, &reference, &scratch);
        }
        arena_pop_to(&input, 0);
    }
    if (ready)
    {
        ir_verified_test_failures(test, &input, &output, &analyses, &reference, &scratch);
    }
    arena_release(&scratch);
    arena_release(&reference);
    arena_release(&analyses);
    arena_release(&output);
    arena_release(&input);
}
