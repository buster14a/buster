// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original SSA value numbering (#139), included after ir_optimize.c. The vector path refines congruence partitions;
// the independent scalar path hashes available expressions while walking the dominator tree with scope rollback.
// Hashes never establish equality: full keys and predecessor-ordered phi operands are compared on every collision.
// Loads, calls, environment-sensitive floating operations and all undef-tainted values are excluded.

#define BUSTER_IR_GVN_FIELDS 8
#define BUSTER_IR_GVN_HASH_MULTIPLIER UINT64_C(1099511628211)

typedef struct IrGvnOptions IrGvnOptions;
struct IrGvnOptions
{
    uint32_t sweep_limit;
    bool scalar;
};

typedef struct IrGvnResult IrGvnResult;
struct IrGvnResult
{
    IrFunction function;
    uint32_t removed;
    uint32_t classes;
    uint64_t rounds;
    uint64_t scratch_bytes;
    bool fallback;
};

typedef struct IrGvnFrame IrGvnFrame;
struct IrGvnFrame
{
    uint32_t block;
    uint32_t child;
    uint32_t mark;
    bool entered;
};

typedef struct IrGvnWork IrGvnWork;
struct IrGvnWork
{
    IrOptimizeWork ir;
    uint32_t* alias;
    uint32_t* classes;
    uint32_t* next_classes;
    uint32_t* order;
    uint32_t* temporary_order;
    uint64_t* fields;
    uint64_t* hashes;
    uint8_t* pure;
    uint8_t* tainted;
    IrGvnResult statistics;
};

static __attribute__((unused)) IrGvnOptions ir_gvn_options(void)
{
    const IrGvnOptions options = { .sweep_limit = 64 };
    return options;
}

static uint32_t ir_gvn_resolve(uint32_t* aliases, uint32_t value)
{
    uint32_t result = value;
    while (aliases[result] != result)
    {
        result = aliases[result];
    }
    while (aliases[value] != value)
    {
        const uint32_t next = aliases[value];
        aliases[value] = result;
        value = next;
    }
    return result;
}

static void ir_gvn_classify(IrGvnWork* work)
{
    const IrFunction* f = work->ir.function;
    static const uint8_t pure[64] = {
        [IR_CONST] = 1,
        [IR_PARAM] = 1,
        [IR_ADD] = 1,
        [IR_SUB] = 1,
        [IR_MUL] = 1,
        [IR_SDIV] = 1,
        [IR_UDIV] = 1,
        [IR_SREM] = 1,
        [IR_UREM] = 1,
        [IR_AND] = 1,
        [IR_OR] = 1,
        [IR_XOR] = 1,
        [IR_SHL] = 1,
        [IR_LSHR] = 1,
        [IR_ASHR] = 1,
        [IR_NEG] = 1,
        [IR_NOT] = 1,
        [IR_FNEG] = 1,
        [IR_ICMP] = 1,
        [IR_SELECT] = 1,
        [IR_TRUNC] = 1,
        [IR_ZEXT] = 1,
        [IR_SEXT] = 1,
        [IR_INTTOPTR] = 1,
        [IR_PTRTOINT] = 1,
        [IR_BITCAST] = 1,
        [IR_STACK_ADDR] = 1,
        [IR_SYMBOL_ADDR] = 1,
        [IR_ADDRESS] = 1,
        [IR_PHI] = 1,
    };
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        work->alias[i] = i;
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
            const uint32_t instruction = work->ir.uses.uses[use].instruction;
            if (work->tainted[instruction] == 0)
            {
                work->tainted[instruction] = 1;
                ir_optimize_enqueue(&work->ir, instruction);
            }
        }
    }
    for (uint64_t first = 0; first < f->instruction_count; first += BUSTER_IR_OPTIMIZE_BATCH)
    {
        const uint32_t count = f->instruction_count - first < BUSTER_IR_OPTIMIZE_BATCH ? f->instruction_count - first : BUSTER_IR_OPTIMIZE_BATCH;
        const __mmask16 valid = (UINT32_C(1) << count) - 1;
        const __m128i operations = _mm_maskz_loadu_epi8(valid, f->op + first);
        const __m128i kinds = _mm512_castsi512_si128(_mm512_permutexvar_epi8(_mm512_zextsi128_si512(operations), _mm512_loadu_si512(pure)));
        const __m128i tainted = _mm_maskz_loadu_epi8(valid, work->tainted + first);
        _mm_mask_storeu_epi8(work->pure + first, valid, _mm_andnot_si128(tainted, kinds));
    }
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        if (f->op[i] == IR_COPY && work->tainted[i] == 0 && work->ir.dominators.rpo_index[work->ir.cfg.instruction_blocks[i]] != BUSTER_DOMINATOR_NONE)
        {
            work->alias[i] = f->a[i];
        }
    }
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        ir_gvn_resolve(work->alias, i);
    }
}

