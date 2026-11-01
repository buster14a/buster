// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1055's scoped optimizer-to-selector handoff. Include after ir_optimize.c. The ordinary ir_optimize entry point
// remains the rebuilding comparison path. This entry point uses the same preparation and transformation kernels,
// but finishes with one retained CFG and dominator analysis, not optimizer-only use lists, lattice or worklists.
// No analysis survives a mutation: invalidate the caller-owned revision BEFORE editing or retiring any IR column,
// side table or analysis arena. Counts and addresses are not revision identities. The scope and both arenas must
// outlive every consumer; scopes must not be copied, reinitialized or moved while a handle can still be used.

#ifndef BUSTER_IR_OPTIMIZE_VERIFIED_INCLUDED
#define BUSTER_IR_OPTIMIZE_VERIFIED_INCLUDED

typedef struct IrVerifiedRevision IrVerifiedRevision;
struct IrVerifiedRevision
{
    const IrFunction* function;
    uint64_t number;
};

// These counters cover only final verification, not optimizer preparation, lowering, SSA or native emission.
// arena_bytes is retained analysis storage, not a transient high-water mark or resident memory measurement.
typedef struct IrVerifiedAnalysis IrVerifiedAnalysis;
struct IrVerifiedAnalysis
{
    const IrVerifiedRevision* owner;
    uint64_t revision;
    const Arena* arena;
    uint64_t arena_end;
    uint64_t arena_bytes;
    IrCfg cfg;
    Dominators dominators;
    uint32_t shape_checks;
    uint32_t row_checks;
    uint32_t graph_checks;
    uint32_t cfg_builds;
    uint32_t dominator_builds;
};

static void ir_verified_invalidate(IrVerifiedRevision* revision)
{
    revision->function = 0;
    // Exhaustion is terminal, not an ABA wrap to a previously published generation.
    if (revision->number != UINT64_MAX)
    {
        revision->number += 1;
    }
}

// An O(1) capability check, not a substitute for verification or for the mutation protocol above. In particular,
// a write through a raw IR pointer MUST invalidate first, including same-size block/value permutations.
static __attribute__((unused)) bool ir_verified_current(const IrVerifiedAnalysis* analysis, const IrVerifiedRevision* revision, const IrFunction* function)
{
    const bool current = analysis != 0 && revision != 0 && function != 0 && analysis->owner == revision &&
                         analysis->revision != 0 && analysis->revision != UINT64_MAX &&
                         analysis->revision == revision->number && revision->function == function &&
                         analysis->arena != 0 && analysis->arena->length >= analysis->arena_end &&
                         analysis->cfg.block_count == function->block_count &&
                         analysis->dominators.block_count == function->block_count;
    return current;
}

// The shared verifier's exact shape, row, phi, switch and SSA checks, with its CFG retained instead of rebuilt.
// The first graph check preserves the structural boundary before dominance construction. The second checks full
// dominance, including same-block order. Both use ir.c's verifier, not a weaker optimizer-specific substitute.
// On failure neither result nor the analysis arena is published; scratch is restored on every path.
static IrCheck ir_verified_build(const IrFunction* function, Arena* analyses, Arena* scratch, bool scalar, IrVerifiedAnalysis* result)
{
    const uint64_t output_mark = analyses->length;
    const uint64_t scratch_mark = scratch->length;
    IrVerifiedAnalysis candidate = { 0 };
    IrCheck check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, 0);
    if (analyses != scratch)
    {
        candidate.shape_checks += 1;
        check = ir_check_shape(function);
    }
    if (check.error == IR_OK)
    {
        candidate.row_checks += 1;
        check = ir_check_rows(function, !BUSTER_SCALAR_PATH(scalar));
    }
    if (check.error == IR_OK)
    {
        candidate.cfg_builds += 1;
        check.error = ir_cfg_validated(function, analyses, scratch, &candidate.cfg);
    }
    if (check.error == IR_OK)
    {
        candidate.graph_checks += 1;
        check = ir_check_graph(function, &candidate.cfg, scratch, 0, !BUSTER_SCALAR_PATH(scalar));
    }
    arena_pop_to(scratch, scratch_mark);
    if (check.error == IR_OK && ir_function_has_vectors(function))
    {
        check.error = IR_ERROR_UNSUPPORTED;
    }
    if (check.error == IR_OK)
    {
        const DominatorCfg graph = {
            .block_count = candidate.cfg.block_count,
            .edge_count = candidate.cfg.edge_count,
            .entry = function->block_count == 0 ? BUSTER_DOMINATOR_NONE : 0,
            .successor_offsets = candidate.cfg.successor_offsets,
            .successors = candidate.cfg.successors,
            .predecessor_offsets = candidate.cfg.predecessor_offsets,
            .predecessors = candidate.cfg.predecessors,
        };
        DominatorOptions options = dominators_options();
        options.outputs = 0;
        options.path = BUSTER_SCALAR_PATH(scalar) ? DOMINATOR_SCALAR : DOMINATOR_AUTO;
        candidate.dominator_builds += 1;
        const DominatorStatus status = dominators_analyze(analyses, scratch, &graph, options, &candidate.dominators);
        check.error = status == DOMINATOR_OK ? IR_OK : status == DOMINATOR_NO_MEMORY ? IR_ERROR_MEMORY : IR_ERROR_DOMINANCE;
    }
    if (check.error == IR_OK)
    {
        const IrDominance dominance = {
            .enter = candidate.dominators.preorder,
            .leave = candidate.dominators.subtree_end,
            .block_count = candidate.dominators.block_count,
        };
        candidate.graph_checks += 1;
        check = ir_check_graph(function, &candidate.cfg, scratch, &dominance, !BUSTER_SCALAR_PATH(scalar));
    }
    if (check.error == IR_OK)
    {
        candidate.arena = analyses;
        candidate.arena_end = analyses->length;
        candidate.arena_bytes = analyses->length - output_mark;
        *result = candidate;
    }
    else
    {
        arena_pop_to(analyses, output_mark);
    }
    arena_pop_to(scratch, scratch_mark);
    return check;
}

