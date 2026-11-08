// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original private-slot field splitting (#139), included after ir_optimize.c. The resulting typed scalar slots
// feed the upstream ssa_ir_promote adapter; this module does not implement another SSA constructor.
// Every observed pointer use must be understood before any field of a slot is rewritten.
//
// #1282's options take the by-value aggregates the typed stream leaves of vectors, which an inlined call's copies then
// reach: `simd` takes a function with vectors, masks or machine rows (a vector field's slot is promoted whole), and
// `flagged` takes an address-taken slot (the typed stream marks every object so) whose every use the provenance below
// understands, since only its STACK_ADDR reaches a slot. Provenance follows an address through COPY, BITCAST, an
// ADDRESS whose index is a constant, and the typed stream's PTRTOINT/INTTOPTR round trips; an integer of a slot's address
// used otherwise escapes it. A store that a later store of the same slot in its block overwrites, with no load of the
// slot between, is dead: it becomes a lifetime start of a sink slot no access reaches (which promotion then drops), so
// that a zeroed object's stores don't overlap the vector field that overwrites them.

typedef struct IrSroaOptions IrSroaOptions;
struct IrSroaOptions
{
    bool scalar;
    bool simd;
    bool flagged;
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
    // Each root's fields in creation order (#1366), as indices plus one (0 for none, as ir_allocate zeroes them): a
    // root's first and last field and each field's next of its root. The field queries walk one root's fields, not the
    // function's: the table holds every split slot's fields, and inlined calls with aggregates leave thousands of slots.
    uint32_t* root_first;
    uint32_t* root_last;
    uint32_t* field_next;
    // Per instruction, whether it is a dead store (ir_sroa_dead_stores), and how many there are.
    uint8_t* dead;
    uint32_t dead_count;
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
        const __mmask16 forward_op = _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_COPY)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_BITCAST)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_ADDRESS)) |
                                     _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_PTRTOINT)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_INTTOPTR));
        const __mmask16 forward = first & forward_op & _mm512_cmpneq_epi32_mask(forwarded, _mm512_setzero_si512());
        const __mmask16 observable = memory & _mm512_test_epi32_mask(flags, _mm512_set1_epi32(BUSTER_IR_OBSERVABLE));
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

// A pointer's root and offset past `base`'s by `delta` bytes into *offset, or false where it would wrap.
static bool ir_sroa_offset(uint64_t base, int64_t delta, uint64_t* offset)
{
    const uint64_t magnitude = delta < 0 ? -(uint64_t)delta : (uint64_t)delta;
    const bool fits = delta < 0 ? base >= magnitude : base <= UINT64_MAX - magnitude;
    *offset = fits ? (delta < 0 ? base - magnitude : base + magnitude) : 0;
    return fits;
}

static void ir_sroa_provenance(IrSroaWork* work, bool scalar, bool flagged)
{
    (void)scalar;

    const IrFunction* f = work->ir.function;
    if (f->instruction_count != 0)
    {
        memset(work->roots, 0xff, (uint64_t)f->instruction_count * 4);
    }
    // With `flagged` (#1282), an address-taken slot is refused by its uses alone; a volatile one and the incoming frame's
    // anchors (a variadic definition's) stay memory whatever their uses.
    const uint32_t refusing = flagged ? BUSTER_IR_SLOT_VOLATILE | BUSTER_IR_SLOT_REGISTER_SAVE_AREA | BUSTER_IR_SLOT_OVERFLOW_AREA :
                                        BUSTER_IR_SLOT_ADDRESS_TAKEN | BUSTER_IR_SLOT_VOLATILE;
    for (uint32_t slot = 0; slot < f->slot_count; slot += 1)
    {
        work->refused[slot] = (f->slots[slot].flags & refusing) != 0;
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
            else if (flagged && ((op == IR_PTRTOINT && f->type[i] == IR_I64 && f->type[f->a[i]] == IR_PTR) || (op == IR_INTTOPTR && f->type[f->a[i]] == IR_I64)))
            {
                // The typed stream's addresses are integers between their memory accesses (#1282): a slot's address as
                // an i64 that an inttoptr makes a ptr again is the same address. Any other use of the integer escapes.
                work->roots[i] = work->roots[f->a[i]];
                work->offsets[i] = work->offsets[f->a[i]];
            }
            else if (op == IR_ADDRESS && work->roots[f->a[i]] != BUSTER_IR_NONE)
            {
                // An index that is a constant (the typed stream's offsets, with `flagged`) moves the address by its value
                // times the scale; any other index leaves the address unknown.
                const IrAddress address = f->addresses[f->c[i]];
                const uint32_t index = f->b[i];
                const bool constant = index != BUSTER_IR_NONE && flagged && f->op[index] == IR_CONST && f->type[index] == IR_I64;
                const int64_t scaled = constant ? (int64_t)(f->constants[f->a[index]].low * (uint64_t)address.scale) : 0;
                const bool small = !constant || (f->constants[f->a[index]].low < (UINT64_C(1) << 32) && address.scale <= 8);
                uint64_t moved = 0;
                if ((index == BUSTER_IR_NONE || constant) && small && ir_sroa_offset(work->offsets[f->a[i]], address.offset, &moved) &&
                    ir_sroa_offset(moved, scaled, &moved))
                {
                    work->roots[i] = work->roots[f->a[i]];
                    work->offsets[i] = moved;
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
                    const bool forward = operand.position == 0 && (f->op[i] == IR_COPY || f->op[i] == IR_BITCAST || f->op[i] == IR_ADDRESS || f->op[i] == IR_PTRTOINT || f->op[i] == IR_INTTOPTR) &&
                                         work->roots[i] == root;
                    if ((!memory && !forward) || (memory && (f->flags[i] & BUSTER_IR_OBSERVABLE) != 0))
                    {
                        work->refused[root] = 1;
                    }
                }
            }
        }
    }
}