static uint32_t ir_gvn_phi_class(const IrGvnWork* work, uint32_t instruction, uint32_t predecessor)
{
    const IrFunction* f = work->ir.function;
    uint32_t result = 0;
    for (uint32_t incoming = 0; incoming < f->b[instruction]; incoming += 1)
    {
        const uint32_t at = f->a[instruction] + incoming * 2;
        if (f->extra[at] == predecessor)
        {
            result = work->classes[work->alias[f->extra[at + 1]]];
        }
    }
    return result;
}

static void ir_gvn_key(IrGvnWork* work, uint32_t i, bool initial, bool parent)
{
    const IrFunction* f = work->ir.function;
    const uint64_t n = f->instruction_count;
    const uint32_t op = f->op[i];
    const uint32_t mask = ir_value_mask(f, i);
    work->fields[i] = op | ((uint64_t)f->type[i] << 8) | ((uint64_t)f->flags[i] << 16) | ((uint64_t)f->alignment_log2[i] << 24);
    work->fields[n + i] = parent && !initial ? work->classes[i] : 0;
    work->fields[n * 2 + i] = 0;
    work->fields[n * 3 + i] = 0;
    work->fields[n * 4 + i] = !initial && (mask & 1) != 0 ? work->classes[ir_gvn_resolve(work->alias, f->a[i])] : 0;
    work->fields[n * 5 + i] = !initial && (mask & 2) != 0 ? work->classes[ir_gvn_resolve(work->alias, f->b[i])] : 0;
    work->fields[n * 6 + i] = !initial && (mask & 4) != 0 ? work->classes[ir_gvn_resolve(work->alias, f->c[i])] : 0;
    work->fields[n * 7 + i] = work->pure[i] == 0 ? (uint64_t)i + 1 : 0;
    if (op == IR_CONST)
    {
        work->fields[n * 2 + i] = f->constants[f->a[i]].low;
        work->fields[n * 3 + i] = f->constants[f->a[i]].high;
    }
    else if (op == IR_PARAM || op == IR_STACK_ADDR || op == IR_SYMBOL_ADDR)
    {
        work->fields[n * 2 + i] = f->a[i];
    }
    else if (op == IR_ICMP)
    {
        work->fields[n * 2 + i] = f->c[i];
    }
    else if (op == IR_ADDRESS)
    {
        work->fields[n * 2 + i] = (uint64_t)f->addresses[f->c[i]].offset;
        work->fields[n * 3 + i] = f->addresses[f->c[i]].scale | ((uint64_t)(f->b[i] == BUSTER_IR_NONE) << 8);
    }
    else if (op == IR_PHI)
    {
        work->fields[n * 2 + i] = work->ir.cfg.instruction_blocks[i];
        work->fields[n * 3 + i] = f->b[i];
    }
}

static uint64_t ir_gvn_hash_one(const IrGvnWork* work, uint32_t i, bool initial)
{
    const IrFunction* f = work->ir.function;
    uint64_t hash = UINT64_C(1469598103934665603);
    for (uint32_t field = 0; field < BUSTER_IR_GVN_FIELDS; field += 1)
    {
        hash = (hash ^ work->fields[(uint64_t)field * f->instruction_count + i]) * BUSTER_IR_GVN_HASH_MULTIPLIER;
    }
    if (!initial && f->op[i] == IR_PHI && work->pure[i] != 0)
    {
        const uint32_t block = work->ir.cfg.instruction_blocks[i];
        for (uint32_t edge = work->ir.cfg.predecessor_offsets[block]; edge < work->ir.cfg.predecessor_offsets[(uint64_t)block + 1]; edge += 1)
        {
            hash = (hash ^ ir_gvn_phi_class(work, i, work->ir.cfg.predecessors[edge])) * BUSTER_IR_GVN_HASH_MULTIPLIER;
        }
    }
    return hash;
}

