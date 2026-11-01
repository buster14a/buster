// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original private-slot field splitting (#139), included after ir_optimize.c. The resulting typed scalar slots
// feed the upstream ssa_ir_promote adapter; this module does not implement another SSA constructor.
// Every observed pointer use must be understood before any field of a slot is rewritten.

typedef struct IrSroaOptions IrSroaOptions;
struct IrSroaOptions
{
    bool scalar;
};

typedef struct IrSroaResult IrSroaResult;
struct IrSroaResult
{
    IrFunction function;
    uint32_t split_slots;
    uint32_t fields;
    uint32_t refused_slots;
    uint64_t scratch_bytes;
};

typedef struct IrSroaWork IrSroaWork;
struct IrSroaWork
{
    IrOptimizeWork ir;
    uint32_t* roots;
    uint64_t* offsets;
    uint8_t* refused;
    uint32_t* field_counts;
    uint32_t* field_roots;
    uint64_t* field_offsets;
    uint32_t* field_types;
    uint32_t* field_sizes;
    uint32_t* field_alignments;
    uint32_t* field_slots;
    uint32_t field_count;
    IrSroaResult statistics;
};

typedef struct IrSroaUseBatch IrSroaUseBatch;
struct IrSroaUseBatch
{
    uint32_t count;
    uint32_t op[16];
    uint32_t position[16];
    uint32_t forwarded[16];
    uint32_t flags[16];
    uint32_t root[16];
};

static uint16_t ir_sroa_escape_mask(const IrSroaUseBatch* batch)
{
    uint16_t result = 0;
    if (batch->count <= 16)
    {
        const __mmask16 valid = (UINT32_C(1) << batch->count) - 1;
        const __m512i op = _mm512_maskz_loadu_epi32(valid, batch->op);
        const __m512i position = _mm512_maskz_loadu_epi32(valid, batch->position);
        const __m512i forwarded = _mm512_maskz_loadu_epi32(valid, batch->forwarded);
        const __m512i flags = _mm512_maskz_loadu_epi32(valid, batch->flags);
        const __mmask16 first = valid & _mm512_cmpeq_epi32_mask(position, _mm512_setzero_si512());
        const __mmask16 memory = first & (_mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_LOAD)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_STORE)));
        const __mmask16 forward_op = _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_COPY)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_BITCAST)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_ADDRESS));
        const __mmask16 forward = first & forward_op & _mm512_cmpneq_epi32_mask(forwarded, _mm512_setzero_si512());
        const __mmask16 observable = memory & _mm512_test_epi32_mask(flags, _mm512_set1_epi32(BUSTER_IR_VOLATILE));
        result = valid & (~(memory | forward) | observable);
    }
    return result;
}

static void ir_sroa_flush_uses(IrSroaWork* work, IrSroaUseBatch* batch)
{
    const uint16_t escaped = ir_sroa_escape_mask(batch);
    for (uint32_t lane = 0; lane < batch->count; lane += 1)
    {
        if ((escaped & (UINT32_C(1) << lane)) != 0)
        {
            work->refused[batch->root[lane]] = 1;
        }
    }
    batch->count = 0;
}

static void ir_sroa_vector_uses(IrSroaWork* work)
{
    const IrFunction* f = work->ir.function;
    IrSroaUseBatch batch = { 0 };
    // IrUses is a stable sparse list. Pack its observed pointer uses into complete SoA packets before classifying;
    // no dword load from the IR's byte opcode/flag columns or signed hardware gather index is required.
    for (uint32_t value = 0; value < f->instruction_count; value += 1)
    {
        const uint32_t root = work->roots[value];
        if (root != BUSTER_IR_NONE)
        {
            for (uint32_t at = work->ir.uses.offsets[value]; at < work->ir.uses.offsets[(uint64_t)value + 1]; at += 1)
            {
                const IrUse use = work->ir.uses.uses[at];
                const uint32_t i = use.instruction;
                if (work->ir.dominators.rpo_index[work->ir.cfg.instruction_blocks[i]] != BUSTER_DOMINATOR_NONE)
                {
                    const uint32_t lane = batch.count++;
                    batch.op[lane] = f->op[i];
                    batch.position[lane] = use.operand;
                    batch.forwarded[lane] = work->roots[i] == root;
                    batch.flags[lane] = f->flags[i];
                    batch.root[lane] = root;
                    if (batch.count == 16)
                    {
                        ir_sroa_flush_uses(work, &batch);
                    }
                }
            }
        }
    }
    ir_sroa_flush_uses(work, &batch);
}

