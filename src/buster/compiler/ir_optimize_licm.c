// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original conservative loop-invariant motion (#139). Include after ir_optimize.c. Only total, side-effect-free,
// undef-independent operations move into existing dedicated preheaders of reducible natural loops.
// Vector readiness sweeps have an independent use-worklist twin and always complete before rewriting.

typedef struct IrLicmOptions IrLicmOptions;
struct IrLicmOptions
{
    uint32_t sweep_limit;
    bool scalar;
};

typedef struct IrLicmResult IrLicmResult;
struct IrLicmResult
{
    IrFunction function;
    uint32_t moved;
    uint32_t eligible_loops;
    uint32_t skipped_loops;
    uint64_t sweeps;
    uint64_t visits;
    uint64_t scratch_bytes;
    bool fallback;
};

typedef struct IrLicmWork IrLicmWork;
struct IrLicmWork
{
    IrOptimizeWork ir;
    Dominators loops;
    uint32_t* destination;
    uint32_t* available;
    uint8_t* safe;
    uint8_t* tainted;
    uint8_t* candidate;
    IrLicmResult statistics;
};

static __attribute__((unused)) IrLicmOptions ir_licm_options(void)
{
    const IrLicmOptions options = { .sweep_limit = 64 };
    return options;
}

static void ir_licm_classify(IrLicmWork* work)
{
    const IrFunction* f = work->ir.function;
    static const uint8_t safe[64] = {
        [IR_CONST] = 1, [IR_PARAM] = 1, [IR_COPY] = 1,
        [IR_ADD] = 1, [IR_SUB] = 1, [IR_MUL] = 1,
        [IR_AND] = 1, [IR_OR] = 1, [IR_XOR] = 1, [IR_NEG] = 1, [IR_NOT] = 1,
        [IR_FNEG] = 1, [IR_ICMP] = 1, [IR_SELECT] = 1,
        [IR_TRUNC] = 1, [IR_ZEXT] = 1, [IR_SEXT] = 1,
        [IR_INTTOPTR] = 1, [IR_PTRTOINT] = 1, [IR_BITCAST] = 1,
        [IR_STACK_ADDR] = 1, [IR_SYMBOL_ADDR] = 1, [IR_ADDRESS] = 1,
    };
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        work->destination[i] = work->ir.cfg.instruction_blocks[i];
        if (f->op[i] == IR_UNDEF)
        {
            work->tainted[i] = 1;
            ir_optimize_enqueue(&work->ir, i);
        }
    }
    while (work->ir.queued_count != 0)
    {
        const uint32_t value = ir_optimize_dequeue(&work->ir);
        for (uint32_t use = work->ir.uses.offsets[value]; use < work->ir.uses.offsets[(uint64_t)value + 1]; use += 1)
        {
            const uint32_t user = work->ir.uses.uses[use].instruction;
            if (work->tainted[user] == 0)
            {
                work->tainted[user] = 1;
                ir_optimize_enqueue(&work->ir, user);
            }
        }
    }
    for (uint64_t first = 0; first < f->instruction_count; first += 16)
    {
        const uint32_t count = f->instruction_count - first < 16 ? f->instruction_count - first : 16;
        const __mmask16 valid = (UINT32_C(1) << count) - 1;
        const __m128i operations = _mm_maskz_loadu_epi8(valid, f->op + first);
        const __m128i kinds = _mm512_castsi512_si128(_mm512_permutexvar_epi8(_mm512_zextsi128_si512(operations), _mm512_loadu_si512(safe)));
        const __m128i tainted = _mm_maskz_loadu_epi8(valid, work->tainted + first);
        _mm_mask_storeu_epi8(work->safe + first, valid, _mm_andnot_si128(tainted, kinds));
    }
}