static void ir_gvn_keys(IrGvnWork* work, bool initial, bool scalar)
{
    (void)scalar;

    const uint32_t n = work->ir.function->instruction_count;
    for (uint32_t i = 0; i < n; i += 1)
    {
        ir_gvn_key(work, i, initial, true);
    }
    for (uint64_t first = 0; first < n; first += BUSTER_IR_OPTIMIZE_BATCH)
    {
        const uint32_t count = n - first < BUSTER_IR_OPTIMIZE_BATCH ? n - first : BUSTER_IR_OPTIMIZE_BATCH;
        if (!BUSTER_SCALAR_PATH(scalar))
        {
            for (uint32_t half = 0; half < count; half += 8)
            {
                const uint32_t amount = count - half < 8 ? count - half : 8;
                const __mmask8 valid = (UINT32_C(1) << amount) - 1;
                __m512i hash = _mm512_set1_epi64(UINT64_C(1469598103934665603));
                for (uint32_t field = 0; field < BUSTER_IR_GVN_FIELDS; field += 1)
                {
                    const __m512i key = _mm512_maskz_loadu_epi64(valid, work->fields + (uint64_t)field * n + first + half);
                    hash = _mm512_mullo_epi64(_mm512_xor_si512(hash, key), _mm512_set1_epi64(BUSTER_IR_GVN_HASH_MULTIPLIER));
                }
                _mm512_mask_storeu_epi64(work->hashes + first + half, valid, hash);
            }
        }
        for (uint32_t lane = 0; lane < count; lane += 1)
        {
            const uint32_t i = first + lane;
            if (BUSTER_SCALAR_PATH(scalar) || (!initial && work->ir.function->op[i] == IR_PHI && work->pure[i] != 0))
            {
                work->hashes[i] = ir_gvn_hash_one(work, i, initial);
            }
            work->order[i] = i;
        }
    }
}

static int ir_gvn_compare(const IrGvnWork* work, uint32_t a, uint32_t b, bool initial)
{
    int result = work->hashes[a] < work->hashes[b] ? -1 : work->hashes[a] != work->hashes[b];
    const IrFunction* f = work->ir.function;
    for (uint32_t field = 0; result == 0 && field < BUSTER_IR_GVN_FIELDS; field += 1)
    {
        const uint64_t left = work->fields[(uint64_t)field * f->instruction_count + a];
        const uint64_t right = work->fields[(uint64_t)field * f->instruction_count + b];
        result = left < right ? -1 : left != right;
    }
    if (result == 0 && !initial && f->op[a] == IR_PHI && work->pure[a] != 0)
    {
        const uint32_t block = work->ir.cfg.instruction_blocks[a];
        for (uint32_t edge = work->ir.cfg.predecessor_offsets[block]; result == 0 && edge < work->ir.cfg.predecessor_offsets[(uint64_t)block + 1]; edge += 1)
        {
            const uint32_t predecessor = work->ir.cfg.predecessors[edge];
            const uint32_t left = ir_gvn_phi_class(work, a, predecessor);
            const uint32_t right = ir_gvn_phi_class(work, b, predecessor);
            result = left < right ? -1 : left != right;
        }
    }
    return result;
}

static uint32_t ir_gvn_partition(IrGvnWork* work, bool initial, bool scalar)
{
    (void)scalar;

    ir_gvn_keys(work, initial, BUSTER_SCALAR_PATH(scalar));
    const uint64_t n = work->ir.function->instruction_count;
    for (uint64_t width = 1; width < n; width *= 2)
    {
        for (uint64_t first = 0; first < n; first += width * 2)
        {
            const uint64_t middle = first + width < n ? first + width : n;
            const uint64_t end = middle + width < n ? middle + width : n;
            uint64_t left = first;
            uint64_t right = middle;
            for (uint64_t out = first; out < end; out += 1)
            {
                const bool take_left = left < middle && (right == end || ir_gvn_compare(work, work->order[left], work->order[right], initial) <= 0);
                work->temporary_order[out] = take_left ? work->order[left++] : work->order[right++];
            }
        }
        uint32_t* swap = work->order;
        work->order = work->temporary_order;
        work->temporary_order = swap;
    }
    uint32_t classes = 0;
    for (uint32_t i = 0; i < n; i += 1)
    {
        if (i == 0 || ir_gvn_compare(work, work->order[i - 1], work->order[i], initial) != 0)
        {
            classes += 1;
        }
        work->next_classes[work->order[i]] = classes;
    }
    uint32_t* swap = work->classes;
    work->classes = work->next_classes;
    work->next_classes = swap;
    work->statistics.rounds += 1;
    return classes;
}

