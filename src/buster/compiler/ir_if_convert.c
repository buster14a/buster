// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Bounded scalar diamond/triangle if-conversion (#1382). Include after ir.c. The caller supplies verified SSA,
// before publishing use lists or a verified-analysis revision, and immediately runs the ordinary optimizer. A
// successful result borrows the input's immutable tables: both the input and output arena must remain alive until
// that optimizer materializes its result. The input is never mutated, including shared/overlapping extra slices.
//
// A 64-byte opcode mask finds conditional terminators, with an exact scalar twin and no padded-tail reads. Only
// reachable, single-entry arms ending at the same two-predecessor join qualify. Each arm has at most four total,
// cheap scalar operations; the combined cost, including the selects, is at most eight. These are conservative
// policy bounds, not measured latencies. There is currently no branch probability in IR_CBR to consult or preserve.
//
// Move the arm values, in their original dependency order, to the join, replace every join phi with a select, and
// make the head branch directly to the join. The old arm blocks retain only their branches and become unreachable;
// SCCP/DCE/CFG cleanup removes them. A join may itself head another region (sequential clamps), but arms cannot
// overlap any accepted region. Block identities stay stable and rows are permuted, never duplicated. No load,
// division, floating operation, call, allocation, lifetime marker, fence or other effect is speculated. Shifts need
// an immediate in-range count. Address-taken blocks and general nested decision trees are deliberately left alone.

#ifndef BUSTER_IR_IF_CONVERT_C
#define BUSTER_IR_IF_CONVERT_C

#define BUSTER_IR_IF_CONVERT_BATCH 64u
#define BUSTER_IR_IF_CONVERT_ARM_LIMIT 4u
#define BUSTER_IR_IF_CONVERT_PHI_LIMIT 4u
#define BUSTER_IR_IF_CONVERT_COST_LIMIT 8u

typedef struct IrIfConvertResult IrIfConvertResult;
struct IrIfConvertResult
{
    IrFunction function;
    uint32_t regions;
    uint32_t selects;
};

typedef struct IrIfConvertRegion IrIfConvertRegion;
struct IrIfConvertRegion
{
    uint32_t head;
    uint32_t join;
    uint32_t true_arm;
    uint32_t false_arm;
};

typedef struct IrIfConvertWork IrIfConvertWork;
struct IrIfConvertWork
{
    const IrFunction* function;
    IrCfg cfg;
    IrIfConvertRegion* regions;
    uint32_t* heads;
    uint32_t* joins;
    uint8_t* arms;
    uint8_t* reachable;
    uint32_t region_count;
    uint32_t select_count;
};

static uint64_t ir_if_convert_candidates(const uint8_t* operations, uint32_t count, bool scalar)
{
    uint64_t candidates = 0;
    if (count != 0 && count <= BUSTER_IR_IF_CONVERT_BATCH)
    {
#if BUSTER_HOST_VECTOR
        if (!scalar)
        {
            const __mmask64 valid = UINT64_MAX >> (BUSTER_IR_IF_CONVERT_BATCH - count);
            const __m512i op = _mm512_maskz_loadu_epi8(valid, operations);
            candidates = valid & _mm512_cmpeq_epi8_mask(op, _mm512_set1_epi8(IR_CBR));
        }
        else
#endif
        {
            for (uint32_t lane = 0; lane < count; lane += 1)
            {
                candidates |= (uint64_t)(operations[lane] == IR_CBR) << lane;
            }
        }
    }
    (void)scalar;
    return candidates;
}

static bool ir_if_convert_scalar_type(uint32_t type)
{
    return (type >= IR_I1 && type <= IR_I64) || type == IR_PTR;
}