static uint32_t ir_licm_preheader(const IrLicmWork* work, uint32_t loop)
{
    const IrCfg* cfg = &work->ir.cfg;
    const uint32_t header = work->loops.loop_header[loop];
    uint32_t preheader = BUSTER_IR_NONE;
    uint32_t entries = 0;
    for (uint32_t edge = cfg->predecessor_offsets[header]; edge < cfg->predecessor_offsets[(uint64_t)header + 1]; edge += 1)
    {
        const uint32_t predecessor = cfg->predecessors[edge];
        if (work->loops.rpo_index[predecessor] != BUSTER_DOMINATOR_NONE && !dominators_loop_contains(&work->loops, loop, predecessor))
        {
            entries += 1;
            preheader = predecessor;
        }
    }
    if (work->loops.loop_irreducible[loop] != 0 || entries != 1 || cfg->successor_offsets[(uint64_t)preheader + 1] - cfg->successor_offsets[preheader] != 1)
    {
        preheader = BUSTER_IR_NONE;
    }
    return preheader;
}

static void ir_licm_worklist(IrLicmWork* work)
{
    const IrFunction* f = work->ir.function;
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        if (work->candidate[i] != 0 && work->available[i] == 0)
        {
            ir_optimize_enqueue(&work->ir, i);
        }
    }
    while (work->ir.queued_count != 0)
    {
        const uint32_t i = ir_optimize_dequeue(&work->ir);
        const uint32_t mask = ir_value_mask(f, i);
        const bool ready = ((mask & 1) == 0 || work->available[f->a[i]] != 0) && ((mask & 2) == 0 || work->available[f->b[i]] != 0) && ((mask & 4) == 0 || work->available[f->c[i]] != 0);
        work->statistics.visits += 1;
        if (ready && work->available[i] == 0)
        {
            work->available[i] = 1;
            for (uint32_t use = work->ir.uses.offsets[i]; use < work->ir.uses.offsets[(uint64_t)i + 1]; use += 1)
            {
                const uint32_t user = work->ir.uses.uses[use].instruction;
                if (work->candidate[user] != 0 && work->available[user] == 0)
                {
                    ir_optimize_enqueue(&work->ir, user);
                }
            }
        }
    }
}

static uint32_t ir_licm_sweep(IrLicmWork* work)
{
    const IrFunction* f = work->ir.function;
    uint32_t progress = 0;
    for (uint64_t first = 0; first < f->instruction_count; first += 16)
    {
        const uint32_t count = f->instruction_count - first < 16 ? f->instruction_count - first : 16;
        const __mmask16 valid = (UINT32_C(1) << count) - 1;
        const __m128i candidates = _mm_maskz_loadu_epi8(valid, work->candidate + first);
        const __m512i available = _mm512_maskz_loadu_epi32(valid, work->available + first);
        __mmask16 ready = valid & _mm_cmpneq_epi8_mask(candidates, _mm_setzero_si128()) & _mm512_cmpeq_epi32_mask(available, _mm512_setzero_si512());
        __mmask16 operands[3] = { 0, 0, 0 };
        for (uint32_t lane = 0; lane < count; lane += 1)
        {
            const uint32_t mask = ir_value_mask(f, first + lane);
            for (uint32_t operand = 0; operand < 3; operand += 1)
            {
                operands[operand] |= ((mask >> operand) & 1) << lane;
            }
        }
        const uint32_t* columns[] = { f->a, f->b, f->c };
        for (uint32_t operand = 0; operand < 3; operand += 1)
        {
            const __mmask16 active = ready & operands[operand];
            const __m512i indices = _mm512_maskz_loadu_epi32(active, columns[operand] + first);
            const __m512i defined = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, indices, work->available, 4);
            ready &= ~active | _mm512_cmpneq_epi32_mask(defined, _mm512_setzero_si512());
        }
        _mm512_mask_storeu_epi32(work->available + first, ready, _mm512_set1_epi32(1));
        work->statistics.visits += count;
        progress += (uint32_t)__builtin_popcount((uint32_t)ready);
    }
    work->statistics.sweeps += 1;
    return progress;
}

