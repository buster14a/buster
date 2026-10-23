// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Structural validation of the whole graph and SSA dominance for reachable uses, using the existing CFG and
// dominator consumers. Include after dominators.c and ir.c. Unreachable uses retain shape/type/phi/order checks;
// they impose no cross-block dominance relation. A reachable use of an unreachable definition is rejected.
// ir_verify_complete borrows the unused remainder of scratch as two disjoint, 64-byte-aligned Arena views: the
// left view owns CFG/tree columns for this call, and the right view owns temporary work. Neither view owns a mapping
// or outlives the call. The caller's length and prefix are preserved on every outcome; no hidden arena is reserved.
// Input columns must not overlap scratch's unused remainder. Tree-only analysis caps matrix work at 16 MiB and
// falls back to the existing scalar algorithm; the scalar entry path requests that algorithm directly.

#define BUSTER_IR_VALIDATE_MATRIX_BYTES (16ull << 20)

static IrCheck ir_validate_reachable(const IrFunction* function, const IrCfg* cfg, const Dominators* tree, bool vector)
{
    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    IrUseBatch batch = { 0 };
    for (uint32_t instruction = 0; check.error == IR_OK && instruction < function->instruction_count; instruction += 1)
    {
        const uint64_t count = ir_operand_count(function, instruction);
        for (uint32_t position = 0; check.error == IR_OK && position < count; position += 1)
        {
            const IrOperand operand = ir_operand(function, instruction, position, cfg->instruction_blocks[instruction]);
            if (tree->subtree_end[operand.block] != 0)
            {
                const uint32_t definition = cfg->instruction_blocks[operand.value];
                const uint32_t lane = batch.count;
                batch.definitions[lane] = operand.value;
                batch.positions[lane] = function->op[instruction] == IR_PHI ? function->block_offsets[operand.block + 1] - 1 : instruction;
                batch.definition_blocks[lane] = definition;
                batch.use_blocks[lane] = operand.block;
                batch.definition_enter[lane] = tree->preorder[definition];
                batch.definition_leave[lane] = tree->subtree_end[definition];
                batch.use_enter[lane] = tree->preorder[operand.block];
                batch.use_leave[lane] = tree->subtree_end[operand.block];
                batch.instructions[lane] = instruction;
                batch.operands[lane] = operand.position;
                batch.count += 1;
                if (batch.count == BUSTER_IR_VECTOR_COUNT)
                {
                    check = ir_check_use_batch(&batch, true, vector);
                    batch.count = 0;
                }
            }
        }
    }
    if (check.error == IR_OK && batch.count != 0)
    {
        check = ir_check_use_batch(&batch, true, vector);
    }
    if (check.error == IR_OK)
    {
        check.dominance_checked = true;
    }
    return check;
}

static __attribute__((unused)) IrCheck ir_verify_complete(const IrFunction* function, Arena* scratch, bool vector)
{
    IrCheck check = ir_check(IR_ERROR_STRUCTURE, BUSTER_IR_NONE, 0);
    const uint64_t mark = scratch != 0 ? scratch->length : 0;
    Arena columns = { 0 };
    Arena temporary = { 0 };
    if (function != 0 && scratch != 0 && scratch->length <= scratch->capacity)
    {
        check.error = IR_ERROR_MEMORY;
        const uintptr_t position = (uintptr_t)scratch->base + mark;
        const uint64_t padding = (UINT64_C(0) - position) & 63u;
        const uint64_t remaining = scratch->capacity - mark;
        if (scratch->base != 0 && padding <= remaining)
        {
            const uint64_t available = remaining - padding;
            const uint64_t left_bytes = (available / 2) & ~UINT64_C(63);
            uint8_t* base = scratch->base + mark + padding;
            columns.base = base;
            columns.capacity = left_bytes;
            temporary.base = base + left_bytes;
            temporary.capacity = available - left_bytes;
            // Keep all existing structural, type, phi coverage and same-block ordering checks, including dead code.
            check = vector ? ir_verify(function, &temporary, 0) : ir_verify_scalar(function, &temporary, 0);
            IrCfg cfg = { 0 };
            if (check.error == IR_OK)
            {
                check.error = ir_build_cfg(function, &columns, &temporary, &cfg);
            }
            if (check.error == IR_OK)
            {
                const DominatorCfg graph = {
                    .block_count = cfg.block_count,
                    .edge_count = cfg.edge_count,
                    .entry = cfg.block_count == 0 ? BUSTER_DOMINATOR_NONE : 0,
                    .successor_offsets = cfg.successor_offsets,
                    .successors = cfg.successors,
                    .predecessor_offsets = cfg.predecessor_offsets,
                    .predecessors = cfg.predecessors,
                };
                // The tree solver retains one frame and three u32 work columns per block before any matrix.
                // Reserve both possible 64-byte alignment gaps so AUTO picks scalar when its matrix cannot fit.
                const uint64_t retained = (uint64_t)cfg.block_count * (sizeof(DominatorFrame) + 3 * sizeof(uint32_t)) + 126;
                const uint64_t budget = temporary.capacity > retained ? temporary.capacity - retained : 0;
                const DominatorOptions options = {
                    .path = vector ? DOMINATOR_AUTO : DOMINATOR_SCALAR,
                    .outputs = 0,
                    .max_matrix_bytes = budget < BUSTER_IR_VALIDATE_MATRIX_BYTES ? budget : BUSTER_IR_VALIDATE_MATRIX_BYTES,
                };
                Dominators tree = { 0 };
                const DominatorStatus status = dominators_analyze(&columns, &temporary, &graph, options, &tree);
                if (status == DOMINATOR_OK)
                {
                    check = ir_validate_reachable(function, &cfg, &tree, vector);
                }
                else
                {
                    check.error = status == DOMINATOR_NO_MEMORY ? IR_ERROR_MEMORY : status == DOMINATOR_MATRIX_LIMIT ? IR_ERROR_LIMIT : IR_ERROR_DOMINANCE;
                }
            }
        }
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, mark);
    }
    return check;
}