// A deliberately separate orchestration entry point while the existing validating/rebuilding path remains the
// comparison control. There is no second optimization algorithm: every preparation, SCCP, DCE and rewrite kernel
// is shared with ir_optimize. Tests compare all pass masks and both forced twins, including AUTO.
//
// The three arenas and the input columns must be disjoint; result, analysis and revision are distinct live
// descriptors outside their storage. The revision belongs to this function's scope, not a process-wide cache.
// Publication is atomic at the C API boundary: result and analysis are unchanged on failure, and the old revision
// is invalidated even on failure. No pass currently preserves this product across a mutation.
static __attribute__((unused, aligned(64))) IrCheck ir_optimize_verified(const IrFunction* function, Arena* output, Arena* analyses, Arena* scratch, IrOptimizeOptions options, IrVerifiedRevision* revision, IrOptimizeResult* result, IrVerifiedAnalysis* analysis)
{
    const uint64_t output_mark = output->length;
    const uint64_t analysis_mark = analyses->length;
    const uint64_t scratch_mark = scratch->length;
    ir_verified_invalidate(revision);
    IrOptimizeWork work = { 0 };
    IrVerifiedAnalysis candidate = { 0 };
    IrCheck check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, options.passes);
    if (revision->number != UINT64_MAX && output != scratch && analyses != scratch && analyses != output &&
        options.path <= BUSTER_IR_OPTIMIZE_SCALAR &&
        (options.passes & ~(BUSTER_IR_OPTIMIZE_SCCP | BUSTER_IR_OPTIMIZE_DCE | BUSTER_IR_OPTIMIZE_CFG)) == 0)
    {
        options.scalar = options.path == BUSTER_IR_OPTIMIZE_SCALAR ||
                         (options.path == BUSTER_IR_OPTIMIZE_AUTO && (options.scalar || ir_optimize_auto_scalar(function)));
        check = ir_optimize_prepare(&work, function, output, scratch, BUSTER_SCALAR_PATH(options.scalar), true);
    }
    if (check.error == IR_OK)
    {
        const uint32_t block_count = function->block_count;
        const uint32_t instruction_count = function->instruction_count;
        if (block_count != 0)
        {
            work.blocks[0] = 1;
        }
        if ((options.passes & BUSTER_IR_OPTIMIZE_SCCP) != 0)
        {
            if (options.scalar)
            {
                ir_optimize_sccp_worklist(&work);
            }
            else
            {
                ir_optimize_sccp_sweeps(&work, options.sweep_limit);
            }
        }
        else
        {
            const uint32_t* const offsets = work.cfg.successor_offsets;
            const uint32_t* const ranks = work.dominators.rpo_index;
            const uint8_t* const operations = function->op;
            const uint32_t* const operands = function->a;
            const IrConstant* const constants = function->constants;
            for (uint32_t block = 0; block < block_count; block += 1)
            {
                const uint8_t reachable = ranks[block] != BUSTER_DOMINATOR_NONE;
                work.blocks[block] = reachable;
                for (uint32_t edge = offsets[block]; edge < offsets[(uint64_t)block + 1]; edge += 1)
                {
                    work.edges[edge] = reachable;
                }
            }
            for (uint32_t i = 0; i < instruction_count; i += 1)
            {
                work.states[i] = operations[i] == IR_CONST ? IROPT_CONSTANT : IROPT_OVERDEFINED;
                if (operations[i] == IR_CONST)
                {
                    work.values[i] = constants[operands[i]];
                }
            }
        }
        ir_optimize_dce(&work, options);
        check.error = ir_optimize_rewrite(&work, output, scratch, options, &work.statistics.function);
    }
    // The rewrite copies every supported side table into output. None of the work's scratch is a dependency of the
    // rewritten function. Retire it before final verification rather than retaining two analyses' live storage.
    work.statistics.scratch_bytes = scratch->length - scratch_mark;
    arena_pop_to(scratch, scratch_mark);
    if (check.error == IR_OK)
    {
        check = ir_verified_build(&work.statistics.function, analyses, scratch, BUSTER_SCALAR_PATH(options.scalar), &candidate);
    }
    if (check.error == IR_OK)
    {
        *result = work.statistics;
        revision->function = &result->function;
        candidate.owner = revision;
        candidate.revision = revision->number;
        *analysis = candidate;
    }
    else
    {
        arena_pop_to(analyses, analysis_mark);
        arena_pop_to(output, output_mark);
    }
    arena_pop_to(scratch, scratch_mark);
    return check;
}

#endif