// Readiness only changes from unavailable to available. Preserve the sweep's discoveries when switching to
// the exact worklist; both reach the same least fixed point, before the separate motion-order pass.
// Continue whole-function sweeps only while at least one value per 16 visited instructions becomes available.
// This bounds productive sweeps by the vector width, rather than paying the whole sweep limit for a thin chain.
static void ir_licm_complete(IrLicmWork* work, IrLicmOptions options)
{
    const uint32_t count = work->ir.function->instruction_count;
    // AVX-512 dword gather indices are signed. Larger value spaces use the exact worklist path.
    if (options.scalar || count > INT32_MAX)
    {
        work->statistics.fallback = work->statistics.fallback || !options.scalar;
        ir_licm_worklist(work);
    }
    else
    {
        bool changed = true;
        bool productive = true;
        uint32_t sweeps = 0;
        while (changed && productive && sweeps < options.sweep_limit)
        {
            const uint32_t progress = ir_licm_sweep(work);
            changed = progress != 0;
            productive = (uint64_t)progress * 16 >= count;
            sweeps += 1;
        }
        if (changed)
        {
            work->statistics.fallback = true;
            ir_licm_worklist(work);
        }
    }
}

static IrError ir_licm_analyze(IrLicmWork* work, Arena* scratch, IrLicmOptions options)
{
    bool ok = true;
    uint32_t* order = ir_allocate(scratch, work->loops.loop_count, sizeof(uint32_t), &ok);
    for (uint32_t loop = 0; ok && loop < work->loops.loop_count; loop += 1)
    {
        uint32_t at = loop;
        while (at != 0 && work->loops.loop_size[order[at - 1]] > work->loops.loop_size[loop])
        {
            order[at] = order[at - 1];
            at -= 1;
        }
        order[at] = loop;
    }
    const IrFunction* f = work->ir.function;
    for (uint32_t at = 0; ok && at < work->loops.loop_count; at += 1)
    {
        const uint32_t loop = order[at];
        const uint32_t preheader = ir_licm_preheader(work, loop);
        if (preheader == BUSTER_IR_NONE)
        {
            work->statistics.skipped_loops += 1;
        }
        else
        {
            work->statistics.eligible_loops += 1;
            for (uint32_t i = 0; i < f->instruction_count; i += 1)
            {
                const bool inside = dominators_loop_contains(&work->loops, loop, work->destination[i]);
                work->candidate[i] = inside && work->safe[i] != 0;
                work->available[i] = !inside && dominators_dominates(&work->loops, work->destination[i], preheader);
            }
            ir_licm_complete(work, options);
            for (uint32_t i = 0; i < f->instruction_count; i += 1)
            {
                if (work->candidate[i] != 0 && work->available[i] != 0)
                {
                    work->destination[i] = preheader;
                }
            }
        }
    }
    const IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    return error;
}