// The bytes a load or a store at `i` accesses.
static uint32_t ir_sroa_access_bytes(const IrFunction* f, uint32_t i)
{
    const uint32_t type = f->op[i] == IR_LOAD ? f->type[i] : f->type[f->b[i]];
    return (ir_type_bits[type] + 7) / 8;
}

// The dead stores of the slots not refused (#1282, work->dead): walking each reachable block backwards, a slot's bytes
// that later stores of the block write before any load of the slot reads them are covered, one interval a slot (the
// latest stores', joined while they touch), and a store within it writes nothing any load reads. A slot not refused has
// no other access (its address escapes nowhere), so nothing else reads its bytes; across blocks nothing is assumed.
// `lows` and `highs` have a slot's room each; `touched` the block's instructions'.
static void ir_sroa_dead_stores(IrSroaWork* work, uint64_t* lows, uint64_t* highs, uint32_t* touched)
{
    const IrFunction* f = work->ir.function;
    for (uint32_t slot = 0; slot < f->slot_count; slot += 1)
    {
        lows[slot] = 0;
        highs[slot] = 0;
    }
    for (uint32_t r = 0; r < work->ir.dominators.reachable_count; r += 1)
    {
        const uint32_t block = work->ir.dominators.rpo[r];
        uint32_t touched_count = 0;
        for (uint32_t i = f->block_offsets[(uint64_t)block + 1]; i > f->block_offsets[block]; i -= 1)
        {
            const uint32_t node = i - 1;
            const uint32_t op = f->op[node];
            const uint32_t root = (op == IR_LOAD || op == IR_STORE) ? work->roots[f->a[node]] : BUSTER_IR_NONE;
            if (root != BUSTER_IR_NONE && work->refused[root] == 0)
            {
                const uint64_t first = work->offsets[f->a[node]];
                const uint64_t bytes = ir_sroa_access_bytes(f, node);
                const uint64_t end = first <= UINT64_MAX - bytes ? first + bytes : UINT64_MAX;
                if (lows[root] == highs[root])
                {
                    touched[touched_count++] = root;
                }
                if (op == IR_LOAD)
                {
                    // A load reads what the stores after it wrote over: none of the block's earlier ones are dead by them.
                    highs[root] = lows[root];
                }
                else if (lows[root] < highs[root] && lows[root] <= first && end <= highs[root])
                {
                    work->dead[node] = 1;
                    work->dead_count += 1;
                }
                else if (lows[root] < highs[root] && first <= highs[root] && lows[root] <= end)
                {
                    lows[root] = first < lows[root] ? first : lows[root];
                    highs[root] = end > highs[root] ? end : highs[root];
                }
                else
                {
                    lows[root] = first;
                    highs[root] = end;
                }
            }
        }
        for (uint32_t k = 0; k < touched_count; k += 1)
        {
            lows[touched[k]] = 0;
            highs[touched[k]] = 0;
        }
    }
}