// Zero refuses an operation. Every admitted operation is total on the admitted operand types; integer arithmetic
// wraps in IR, without signed-overflow flags. Even an ordinary load can fault and needs a separate proof to move.
static uint32_t ir_if_convert_cost(const IrFunction* f, uint32_t i)
{
    static const uint8_t costs[IR_OP_COUNT] = {
        [IR_CONST] = 1,
        [IR_COPY] = 1,
        [IR_ADD] = 1,
        [IR_SUB] = 1,
        [IR_MUL] = 3,
        [IR_AND] = 1,
        [IR_OR] = 1,
        [IR_XOR] = 1,
        [IR_SHL] = 1,
        [IR_LSHR] = 1,
        [IR_ASHR] = 1,
        [IR_NEG] = 1,
        [IR_NOT] = 1,
        [IR_ICMP] = 1,
        [IR_SELECT] = 2,
        [IR_TRUNC] = 1,
        [IR_ZEXT] = 1,
        [IR_SEXT] = 1,
        [IR_INTTOPTR] = 1,
        [IR_PTRTOINT] = 1,
        [IR_BITCAST] = 1,
    };
    const uint32_t op = f->op[i];
    uint32_t cost = costs[op];
    bool safe = cost != 0 && ir_if_convert_scalar_type(f->type[i]) && f->flags[i] == 0;
    const uint32_t mask = ir_value_mask(f, i);
    safe = safe && ((mask & 1) == 0 || ir_if_convert_scalar_type(f->type[f->a[i]]));
    safe = safe && ((mask & 2) == 0 || ir_if_convert_scalar_type(f->type[f->b[i]]));
    safe = safe && ((mask & 4) == 0 || ir_if_convert_scalar_type(f->type[f->c[i]]));
    if (safe && (op == IR_SHL || op == IR_LSHR || op == IR_ASHR))
    {
        const uint32_t count = f->b[i];
        safe = f->op[count] == IR_CONST;
        if (safe)
        {
            const IrConstant bits = f->constants[f->a[count]];
            safe = bits.high == 0 && bits.low < ir_type_bits[f->type[i]];
        }
    }
    cost = safe ? cost : 0;
    return cost;
}

static bool ir_if_convert_arm(const IrIfConvertWork* work, uint32_t head, uint32_t arm, uint32_t join, uint32_t* cost)
{
    const IrFunction* const f = work->function;
    const IrCfg* const cfg = &work->cfg;
    const uint32_t first = f->block_offsets[arm];
    const uint32_t last = f->block_offsets[(uint64_t)arm + 1] - 1;
    const uint32_t predecessor = cfg->predecessor_offsets[arm];
    bool safe = arm != 0 && arm != head && arm != join && work->arms[arm] == 0 && work->heads[arm] == 0 && work->joins[arm] == 0 &&
                cfg->predecessor_offsets[(uint64_t)arm + 1] - predecessor == 1 && cfg->predecessors[predecessor] == head &&
                f->op[last] == IR_BR && f->a[last] == join && last - first <= BUSTER_IR_IF_CONVERT_ARM_LIMIT;
    uint32_t total = 0;
    for (uint32_t i = first; safe && i < last; i += 1)
    {
        const uint32_t row_cost = ir_if_convert_cost(f, i);
        total += row_cost;
        safe = row_cost != 0;
    }
    if (safe)
    {
        *cost += total;
    }
    return safe;
}