// A private view keeps immutable metadata borrowed from the input while rebuilding every value-bearing slice.
// Shared compaction subsequently materializes every final column in the caller's output arena.
static IrError ir_licm_reorder(IrLicmWork* work, Arena* scratch, IrFunction* result)
{
    const IrFunction* f = work->ir.function;
    bool ok = true;
    uint32_t* offsets = ir_allocate(scratch, (uint64_t)f->block_count + 1, sizeof(uint32_t), &ok);
    uint32_t* cursors = ir_allocate(scratch, f->block_count, sizeof(uint32_t), &ok);
    uint32_t* movers = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
    uint32_t* order = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
    uint32_t* values = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
    IrFunction view = *f;
    view.op = ir_allocate(scratch, f->instruction_count, 1, &ok);
    view.type = ir_allocate(scratch, f->instruction_count, 1, &ok);
    view.flags = ir_allocate(scratch, f->instruction_count, 1, &ok);
    view.alignment_log2 = ir_allocate(scratch, f->instruction_count, 1, &ok);
    view.a = ir_allocate(scratch, f->instruction_count, 4, &ok);
    view.b = ir_allocate(scratch, f->instruction_count, 4, &ok);
    view.c = ir_allocate(scratch, f->instruction_count, 4, &ok);
    view.block_offsets = ir_allocate(scratch, (uint64_t)f->block_count + 1, 4, &ok);
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    uint64_t extras = 0;
    uint64_t calls = 0;
    for (uint32_t i = 0; error == IR_OK && i < f->instruction_count; i += 1)
    {
        if (work->destination[i] != work->ir.cfg.instruction_blocks[i])
        {
            offsets[(uint64_t)work->destination[i] + 1] += 1;
            work->statistics.moved += 1;
        }
        if (f->op[i] == IR_PHI)
        {
            extras += (uint64_t)f->b[i] * 2;
        }
        else if (f->op[i] == IR_SWITCH)
        {
            extras += (uint64_t)f->c[i] * 2 + 1;
        }
        else if (f->op[i] == IR_BR_INDIRECT && f->b[i] != BUSTER_IR_NONE)
        {
            extras += f->c[i];
        }
        else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
        {
            extras += f->calls[f->b[i]].argument_count;
            calls += 1;
        }
    }
    if (error == IR_OK && (extras > UINT32_MAX || calls > UINT32_MAX))
    {
        error = IR_ERROR_LIMIT;
    }
    if (error == IR_OK)
    {
        view.extra = ir_allocate(scratch, extras, 4, &ok);
        view.calls = ir_allocate(scratch, calls, sizeof(IrCall), &ok);
        view.extra_count = extras;
        view.call_count = calls;
        error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (error == IR_OK)
    {
        for (uint32_t b = 0; b < f->block_count; b += 1)
        {
            offsets[(uint64_t)b + 1] += offsets[b];
            cursors[b] = offsets[b];
        }
        // Original RPO and in-block instruction order topologically order all non-phi SSA dependencies.
        for (uint32_t r = 0; r < work->ir.dominators.reachable_count; r += 1)
        {
            const uint32_t block = work->ir.dominators.rpo[r];
            for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
            {
                if (work->destination[i] != block)
                {
                    movers[cursors[work->destination[i]]++] = i;
                }
            }
        }
        uint32_t count = 0;
        for (uint32_t b = 0; b < f->block_count; b += 1)
        {
            view.block_offsets[b] = count;
            const uint32_t end = f->block_offsets[(uint64_t)b + 1];
            for (uint32_t i = f->block_offsets[b]; i + 1 < end; i += 1)
            {
                if (work->destination[i] == b)
                {
                    order[count++] = i;
                }
            }
            for (uint32_t at = offsets[b]; at < offsets[(uint64_t)b + 1]; at += 1)
            {
                order[count++] = movers[at];
            }
            order[count++] = end - 1;
        }
        view.block_offsets[f->block_count] = count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            values[order[i]] = i;
        }
        uint32_t extra = 0;
        uint32_t call = 0;
        for (uint32_t out = 0; out < count; out += 1)
        {
            const uint32_t i = order[out];
            const uint32_t mask = ir_value_mask(f, i);
            view.op[out] = f->op[i];
            view.type[out] = f->type[i];
            view.flags[out] = f->flags[i];
            view.alignment_log2[out] = f->alignment_log2[i];
            view.a[out] = (mask & 1) != 0 ? values[f->a[i]] : f->a[i];
            view.b[out] = (mask & 2) != 0 ? values[f->b[i]] : f->b[i];
            view.c[out] = (mask & 4) != 0 ? values[f->c[i]] : f->c[i];
            if (f->op[i] == IR_PHI)
            {
                view.a[out] = extra;
                for (uint32_t incoming = 0; incoming < f->b[i]; incoming += 1)
                {
                    view.extra[extra++] = f->extra[f->a[i] + incoming * 2];
                    view.extra[extra++] = values[f->extra[f->a[i] + incoming * 2 + 1]];
                }
            }
            else if (f->op[i] == IR_SWITCH)
            {
                view.b[out] = extra;
                const uint64_t words = (uint64_t)f->c[i] * 2 + 1;
                memcpy(view.extra + extra, f->extra + f->b[i], words * 4);
                extra += words;
            }
            else if (f->op[i] == IR_BR_INDIRECT && f->b[i] != BUSTER_IR_NONE)
            {
                view.b[out] = extra;
                memcpy(view.extra + extra, f->extra + f->b[i], (uint64_t)f->c[i] * 4);
                extra += f->c[i];
            }
            else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
            {
                view.b[out] = call;
                view.calls[call] = f->calls[f->b[i]];
                view.calls[call].argument_first = extra;
                for (uint32_t argument = 0; argument < view.calls[call].argument_count; argument += 1)
                {
                    view.extra[extra++] = values[f->extra[f->calls[f->b[i]].argument_first + argument]];
                }
                call += 1;
            }
        }
        *result = view;
    }
    return error;
}