static IrError ir_gvn_available(IrGvnWork* work, Arena* scratch, bool scalar)
{
    (void)scalar;

    const IrFunction* f = work->ir.function;
    uint64_t buckets = 1;
    while (buckets < (uint64_t)f->instruction_count * 2 + 1)
    {
        buckets *= 2;
    }
    bool ok = true;
    uint32_t* heads = ir_allocate(scratch, buckets, sizeof(uint32_t), &ok);
    uint32_t* previous = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
    uint32_t* inserted = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
    IrGvnFrame* frames = ir_allocate(scratch, f->block_count, sizeof(IrGvnFrame), &ok);
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    if (ok)
    {
        memset(heads, 0xff, buckets * sizeof(uint32_t));
        if (BUSTER_SCALAR_PATH(scalar))
        {
            for (uint32_t i = 0; i < f->instruction_count; i += 1)
            {
                work->classes[i] = i + 1;
                if (f->op[i] == IR_PHI)
                {
                    work->pure[i] = 0;
                }
            }
        }
        uint32_t depth = f->block_count != 0;
        uint32_t insertions = 0;
        if (depth != 0)
        {
            frames[0] = (IrGvnFrame){ .block = 0 };
        }
        while (depth != 0)
        {
            IrGvnFrame* frame = &frames[depth - 1];
            if (!frame->entered)
            {
                frame->entered = true;
                frame->mark = insertions;
                frame->child = work->ir.dominators.tree_offsets[frame->block];
                for (uint32_t i = f->block_offsets[frame->block]; i < f->block_offsets[(uint64_t)frame->block + 1]; i += 1)
                {
                    if (work->pure[i] != 0 && work->alias[i] == i)
                    {
                        if (BUSTER_SCALAR_PATH(scalar))
                        {
                            ir_gvn_key(work, i, false, false);
                            work->hashes[i] = ir_gvn_hash_one(work, i, false);
                        }
                        else
                        {
                            work->hashes[i] = work->classes[i];
                        }
                        const uint64_t bucket = work->hashes[i] & (buckets - 1);
                        uint32_t match = heads[bucket];
                        while (match != BUSTER_IR_NONE && (BUSTER_SCALAR_PATH(scalar) ? ir_gvn_compare(work, i, match, false) != 0 : work->classes[i] != work->classes[match]))
                        {
                            match = previous[match];
                        }
                        if (match != BUSTER_IR_NONE)
                        {
                            work->alias[i] = match;
                            if (BUSTER_SCALAR_PATH(scalar))
                            {
                                work->classes[i] = work->classes[match];
                            }
                        }
                        else
                        {
                            previous[i] = heads[bucket];
                            heads[bucket] = i;
                            inserted[insertions++] = i;
                        }
                    }
                }
            }
            if (frame->child < work->ir.dominators.tree_offsets[(uint64_t)frame->block + 1])
            {
                const uint32_t child = work->ir.dominators.tree_children[frame->child++];
                frames[depth++] = (IrGvnFrame){ .block = child };
            }
            else
            {
                while (insertions > frame->mark)
                {
                    const uint32_t value = inserted[--insertions];
                    heads[work->hashes[value] & (buckets - 1)] = previous[value];
                }
                depth -= 1;
            }
        }
    }
    return error;
}

// Materialize private operand columns and independent variable slices before remapping. Input side slices may alias
// each other, even with different metadata roles; mutating a copied extra array in place would corrupt such inputs.
static IrError ir_gvn_alias_view(IrGvnWork* work, Arena* scratch, IrFunction* result)
{
    const IrFunction* f = work->ir.function;
    uint64_t extras = 0;
    uint64_t calls = 0;
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        if (f->op[i] == IR_PHI)
        {
            extras += (uint64_t)f->b[i] * 2;
        }
        else if (f->op[i] == IR_SWITCH)
        {
            extras += (uint64_t)f->c[i] * 2 + 1;
        }
        else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
        {
            extras += f->calls[f->b[i]].argument_count;
            calls += 1;
        }
    }
    IrError error = extras <= UINT32_MAX && calls <= UINT32_MAX ? IR_OK : IR_ERROR_LIMIT;
    IrFunction view = *f;
    if (error == IR_OK)
    {
        bool ok = true;
        view.a = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
        view.b = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
        view.c = ir_allocate(scratch, f->instruction_count, sizeof(uint32_t), &ok);
        view.extra = ir_allocate(scratch, extras, sizeof(uint32_t), &ok);
        view.calls = ir_allocate(scratch, calls, sizeof(IrCall), &ok);
        view.extra_count = extras;
        view.call_count = calls;
        error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    uint32_t extra = 0;
    uint32_t call = 0;
    for (uint32_t i = 0; error == IR_OK && i < f->instruction_count; i += 1)
    {
        const uint32_t mask = ir_value_mask(f, i);
        view.a[i] = (mask & 1) != 0 ? ir_gvn_resolve(work->alias, f->a[i]) : f->a[i];
        view.b[i] = (mask & 2) != 0 ? ir_gvn_resolve(work->alias, f->b[i]) : f->b[i];
        view.c[i] = (mask & 4) != 0 ? ir_gvn_resolve(work->alias, f->c[i]) : f->c[i];
        if (f->op[i] == IR_PHI)
        {
            view.a[i] = extra;
            for (uint32_t incoming = 0; incoming < f->b[i]; incoming += 1)
            {
                view.extra[extra++] = f->extra[f->a[i] + incoming * 2];
                view.extra[extra++] = ir_gvn_resolve(work->alias, f->extra[f->a[i] + incoming * 2 + 1]);
            }
        }
        else if (f->op[i] == IR_SWITCH)
        {
            view.b[i] = extra;
            const uint64_t count = (uint64_t)f->c[i] * 2 + 1;
            memcpy(view.extra + extra, f->extra + f->b[i], count * sizeof(uint32_t));
            extra += count;
        }
        else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
        {
            view.b[i] = call;
            view.calls[call] = f->calls[f->b[i]];
            view.calls[call].argument_first = extra;
            for (uint32_t argument = 0; argument < view.calls[call].argument_count; argument += 1)
            {
                view.extra[extra++] = ir_gvn_resolve(work->alias, f->extra[f->calls[f->b[i]].argument_first + argument]);
            }
            call += 1;
        }
    }
    if (error == IR_OK)
    {
        *result = view;
    }
    return error;
}