static __attribute__((unused)) IrSroaOptions ir_sroa_options(void)
{
    const IrSroaOptions options = { 0 };
    return options;
}

static void ir_sroa_provenance(IrSroaWork* work, bool scalar)
{
    (void)scalar;

    const IrFunction* f = work->ir.function;
    if (f->instruction_count != 0)
    {
        memset(work->roots, 0xff, (uint64_t)f->instruction_count * 4);
    }
    for (uint32_t slot = 0; slot < f->slot_count; slot += 1)
    {
        work->refused[slot] = (f->slots[slot].flags & (BUSTER_IR_SLOT_ADDRESS_TAKEN | BUSTER_IR_SLOT_VOLATILE)) != 0;
    }
    // These pointer definitions have only ordinary SSA operands, so RPO plus in-block order is topological.
    for (uint32_t r = 0; r < work->ir.dominators.reachable_count; r += 1)
    {
        const uint32_t block = work->ir.dominators.rpo[r];
        for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
        {
            const uint32_t op = f->op[i];
            if (op == IR_STACK_ADDR)
            {
                work->roots[i] = f->a[i];
                work->offsets[i] = 0;
            }
            else if ((op == IR_COPY || op == IR_BITCAST) && f->type[i] == IR_PTR && f->type[f->a[i]] == IR_PTR)
            {
                work->roots[i] = work->roots[f->a[i]];
                work->offsets[i] = work->offsets[f->a[i]];
            }
            else if (op == IR_ADDRESS && f->b[i] == BUSTER_IR_NONE && work->roots[f->a[i]] != BUSTER_IR_NONE)
            {
                const int64_t delta = f->addresses[f->c[i]].offset;
                const uint64_t base = work->offsets[f->a[i]];
                const uint64_t magnitude = delta < 0 ? -(uint64_t)delta : (uint64_t)delta;
                if (delta < 0 ? base >= magnitude : base <= UINT64_MAX - magnitude)
                {
                    work->roots[i] = work->roots[f->a[i]];
                    work->offsets[i] = delta < 0 ? base - magnitude : base + magnitude;
                }
            }
        }
    }
    if (!BUSTER_SCALAR_PATH(scalar))
    {
        ir_sroa_vector_uses(work);
    }
    for (uint32_t i = 0; BUSTER_SCALAR_PATH(scalar) && i < f->instruction_count; i += 1)
    {
        if (work->ir.dominators.rpo_index[work->ir.cfg.instruction_blocks[i]] != BUSTER_DOMINATOR_NONE)
        {
            const uint64_t count = ir_operand_count(f, i);
            for (uint32_t number = 0; number < count; number += 1)
            {
                const IrOperand operand = ir_operand(f, i, number, work->ir.cfg.instruction_blocks[i]);
                const uint32_t root = work->roots[operand.value];
                if (root != BUSTER_IR_NONE)
                {
                    const bool memory = operand.position == 0 && (f->op[i] == IR_LOAD || f->op[i] == IR_STORE);
                    const bool forward = operand.position == 0 && (f->op[i] == IR_COPY || f->op[i] == IR_BITCAST || f->op[i] == IR_ADDRESS) && work->roots[i] == root;
                    if ((!memory && !forward) || (memory && (f->flags[i] & BUSTER_IR_VOLATILE) != 0))
                    {
                        work->refused[root] = 1;
                    }
                }
            }
        }
    }
}