static __attribute__((unused)) IrCheck ir_optimize_licm(const IrFunction* function, Arena* output, Arena* scratch, IrLicmOptions options, IrLicmResult* result)
{
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    IrLicmWork work = { 0 };
    IrCheck check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, 0);
    if (output != scratch)
    {
        check = ir_optimize_prepare(&work.ir, function, output, scratch, BUSTER_SCALAR_PATH(options.scalar), false);
    }
    if (check.error == IR_OK)
    {
        const DominatorCfg cfg = {
            .block_count = work.ir.cfg.block_count,
            .edge_count = work.ir.cfg.edge_count,
            .entry = function->block_count == 0 ? BUSTER_DOMINATOR_NONE : 0,
            .successor_offsets = work.ir.cfg.successor_offsets,
            .successors = work.ir.cfg.successors,
            .predecessor_offsets = work.ir.cfg.predecessor_offsets,
            .predecessors = work.ir.cfg.predecessors,
        };
        DominatorOptions dom = dominators_options();
        dom.outputs = BUSTER_DOMINATOR_LOOPS;
        dom.path = BUSTER_SCALAR_PATH(options.scalar) ? DOMINATOR_SCALAR : DOMINATOR_AUTO;
        const DominatorStatus status = dominators_analyze(scratch, output, &cfg, dom, &work.loops);
        check.error = status == DOMINATOR_OK ? IR_OK : status == DOMINATOR_NO_MEMORY ? IR_ERROR_MEMORY : IR_ERROR_DOMINANCE;
    }
    if (check.error == IR_OK)
    {
        bool ok = true;
        const uint64_t n = function->instruction_count;
        work.destination = ir_allocate(scratch, n, 4, &ok);
        work.available = ir_allocate(scratch, n, 4, &ok);
        work.safe = ir_allocate(scratch, n, 1, &ok);
        work.tainted = ir_allocate(scratch, n, 1, &ok);
        work.candidate = ir_allocate(scratch, n, 1, &ok);
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (check.error == IR_OK)
    {
        ir_licm_classify(&work);
        check.error = ir_licm_analyze(&work, scratch, options);
    }
    IrFunction view = { 0 };
    if (check.error == IR_OK)
    {
        check.error = ir_licm_reorder(&work, scratch, &view);
    }
    if (check.error == IR_OK)
    {
        work.ir.function = &view;
        check.error = ir_build_cfg(&view, scratch, output, &work.ir.cfg);
    }
    if (check.error == IR_OK)
    {
        for (uint32_t b = 0; b < view.block_count; b += 1)
        {
            work.ir.blocks[b] = work.ir.dominators.rpo_index[b] != BUSTER_DOMINATOR_NONE;
            for (uint32_t edge = work.ir.cfg.successor_offsets[b]; edge < work.ir.cfg.successor_offsets[(uint64_t)b + 1]; edge += 1)
            {
                work.ir.edges[edge] = work.ir.blocks[b];
            }
        }
        for (uint32_t i = 0; i < view.instruction_count; i += 1)
        {
            work.ir.states[i] = IROPT_OVERDEFINED;
            work.ir.live[i] = work.ir.blocks[work.ir.cfg.instruction_blocks[i]];
        }
        IrOptimizeOptions rewrite = ir_optimize_options();
        rewrite.passes = 0;
        rewrite.scalar = BUSTER_SCALAR_PATH(options.scalar);
        check.error = ir_optimize_rewrite(&work.ir, output, scratch, rewrite, &work.statistics.function);
    }
    if (check.error == IR_OK)
    {
        check = ir_optimize_verify_result(&work.statistics.function, output, scratch, BUSTER_SCALAR_PATH(options.scalar));
    }
    if (check.error == IR_OK)
    {
        work.statistics.scratch_bytes = scratch->length - sm;
        *result = work.statistics;
    }
    else
    {
        arena_pop_to(output, om);
    }
    arena_pop_to(scratch, sm);
    return check;
}