static __attribute__((unused)) IrCheck ir_optimize_gvn(const IrFunction* function, Arena* output, Arena* scratch, IrGvnOptions options, IrGvnResult* result)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    IrGvnWork work = { 0 };
    IrCheck check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, 0);
    if (output != scratch)
    {
        check = ir_optimize_prepare(&work.ir, function, output, scratch, BUSTER_SCALAR_PATH(options.scalar));
    }
    if (check.error == IR_OK)
    {
        bool ok = true;
        const uint64_t n = function->instruction_count;
        work.alias = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        work.classes = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        work.next_classes = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        work.order = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        work.temporary_order = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        work.fields = ir_allocate(scratch, n * BUSTER_IR_GVN_FIELDS, sizeof(uint64_t), &ok);
        work.hashes = ir_allocate(scratch, n, sizeof(uint64_t), &ok);
        work.pure = ir_allocate(scratch, n, sizeof(uint8_t), &ok);
        work.tainted = ir_allocate(scratch, n, sizeof(uint8_t), &ok);
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (check.error == IR_OK)
    {
        ir_gvn_classify(&work);
        if (!BUSTER_SCALAR_PATH(options.scalar))
        {
            uint32_t classes = ir_gvn_partition(&work, true, false);
            bool changed = true;
            while (changed)
            {
                const bool fallback = work.statistics.rounds > options.sweep_limit;
                const uint32_t refined = ir_gvn_partition(&work, false, fallback);
                changed = refined != classes;
                classes = refined;
                work.statistics.fallback = work.statistics.fallback || fallback;
            }
            work.statistics.classes = classes;
        }
        check.error = ir_gvn_available(&work, scratch, BUSTER_SCALAR_PATH(options.scalar));
    }
    IrFunction view = { 0 };
    if (check.error == IR_OK)
    {
        check.error = ir_gvn_alias_view(&work, scratch, &view);
    }
    if (check.error == IR_OK)
    {
        work.ir.function = &view;
        for (uint32_t block = 0; block < function->block_count; block += 1)
        {
            work.ir.blocks[block] = work.ir.dominators.rpo_index[block] != BUSTER_DOMINATOR_NONE;
            for (uint32_t edge = work.ir.cfg.successor_offsets[block]; edge < work.ir.cfg.successor_offsets[(uint64_t)block + 1]; edge += 1)
            {
                work.ir.edges[edge] = work.ir.blocks[block];
            }
        }
        for (uint32_t i = 0; i < function->instruction_count; i += 1)
        {
            work.ir.states[i] = IROPT_OVERDEFINED;
            work.ir.live[i] = ir_gvn_resolve(work.alias, i) == i && work.ir.blocks[work.ir.cfg.instruction_blocks[i]] != 0;
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
        work.statistics.removed = function->instruction_count - work.statistics.function.instruction_count;
        work.statistics.scratch_bytes = scratch->length - scratch_mark;
        *result = work.statistics;
    }
    else
    {
        arena_pop_to(output, output_mark);
    }
    arena_pop_to(scratch, scratch_mark);
    return check;
}