// Exact scalar and vector interval/key queries. The caller proves offset+size is inside the source slot, so ends
// cannot wrap. Inactive lanes never read fields; the arrays have field_count logical elements, without padding.
static uint32_t ir_sroa_field(const IrSroaWork* work, uint32_t root, uint64_t offset, uint32_t type, uint32_t size, bool scalar, bool* overlap)
{
    (void)scalar;

    uint32_t found = BUSTER_IR_NONE;
    bool conflict = false;
    if (BUSTER_SCALAR_PATH(scalar))
    {
        for (uint32_t i = 0; i < work->field_count; i += 1)
        {
            if (work->field_roots[i] == root)
            {
                const bool same = work->field_offsets[i] == offset && work->field_types[i] == type;
                if (same)
                {
                    found = i;
                }
                else if (offset < work->field_offsets[i] + work->field_sizes[i] && work->field_offsets[i] < offset + size)
                {
                    conflict = true;
                }
            }
        }
    }
    else
    {
        for (uint64_t first = 0; first < work->field_count; first += 8)
        {
            const uint32_t count = work->field_count - first < 8 ? work->field_count - first : 8;
            const __mmask8 valid = (UINT32_C(1) << count) - 1;
            const __m256i roots = _mm256_maskz_loadu_epi32(valid, work->field_roots + first);
            const __m256i types = _mm256_maskz_loadu_epi32(valid, work->field_types + first);
            const __m256i sizes = _mm256_maskz_loadu_epi32(valid, work->field_sizes + first);
            const __m512i offsets = _mm512_maskz_loadu_epi64(valid, work->field_offsets + first);
            const __m512i ends = _mm512_add_epi64(offsets, _mm512_cvtepu32_epi64(sizes));
            const __mmask8 roots_equal = valid & _mm256_cmpeq_epi32_mask(roots, _mm256_set1_epi32(root));
            const __mmask8 same = roots_equal & _mm512_cmpeq_epi64_mask(offsets, _mm512_set1_epi64(offset)) & _mm256_cmpeq_epi32_mask(types, _mm256_set1_epi32(type));
            const __mmask8 intersect = roots_equal & _mm512_cmplt_epu64_mask(_mm512_set1_epi64(offset), ends) & _mm512_cmplt_epu64_mask(offsets, _mm512_set1_epi64(offset + size));
            if (same != 0)
            {
                found = first + 31 - __builtin_clz((uint32_t)same);
            }
            conflict = conflict || (intersect & ~same) != 0;
        }
    }
    *overlap = conflict;
    return found;
}

static void ir_sroa_fields(IrSroaWork* work, bool scalar)
{
    (void)scalar;

    const IrFunction* f = work->ir.function;
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        if ((f->op[i] == IR_LOAD || f->op[i] == IR_STORE) && work->roots[f->a[i]] != BUSTER_IR_NONE)
        {
            const uint32_t root = work->roots[f->a[i]];
            const uint64_t offset = work->offsets[f->a[i]];
            const uint32_t type = f->op[i] == IR_LOAD ? f->type[i] : f->type[f->b[i]];
            const uint32_t bytes = (ir_type_bits[type] + 7) / 8;
            if (offset > f->slots[root].size || bytes > f->slots[root].size - offset)
            {
                work->refused[root] = 1;
            }
            else if (work->refused[root] == 0)
            {
                bool overlap = false;
                uint32_t field = ir_sroa_field(work, root, offset, type, bytes, BUSTER_SCALAR_PATH(scalar), &overlap);
                if (overlap)
                {
                    work->refused[root] = 1;
                }
                else
                {
                    if (field == BUSTER_IR_NONE)
                    {
                        field = work->field_count++;
                        work->field_counts[root] += 1;
                        work->field_roots[field] = root;
                        work->field_offsets[field] = offset;
                        work->field_types[field] = type;
                        work->field_sizes[field] = bytes;
                        work->field_alignments[field] = f->slots[root].alignment;
                    }
                    const uint32_t alignment = UINT32_C(1) << f->alignment_log2[i];
                    if (alignment > work->field_alignments[field])
                    {
                        work->field_alignments[field] = alignment;
                    }
                }
            }
        }
    }
}