static void ir_if_convert_region(IrIfConvertWork* work, uint32_t branch)
{
    const IrFunction* const f = work->function;
    const IrCfg* const cfg = &work->cfg;
    const uint32_t head = cfg->instruction_blocks[branch];
    const uint32_t yes = f->b[branch];
    const uint32_t no = f->c[branch];
    const uint32_t yes_last = f->block_offsets[(uint64_t)yes + 1] - 1;
    const uint32_t no_last = f->block_offsets[(uint64_t)no + 1] - 1;
    uint32_t join = BUSTER_IR_NONE;
    // A triangle's direct edge takes precedence over a diamond; neither can include a self edge.
    if (f->op[yes_last] == IR_BR && f->a[yes_last] == no)
    {
        join = no;
    }
    else if (f->op[no_last] == IR_BR && f->a[no_last] == yes)
    {
        join = yes;
    }
    else if (f->op[yes_last] == IR_BR && f->op[no_last] == IR_BR && f->a[yes_last] == f->a[no_last])
    {
        join = f->a[yes_last];
    }
    bool safe = yes != no && yes != head && no != head && join != BUSTER_IR_NONE && join != 0 && join != head &&
                work->reachable[head] != 0 && work->arms[head] == 0 && work->heads[head] == 0;
    uint32_t cost = 0;
    uint32_t phis = 0;
    if (safe)
    {
        const uint32_t predecessor = cfg->predecessor_offsets[join];
        const uint32_t true_from = yes == join ? head : yes;
        const uint32_t false_from = no == join ? head : no;
        safe = work->arms[join] == 0 && work->joins[join] == 0 && cfg->predecessor_offsets[(uint64_t)join + 1] - predecessor == 2 &&
                ((cfg->predecessors[predecessor] == true_from && cfg->predecessors[predecessor + 1] == false_from) ||
                 (cfg->predecessors[predecessor] == false_from && cfg->predecessors[predecessor + 1] == true_from));
        safe = safe && (yes == join || ir_if_convert_arm(work, head, yes, join, &cost));
        safe = safe && (no == join || ir_if_convert_arm(work, head, no, join, &cost));
        const uint32_t first = f->block_offsets[join];
        const uint32_t end = f->block_offsets[(uint64_t)join + 1];
        for (uint32_t i = first; safe && i < end && f->op[i] == IR_PHI; i += 1)
        {
            phis += 1;
            cost += 2;
            safe = phis <= BUSTER_IR_IF_CONVERT_PHI_LIMIT && f->b[i] == 2 && ir_if_convert_scalar_type(f->type[i]);
        }
        safe = safe && phis != 0 && cost <= BUSTER_IR_IF_CONVERT_COST_LIMIT;
    }
    if (safe)
    {
        const uint32_t index = work->region_count;
        work->regions[index] = (IrIfConvertRegion){
            .head = head,
            .join = join,
            .true_arm = yes == join ? BUSTER_IR_NONE : yes,
            .false_arm = no == join ? BUSTER_IR_NONE : no,
        };
        work->heads[head] = index + 1;
        work->joins[join] = index + 1;
        if (yes != join)
        {
            work->arms[yes] = 1;
        }
        if (no != join)
        {
            work->arms[no] = 1;
        }
        work->region_count = index + 1;
        work->select_count += phis;
    }
}