// The exact interval/key query over `root`'s fields alone, in creation order (#1366): its field of `offset` and `type`, and
// whether another of its fields overlaps the access. The caller proves offset+size is inside the source slot, so ends
// cannot wrap. Both paths walk the root's chain. A root has a handful of fields (one per distinct access, 16 for a
// 64-byte vector of int), and the 8-wide compare the vector path had over the function's whole field table was what
// made the pass quadratic in inlined calls: over one root's fields it would mostly run on a partial vector.
static uint32_t ir_sroa_field(const IrSroaWork* work, uint32_t root, uint64_t offset, uint32_t type, uint32_t size, bool scalar, bool* overlap)
{
    (void)scalar;

    uint32_t found = BUSTER_IR_NONE;
    bool conflict = false;
    for (uint32_t next = work->root_first[root]; next != 0; next = work->field_next[next - 1])
    {
        const uint32_t i = next - 1;
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
    *overlap = conflict;
    return found;
}

static void ir_sroa_fields(IrSroaWork* work, bool scalar)
{
    (void)scalar;

    const IrFunction* f = work->ir.function;
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        if ((f->op[i] == IR_LOAD || f->op[i] == IR_STORE) && work->roots[f->a[i]] != BUSTER_IR_NONE && work->dead[i] == 0)
        {
            const uint32_t root = work->roots[f->a[i]];
            const uint64_t offset = work->offsets[f->a[i]];
            const uint32_t type = f->op[i] == IR_LOAD ? f->type[i] : f->type[f->b[i]];
            const uint32_t bytes = ir_sroa_access_bytes(f, i);
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
                        if (work->root_last[root] == 0)
                        {
                            work->root_first[root] = field + 1;
                        }
                        else
                        {
                            work->field_next[work->root_last[root] - 1] = field + 1;
                        }
                        work->root_last[root] = field + 1;
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
    // The dead stores' sink (#1282): a byte no access reaches, whose lifetime starts stand where they were.
    bool sinking = false;
    for (uint32_t i = 0; work->dead_count != 0 && i < f->instruction_count; i += 1)
    {
        sinking = sinking || (work->dead[i] != 0 && work->refused[work->roots[f->a[i]]] == 0);
    }
    const uint32_t sink = (uint32_t)slots;
    slots += sinking;
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
        view.flags = ir_allocate(scratch, f->instruction_count, 1, &ok);
        view.alignment_log2 = ir_allocate(scratch, f->instruction_count, 1, &ok);
        view.slots = ir_allocate(scratch, slots, sizeof(IrSlot), &ok);
        view.slot_count = slots;
        error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (error == IR_OK && !ir_debug_create(scratch, &view, f->debug))
    {
        error = IR_ERROR_MEMORY;
    }
    if (error == IR_OK)
    {
        ir_debug_remap(f, &view, NULL, NULL);
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
                if (view.debug != NULL)
                {
                    // A split object's whole home no longer exists. New field slots start unknown too; describing
                    // either as the original whole object would let the debugger read adjacent, unrelated storage.
                    view.debug->locals[root] = BUSTER_IR_NONE;
                }
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
        if (sinking)
        {
            view.slots[sink] = (IrSlot){ .size = 1, .alignment = 1, .type = IR_I8 };
        }
        for (uint32_t i = 0; i < f->instruction_count; i += 1)
        {
            view.op[i] = f->op[i];
            view.flags[i] = f->flags[i];
            view.alignment_log2[i] = f->alignment_log2[i];
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
            if (work->dead[i] != 0 && work->refused[work->roots[f->a[i]]] == 0)
            {
                if (view.debug != NULL)
                {
                    // Even without a split field, surviving lifetime markers can keep this now-unwritten slot.
                    // Its bytes no longer represent every source assignment, so it cannot retain a whole home.
                    view.debug->locals[work->roots[f->a[i]]] = BUSTER_IR_NONE;
                }
                view.op[i] = IR_LIFETIME;
                view.flags[i] = 0;
                view.alignment_log2[i] = 0;
                view.a[i] = sink;
                view.b[i] = 0;
                view.c[i] = BUSTER_IR_NONE;
            }
            // Only a ptr becomes a field's address: a slot's address as an integer (#1282) has only inttoptrs' uses,
            // which become addresses themselves, and goes with the dead code.
            else if (root != BUSTER_IR_NONE && work->refused[root] == 0 && f->type[i] == IR_PTR)
            {
                // The root's fields in creation order, the last of `offset` winning as in a scan of the whole table (#1366).
                for (uint32_t next = work->root_first[root]; next != 0; next = work->field_next[next - 1])
                {
                    const uint32_t field = next - 1;
                    if (work->field_offsets[field] == work->offsets[i])
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
                if (f->debug != NULL)
                {
                    f->debug->locals[count] = f->debug->locals[slot];
                }
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
        if (f->debug != NULL)
        {
            f->debug->slot_count = count;
        }
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
        check = ir_optimize_prepare(&work.ir, function, output, scratch, BUSTER_SCALAR_PATH(options.scalar), options.simd);
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
        work.root_first = ir_allocate(scratch, function->slot_count, 4, &ok);
        work.root_last = ir_allocate(scratch, function->slot_count, 4, &ok);
        work.field_next = ir_allocate(scratch, n, 4, &ok);
        work.dead = ir_allocate(scratch, n, 1, &ok);
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
        if (ok && n != 0)
        {
            memset(work.dead, 0, n);
        }
    }
    IrFunction view = { 0 };
    if (check.error == IR_OK)
    {
        ir_sroa_provenance(&work, BUSTER_SCALAR_PATH(options.scalar), options.flagged);
        if (options.flagged)
        {
            const uint64_t mark = scratch->length;
            bool ok = true;
            uint64_t* lows = ir_allocate(scratch, function->slot_count, 8, &ok);
            uint64_t* highs = ir_allocate(scratch, function->slot_count, 8, &ok);
            uint32_t* touched = ir_allocate(scratch, function->instruction_count, 4, &ok);
            if (ok)
            {
                ir_sroa_dead_stores(&work, lows, highs, touched);
            }
            check.error = ok ? IR_OK : IR_ERROR_MEMORY;
            arena_pop_to(scratch, mark);
        }
    }
    if (check.error == IR_OK)
    {
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