static IrError ir_sroa_view(IrSroaWork* work, Arena* scratch, IrFunction* result)
{
    const IrFunction* f = work->ir.function;
    uint64_t slots = f->slot_count;
    for (uint32_t field = 0; field < work->field_count; field += 1)
    {
        slots += work->refused[work->field_roots[field]] == 0;
    }
    IrError error = slots <= UINT32_MAX ? IR_OK : IR_ERROR_LIMIT;
    IrFunction view = *f;
    uint32_t* firsts = NULL;
    if (error == IR_OK)
    {
        bool ok = true;
        firsts = ir_allocate(scratch, f->slot_count, 4, &ok);
        view.op = ir_allocate(scratch, f->instruction_count, 1, &ok);
        view.a = ir_allocate(scratch, f->instruction_count, 4, &ok);
        view.b = ir_allocate(scratch, f->instruction_count, 4, &ok);
        view.c = ir_allocate(scratch, f->instruction_count, 4, &ok);
        view.slots = ir_allocate(scratch, slots, sizeof(IrSlot), &ok);
        view.slot_count = slots;
        error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (error == IR_OK)
    {
        for (uint32_t slot = 0; slot < f->slot_count; slot += 1)
        {
            view.slots[slot] = f->slots[slot];
            firsts[slot] = BUSTER_IR_NONE;
            work->statistics.refused_slots += work->refused[slot] != 0;
            work->statistics.split_slots += work->refused[slot] == 0 && work->field_counts[slot] != 0;
        }
        uint32_t next = f->slot_count;
        for (uint32_t field = 0; field < work->field_count; field += 1)
        {
            const uint32_t root = work->field_roots[field];
            work->field_slots[field] = BUSTER_IR_NONE;
            if (work->refused[root] == 0)
            {
                firsts[root] = firsts[root] == BUSTER_IR_NONE ? next : firsts[root];
                work->field_slots[field] = next;
                view.slots[next++] = (IrSlot){
                    .size = work->field_sizes[field],
                    .alignment = work->field_alignments[field],
                    .type = work->field_types[field],
                    .flags = f->slots[root].flags & BUSTER_IR_SLOT_TEMPORARY,
                };
                work->statistics.fields += 1;
            }
        }
        for (uint32_t i = 0; i < f->instruction_count; i += 1)
        {
            view.op[i] = f->op[i];
            view.a[i] = f->a[i];
            view.b[i] = f->b[i];
            view.c[i] = f->c[i];
            // A split slot's lifetime markers go to its first field, which no access reaches outside the slot's
            // lifetime; its other fields keep the whole function's, and the slot itself, without references, goes.
            if (f->op[i] == IR_LIFETIME && firsts[f->a[i]] != BUSTER_IR_NONE)
            {
                view.a[i] = firsts[f->a[i]];
            }
            const uint32_t root = work->roots[i];
            if (root != BUSTER_IR_NONE && work->refused[root] == 0)
            {
                for (uint32_t field = 0; field < work->field_count; field += 1)
                {
                    if (work->field_roots[field] == root && work->field_offsets[field] == work->offsets[i])
                    {
                        view.op[i] = IR_STACK_ADDR;
                        view.a[i] = work->field_slots[field];
                        view.b[i] = BUSTER_IR_NONE;
                        view.c[i] = BUSTER_IR_NONE;
                    }
                }
            }
        }
        *result = view;
    }
    return error;
}

static IrError ir_sroa_compact_slots(IrFunction* f, Arena* scratch)
{
    bool ok = true;
    uint32_t* slots = ir_allocate(scratch, f->slot_count, 4, &ok);
    if (ok)
    {
        if (f->slot_count != 0)
        {
            memset(slots, 0xff, (uint64_t)f->slot_count * 4);
        }
        // A lifetime marker refers to its slot as an address does: a slot only markers refer to stays, with them.
        for (uint32_t i = 0; i < f->instruction_count; i += 1)
        {
            if (f->op[i] == IR_STACK_ADDR || f->op[i] == IR_LIFETIME)
            {
                slots[f->a[i]] = 0;
            }
        }
        uint32_t count = 0;
        for (uint32_t slot = 0; slot < f->slot_count; slot += 1)
        {
            if (slots[slot] != BUSTER_IR_NONE)
            {
                slots[slot] = count;
                f->slots[count++] = f->slots[slot];
            }
        }
        for (uint32_t i = 0; i < f->instruction_count; i += 1)
        {
            if (f->op[i] == IR_STACK_ADDR || f->op[i] == IR_LIFETIME)
            {
                f->a[i] = slots[f->a[i]];
            }
        }
        f->slot_count = count;
    }
    const IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    return error;
}

static __attribute__((unused)) IrCheck ir_optimize_sroa(const IrFunction* function, Arena* output, Arena* scratch, IrSroaOptions options, IrSroaResult* result)
{
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    IrSroaWork work = { 0 };
    IrCheck check = ir_check(IR_ERROR_METADATA, BUSTER_IR_NONE, 0);
    if (output != scratch)
    {
        check = ir_optimize_prepare(&work.ir, function, output, scratch, BUSTER_SCALAR_PATH(options.scalar), false);
    }
    if (check.error == IR_OK)
    {
        bool ok = true;
        const uint64_t n = function->instruction_count;
        work.roots = ir_allocate(scratch, n, 4, &ok);
        work.offsets = ir_allocate(scratch, n, 8, &ok);
        work.refused = ir_allocate(scratch, function->slot_count, 1, &ok);
        work.field_counts = ir_allocate(scratch, function->slot_count, 4, &ok);
        work.field_roots = ir_allocate(scratch, n, 4, &ok);
        work.field_offsets = ir_allocate(scratch, n, 8, &ok);
        work.field_types = ir_allocate(scratch, n, 4, &ok);
        work.field_sizes = ir_allocate(scratch, n, 4, &ok);
        work.field_alignments = ir_allocate(scratch, n, 4, &ok);
        work.field_slots = ir_allocate(scratch, n, 4, &ok);
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    IrFunction view = { 0 };
    if (check.error == IR_OK)
    {
        ir_sroa_provenance(&work, BUSTER_SCALAR_PATH(options.scalar));
        ir_sroa_fields(&work, BUSTER_SCALAR_PATH(options.scalar));
        check.error = ir_sroa_view(&work, scratch, &view);
    }
    if (check.error == IR_OK)
    {
        work.ir.function = &view;
        for (uint32_t block = 0; block < view.block_count; block += 1)
        {
            work.ir.blocks[block] = work.ir.dominators.rpo_index[block] != BUSTER_DOMINATOR_NONE;
            for (uint32_t edge = work.ir.cfg.successor_offsets[block]; edge < work.ir.cfg.successor_offsets[(uint64_t)block + 1]; edge += 1)
            {
                work.ir.edges[edge] = work.ir.blocks[block];
            }
        }
        for (uint32_t i = 0; i < view.instruction_count; i += 1)
        {
            work.ir.states[i] = IROPT_OVERDEFINED;
        }
        IrOptimizeOptions rewrite = ir_optimize_options();
        rewrite.passes = BUSTER_IR_OPTIMIZE_DCE;
        rewrite.scalar = BUSTER_SCALAR_PATH(options.scalar);
        ir_optimize_dce(&work.ir, rewrite);
        check.error = ir_optimize_rewrite(&work.ir, output, scratch, rewrite, &work.statistics.function);
    }
    if (check.error == IR_OK)
    {
        check.error = ir_sroa_compact_slots(&work.statistics.function, scratch);
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