static IrError ir_if_convert_rewrite(const IrIfConvertWork* work, Arena* output, Arena* scratch, IrFunction* result)
{
    const IrFunction* const f = work->function;
    const uint64_t mark = output->length;
    const uint32_t count = f->instruction_count;
    const uint32_t blocks = f->block_count;
    bool ok = true;
    uint32_t* order = ir_allocate(scratch, count, 4, &ok);
    uint32_t* values = ir_allocate(scratch, count, 4, &ok);
    IrFunction view = *f;
    view.op = ir_allocate(output, count, 1, &ok);
    view.type = ir_allocate(output, count, 1, &ok);
    view.flags = ir_allocate(output, count, 1, &ok);
    view.alignment_log2 = ir_allocate(output, count, 1, &ok);
    view.a = ir_allocate(output, count, 4, &ok);
    view.b = ir_allocate(output, count, 4, &ok);
    view.c = ir_allocate(output, count, 4, &ok);
    view.block_offsets = ir_allocate(output, (uint64_t)blocks + 1, 4, &ok);
    uint64_t extras = f->extra_count;
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (f->op[i] == IR_PHI && work->joins[work->cfg.instruction_blocks[i]] == 0)
        {
            extras += (uint64_t)f->b[i] * 2;
        }
        else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
        {
            extras += f->calls[f->b[i]].argument_count;
        }
        else if (ir_ternary_instruction(f, i))
        {
            extras += 1;
        }
    }
    IrError error = !ok ? IR_ERROR_MEMORY : extras > UINT32_MAX ? IR_ERROR_LIMIT : IR_OK;
    if (error == IR_OK)
    {
        view.extra = ir_allocate(output, extras, 4, &ok);
        view.calls = ir_allocate(output, f->call_count, sizeof(IrCall), &ok);
        view.extra_count = (uint32_t)extras;
        error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (error == IR_OK && !ir_debug_create(output, &view, f->debug))
    {
        error = IR_ERROR_MEMORY;
    }
    if (error == IR_OK)
    {
        // Never remap extra in place: a phi's values, a call's arguments and a switch's block/constant indices may
        // overlap. Keep non-value slices unchanged and append a private value slice for each surviving occurrence.
        if (f->extra_count != 0)
        {
            memcpy(view.extra, f->extra, (uint64_t)f->extra_count * 4);
        }
        if (f->call_count != 0)
        {
            memcpy(view.calls, f->calls, (uint64_t)f->call_count * sizeof(IrCall));
        }
        uint32_t next = 0;
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            view.block_offsets[block] = next;
            if (work->joins[block] != 0)
            {
                const IrIfConvertRegion region = work->regions[work->joins[block] - 1];
                const uint32_t arms[] = { region.true_arm, region.false_arm };
                for (uint32_t a = 0; a < 2; a += 1)
                {
                    if (arms[a] != BUSTER_IR_NONE)
                    {
                        const uint32_t last = f->block_offsets[(uint64_t)arms[a] + 1] - 1;
                        for (uint32_t i = f->block_offsets[arms[a]]; i < last; i += 1)
                        {
                            order[next++] = i;
                        }
                    }
                }
            }
            const uint32_t end = f->block_offsets[(uint64_t)block + 1];
            const uint32_t first = work->arms[block] != 0 ? end - 1 : f->block_offsets[block];
            for (uint32_t i = first; i < end; i += 1)
            {
                order[next++] = i;
            }
        }
        view.block_offsets[blocks] = next;
        for (uint32_t i = 0; i < count; i += 1)
        {
            values[order[i]] = i;
        }
        ir_debug_remap(f, &view, values, NULL, NULL);
        uint32_t extra = f->extra_count;
        for (uint32_t out = 0; out < count; out += 1)
        {
            const uint32_t i = order[out];
            const uint32_t block = work->cfg.instruction_blocks[i];
            if (view.debug != NULL && work->arms[block] != 0)
            {
                // These former arm instructions now execute speculatively in the join. Their lexical scope cannot
                // be advertised unconditionally just because their expression's original token remains known.
                view.debug->origins[out] = BUSTER_IR_NONE;
            }
            const uint32_t mask = ir_value_mask(f, i);
            view.op[out] = f->op[i];
            view.type[out] = f->type[i];
            view.flags[out] = f->flags[i];
            view.alignment_log2[out] = f->alignment_log2[i];
            view.a[out] = (mask & 1) != 0 ? values[f->a[i]] : f->a[i];
            view.b[out] = (mask & 2) != 0 ? values[f->b[i]] : f->b[i];
            view.c[out] = (mask & 4) != 0 ? values[f->c[i]] : f->c[i];
            if (f->op[i] == IR_CBR && work->heads[block] != 0)
            {
                view.op[out] = IR_BR;
                view.a[out] = work->regions[work->heads[block] - 1].join;
                view.b[out] = BUSTER_IR_NONE;
                view.c[out] = BUSTER_IR_NONE;
            }
            else if (f->op[i] == IR_PHI && work->joins[block] != 0)
            {
                const IrIfConvertRegion region = work->regions[work->joins[block] - 1];
                const uint32_t true_from = region.true_arm == BUSTER_IR_NONE ? region.head : region.true_arm;
                const uint32_t true_at = f->extra[f->a[i]] == true_from ? 0 : 2;
                view.op[out] = IR_SELECT;
                view.a[out] = values[f->a[f->block_offsets[(uint64_t)region.head + 1] - 1]];
                view.b[out] = values[f->extra[f->a[i] + true_at + 1]];
                view.c[out] = values[f->extra[f->a[i] + (2 - true_at) + 1]];
            }
            else if (f->op[i] == IR_PHI)
            {
                view.a[out] = extra;
                for (uint32_t p = 0; p < f->b[i]; p += 1)
                {
                    view.extra[extra++] = f->extra[f->a[i] + p * 2];
                    view.extra[extra++] = values[f->extra[f->a[i] + p * 2 + 1]];
                }
            }
            else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
            {
                const IrCall call = f->calls[f->b[i]];
                view.calls[f->b[i]].argument_first = extra;
                for (uint32_t a = 0; a < call.argument_count; a += 1)
                {
                    view.extra[extra++] = values[f->extra[call.argument_first + a]];
                }
            }
            else if (ir_ternary_instruction(f, i))
            {
                // A ternary intrinsic's third operand (#1840), in its own appended slot.
                view.c[out] = ir_intrinsic_ternary_c(f->c[i], extra);
                view.extra[extra++] = values[ir_intrinsic_third(f, i)];
            }
        }
        view.arena_bytes = output->length - mark;
        *result = view;
    }
    return error;
}

static IrCheck ir_if_convert(const IrFunction* function, Arena* output, Arena* scratch, bool scalar, IrIfConvertResult* result)
{
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    IrCheck check = ir_check(output != scratch ? IR_OK : IR_ERROR_STRUCTURE, BUSTER_IR_NONE, 0);
    IrIfConvertWork work = { .function = function };
    const uint32_t count = function->instruction_count;
    const uint8_t* const operations = function->op;
    uint64_t first_candidate = 0;
    bool candidate = false;
    if (check.error == IR_OK && function->label_count == 0)
    {
        // The usual straight-line function pays only a bounded byte scan, with no CFG, queue or output allocation.
        while (first_candidate < count && !candidate)
        {
            const uint32_t lanes = count - first_candidate < BUSTER_IR_IF_CONVERT_BATCH ? count - first_candidate : BUSTER_IR_IF_CONVERT_BATCH;
            candidate = ir_if_convert_candidates(operations + first_candidate, lanes, scalar) != 0;
            first_candidate += candidate ? 0 : BUSTER_IR_IF_CONVERT_BATCH;
        }
    }
    // Indirect branch labels name semantic entries, including otherwise unreachable blocks. Until the pass has a
    // label-aware region proof, leave the whole such function intact. Empty functions also need no preparation.
    if (check.error == IR_OK && candidate)
    {
        check.error = ir_cfg_validated(function, scratch, scratch, &work.cfg);
        bool ok = check.error == IR_OK;
        const uint32_t blocks = function->block_count;
        work.regions = ir_allocate(scratch, blocks, sizeof(IrIfConvertRegion), &ok);
        work.heads = ir_allocate(scratch, blocks, 4, &ok);
        work.joins = ir_allocate(scratch, blocks, 4, &ok);
        work.arms = ir_allocate(scratch, blocks, 1, &ok);
        work.reachable = ir_allocate(scratch, blocks, 1, &ok);
        uint32_t* queue = ir_allocate(scratch, blocks, 4, &ok);
        if (check.error == IR_OK && !ok)
        {
            check.error = IR_ERROR_MEMORY;
        }
        if (check.error == IR_OK)
        {
            uint32_t queued = 1;
            queue[0] = 0;
            work.reachable[0] = 1;
            for (uint32_t at = 0; at < queued; at += 1)
            {
                const uint32_t block = queue[at];
                for (uint32_t e = work.cfg.successor_offsets[block]; e < work.cfg.successor_offsets[(uint64_t)block + 1]; e += 1)
                {
                    const uint32_t target = work.cfg.successors[e];
                    if (work.reachable[target] == 0)
                    {
                        work.reachable[target] = 1;
                        queue[queued++] = target;
                    }
                }
            }
            for (uint64_t first = first_candidate; first < count; first += BUSTER_IR_IF_CONVERT_BATCH)
            {
                const uint32_t lanes = count - first < BUSTER_IR_IF_CONVERT_BATCH ? count - first : BUSTER_IR_IF_CONVERT_BATCH;
                uint64_t candidates = ir_if_convert_candidates(operations + first, lanes, scalar);
                while (candidates != 0)
                {
                    ir_if_convert_region(&work, (uint32_t)first + (uint32_t)__builtin_ctzll(candidates));
                    candidates &= candidates - 1;
                }
            }
        }
    }
    IrIfConvertResult converted = {
        .function = *function,
        .regions = work.region_count,
        .selects = work.select_count,
    };
    if (check.error == IR_OK && work.region_count != 0)
    {
        check.error = ir_if_convert_rewrite(&work, output, scratch, &converted.function);
    }
    if (check.error == IR_OK)
    {
        *result = converted;
    }
    else
    {
        arena_pop_to(output, om);
    }
    arena_pop_to(scratch, sm);
    return check;
}

#endif
