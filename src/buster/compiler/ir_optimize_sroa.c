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
//
// A vector assembled in memory (#1344): clang's headers write _mm512_set1_epi8, _mm512_setzero_si512, _mm512_set_epi64
// and their kin as vector compound literals, which the typed stream initializes as objects (each element's store, then
// one load of the whole vector), so that a splat of bytes was 72 stores and a load no store forwards to. A vector load
// whose every byte the block's stores before it wrote becomes the value they wrote (ir_sroa_assemble): a vector
// constant where every byte is a constant's, or a splat where every store wrote the same scalar. The stores then
// have no load to reach and go with their fields.

typedef struct IrSroaOptions IrSroaOptions;
struct IrSroaOptions
{
    bool scalar;
    bool simd;
    bool flagged;
    // #3311: the eightbytes of slots read and written at mixed widths (ir_sroa_widen), and a register-class aggregate
    // result's slot returned through a slot of its own. Needs `flagged`.
    bool widen;
};

typedef struct IrSroaResult IrSroaResult;
struct IrSroaResult
{
    IrFunction function;
    uint32_t split_slots;
    uint32_t fields;
    uint32_t refused_slots;
    // The vector loads that became the constant or the splat their block's stores wrote (#1344).
    uint32_t assembled;
    // #3311's slots rewritten as eightbytes, and the returns rewritten through a slot of their own (ir_sroa_widen).
    uint32_t widened_slots;
    uint32_t widened_returns;
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
    // Per instruction (ir_sroa_assemble, #1344): whether it is a vector load that becomes a constant
    // (BUSTER_IR_SROA_ASSEMBLED_CONSTANT, `assembled_values` its index among `assembled_constants`) or a splat
    // (BUSTER_IR_SROA_ASSEMBLED_SPLAT, `assembled_values` its scalar); and each load's first equal load of its block
    // (`same`, itself where there is none), since the typed stream reads a local again for each use.
    uint8_t* assembled;
    uint32_t* assembled_values;
    uint32_t* same;
    IrSimdValue* assembled_constants;
    uint32_t assembled_constant_count;
    uint32_t assembled_count;
    // #3311 (ir_sroa_widen): whether a ret's use of a slot's address is understood rather than an escape, and the slots a
    // reachable ret returns.
    bool returns;
    uint8_t* returned;
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

// The batch's uses that escape their slots: any but a load's or a store's address, or a forwarding instruction's operand
// (which then has the same root), and a volatile or atomic access's address. With `returns` (#3311), a ret's operand is
// no escape: *returning gets those lanes, whose slots the widening's return rewrite then decides.
static uint16_t ir_sroa_escape_mask(const IrSroaUseBatch* batch, bool returns, uint16_t* returning)
{
    uint16_t result = 0;
    *returning = 0;
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
        const __mmask16 ret = returns ? first & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_RET)) : 0;
        result = valid & (~(memory | forward | ret) | observable);
        *returning = ret;
    }
    return result;
}

static void ir_sroa_flush_uses(IrSroaWork* work, IrSroaUseBatch* batch)
{
    uint16_t returning = 0;
    const uint16_t escaped = ir_sroa_escape_mask(batch, work->returns, &returning);
    for (uint32_t lane = 0; lane < batch->count; lane += 1)
    {
        if ((escaped & (UINT32_C(1) << lane)) != 0)
        {
            work->refused[batch->root[lane]] = 1;
        }
        if ((returning & (UINT32_C(1) << lane)) != 0)
        {
            work->returned[batch->root[lane]] = 1;
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
        if (work->returns)
        {
            work->returned[slot] = 0;
        }
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
                    const bool ret = work->returns && operand.position == 0 && f->op[i] == IR_RET;
                    if ((!memory && !forward && !ret) || (memory && (f->flags[i] & BUSTER_IR_OBSERVABLE) != 0))
                    {
                        work->refused[root] = 1;
                    }
                    if (ret)
                    {
                        work->returned[root] = 1;
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

#define BUSTER_IR_SROA_ASSEMBLED_CONSTANT 1
#define BUSTER_IR_SROA_ASSEMBLED_SPLAT 2
// The widest object whose bytes a mask of 64 bits tracks: a vector of 64 bytes.
#define BUSTER_IR_SROA_ASSEMBLE_BYTES 64

// What a block's stores so far have made of a slot of at most 64 bytes (ir_sroa_assemble): the bytes they wrote
// (`covered`), those of them a constant's (`constant`, their values in `image`), and whether every store wrote the same
// scalar (`uniform`: `value`, of `bytes` bytes, at offsets that are its multiples). `load` is the slot's first load
// since its last store, which later loads of the same bytes and type equal.
typedef struct IrSroaAssembly IrSroaAssembly;
struct IrSroaAssembly
{
    uint64_t covered;
    uint64_t constant;
    uint32_t value;
    uint32_t load;
    uint8_t bytes;
    uint8_t uniform;
    uint8_t seen;
    // An access of the block's that leaves the slot's bytes (ir_sroa_fields refuses its slot): nothing is decided after it.
    uint8_t outside;
    uint8_t image[BUSTER_IR_SROA_ASSEMBLE_BYTES];
};

// A scalar's bits where it is a constant, through the conversions the typed stream leaves between a literal and its
// store (a truncation, an extension, a copy), into *bits; false for any other value.
static bool ir_sroa_constant_bits(const IrFunction* f, uint32_t value, uint64_t* bits)
{
    bool known = false;
    uint64_t result = 0;
    // The conversions from the stored value down to the constant, at most four, applied back in reverse.
    uint32_t chain[4];
    uint32_t depth = 0;
    uint32_t at = value;
    bool walking = true;
    while (walking)
    {
        const uint32_t op = f->op[at];
        const bool scalar = ir_splat_scalar_type(f->type[at]);
        if (op == IR_CONST && scalar)
        {
            result = f->constants[f->a[at]].low;
            known = true;
            walking = false;
        }
        else if ((op == IR_TRUNC || op == IR_ZEXT || op == IR_SEXT || op == IR_COPY) && scalar && f->type[at] >= IR_I8 && f->type[at] <= IR_I64 && depth < 4 &&
                 f->type[f->a[at]] >= IR_I8 && f->type[f->a[at]] <= IR_I64)
        {
            chain[depth++] = at;
            at = f->a[at];
        }
        else
        {
            walking = false;
        }
    }
    for (uint32_t k = depth; known && k-- > 0;)
    {
        const uint32_t i = chain[k];
        const uint32_t from = ir_type_bits[f->type[f->a[i]]];
        const uint32_t to = ir_type_bits[f->type[i]];
        const uint64_t source = from >= 64 ? result : result & ((UINT64_C(1) << from) - 1);
        const uint64_t extended = f->op[i] == IR_SEXT && from < 64 ? (uint64_t)((int64_t)(source << (64 - from)) >> (64 - from)) : source;
        result = to >= 64 ? extended : extended & ((UINT64_C(1) << to) - 1);
    }
    *bits = result;
    return known;
}

// The vector loads the block's stores decide (#1344, work->assembled): each reachable block's loads and stores of the
// slots not refused, of at most 64 bytes, in order. A slot not refused has no access but its loads and stores, and
// a dead store's bytes no load reads, so the stores walked are all that write the bytes a load of the block reads once
// they cover them; across blocks nothing is assumed. A load equals the slot's earlier load of the same bytes and type
// with no store between (work->same), which is how 64 reads of one parameter are one scalar. `states` has a slot's
// room each, zeroed; `touched` the block's instructions'.
static void ir_sroa_assemble(IrSroaWork* work, IrSroaAssembly* states, uint32_t* touched)
{
    const IrFunction* f = work->ir.function;
    for (uint32_t r = 0; r < work->ir.dominators.reachable_count; r += 1)
    {
        const uint32_t block = work->ir.dominators.rpo[r];
        uint32_t touched_count = 0;
        for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
        {
            const uint32_t op = f->op[i];
            const uint32_t root = (op == IR_LOAD || op == IR_STORE) ? work->roots[f->a[i]] : BUSTER_IR_NONE;
            const uint64_t size = root != BUSTER_IR_NONE ? f->slots[root].size : 0;
            const uint64_t offset = root != BUSTER_IR_NONE ? work->offsets[f->a[i]] : 0;
            const uint64_t bytes = root != BUSTER_IR_NONE ? ir_sroa_access_bytes(f, i) : 0;
            const bool tracked = root != BUSTER_IR_NONE && work->refused[root] == 0 && work->dead[i] == 0 && size <= BUSTER_IR_SROA_ASSEMBLE_BYTES;
            const bool inside = bytes != 0 && offset <= size && bytes <= size - offset;
            if (tracked && states[root].seen == 0)
            {
                touched[touched_count++] = root;
                states[root] = (IrSroaAssembly){
                    .value = BUSTER_IR_NONE,
                    .load = BUSTER_IR_NONE,
                    .uniform = 1,
                    .seen = 1,
                };
            }
            if (tracked && !inside)
            {
                states[root].outside = 1;
            }
            if (tracked && inside && states[root].outside == 0)
            {
                IrSroaAssembly* state = &states[root];
                const uint64_t range = (bytes == 64 ? UINT64_MAX : (UINT64_C(1) << bytes) - 1) << offset;
                if (op == IR_STORE)
                {
                    const uint32_t stored = f->b[i];
                    const uint32_t type = f->type[stored];
                    // The value as its first equal load, where it is a load of this block's.
                    const uint32_t value = work->same[stored];
                    uint64_t bits = 0;
                    if (bytes <= 8 && ir_sroa_constant_bits(f, stored, &bits))
                    {
                        memcpy(state->image + offset, &bits, bytes);
                        state->constant |= range;
                    }
                    else
                    {
                        state->constant &= ~range;
                    }
                    const bool lane = ir_splat_scalar_type(type) && offset % bytes == 0 && (state->value == BUSTER_IR_NONE || (state->value == value && state->bytes == bytes));
                    state->uniform = state->uniform != 0 && lane;
                    state->value = value;
                    state->bytes = (uint8_t)bytes;
                    state->covered |= range;
                    state->load = BUSTER_IR_NONE;
                }
                else
                {
                    const uint32_t first = state->load;
                    if (first != BUSTER_IR_NONE && work->offsets[f->a[first]] == offset && f->type[first] == f->type[i])
                    {
                        work->same[i] = first;
                    }
                    else
                    {
                        state->load = i;
                    }
                    if (ir_vector_type(f->type[i]) && (state->constant & range) == range)
                    {
                        IrSimdValue constant = { 0 };
                        memcpy(constant.words, state->image + offset, bytes);
                        work->assembled[i] = BUSTER_IR_SROA_ASSEMBLED_CONSTANT;
                        work->assembled_values[i] = work->assembled_constant_count;
                        work->assembled_constants[work->assembled_constant_count++] = constant;
                        work->assembled_count += 1;
                    }
                    else if (ir_vector_type(f->type[i]) && state->uniform != 0 && (state->covered & range) == range && offset % state->bytes == 0)
                    {
                        work->assembled[i] = BUSTER_IR_SROA_ASSEMBLED_SPLAT;
                        work->assembled_values[i] = state->value;
                        work->assembled_count += 1;
                    }
                }
            }
        }
        for (uint32_t k = 0; k < touched_count; k += 1)
        {
            states[touched[k]].seen = 0;
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
        // An assembled load (#1344) reads no memory any more, so it makes no field.
        if ((f->op[i] == IR_LOAD || f->op[i] == IR_STORE) && work->roots[f->a[i]] != BUSTER_IR_NONE && work->dead[i] == 0 && work->assembled[i] == 0)
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
        if (work->assembled_constant_count != 0)
        {
            // The assembled constants (#1344) after the function's own, which its IR_VCONSTs index as before.
            const uint64_t constants = (uint64_t)f->vector_constant_count + work->assembled_constant_count;
            IrSimdValue* values = constants <= UINT32_MAX ? ir_allocate(scratch, constants, sizeof(IrSimdValue), &ok) : NULL;
            ok = ok && values != NULL;
            if (ok)
            {
                if (f->vector_constant_count != 0)
                {
                    memcpy(values, f->vector_constants, (uint64_t)f->vector_constant_count * sizeof(IrSimdValue));
                }
                memcpy(values + f->vector_constant_count, work->assembled_constants, (uint64_t)work->assembled_constant_count * sizeof(IrSimdValue));
                view.vector_constants = values;
                view.vector_constant_count = (uint32_t)constants;
            }
        }
        error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (error == IR_OK && !ir_debug_create(scratch, &view, f->debug))
    {
        error = IR_ERROR_MEMORY;
    }
    if (error == IR_OK)
    {
        ir_debug_remap(f, &view, NULL, NULL, NULL);
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
            if (work->assembled[i] != 0)
            {
                // The value the block's stores wrote (#1344), whatever becomes of the slot: its uses were all understood
                // when the load was decided.
                const bool constant = work->assembled[i] == BUSTER_IR_SROA_ASSEMBLED_CONSTANT;
                view.op[i] = constant ? IR_VCONST : IR_INTRINSIC;
                view.flags[i] = 0;
                view.alignment_log2[i] = 0;
                view.a[i] = constant ? f->vector_constant_count + work->assembled_values[i] : work->assembled_values[i];
                view.b[i] = BUSTER_IR_NONE;
                view.c[i] = constant ? BUSTER_IR_NONE : BUSTER_IR_INTRINSIC_SPLAT;
                work->statistics.assembled += 1;
            }
            else if (work->dead[i] != 0 && work->refused[work->roots[f->a[i]]] == 0)
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

// The split itself (ir_optimize_sroa, after #3311's widening).
static IrCheck ir_sroa_split(const IrFunction* function, Arena* output, Arena* scratch, IrSroaOptions options, IrSroaResult* result)
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
        work.assembled = ir_allocate(scratch, n, 1, &ok);
        work.assembled_values = ir_allocate(scratch, n, 4, &ok);
        work.same = ir_allocate(scratch, n, 4, &ok);
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
        if (ok && n != 0)
        {
            memset(work.dead, 0, n);
            memset(work.assembled, 0, n);
        }
        for (uint32_t i = 0; ok && i < n; i += 1)
        {
            work.same[i] = i;
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
    if (check.error == IR_OK && options.flagged && options.simd)
    {
        // The vectors the stores assemble (#1344), once the dead stores are known: a constant a vector load of the
        // function at most, kept past the walk's own state.
        bool ok = true;
        uint64_t loads = 0;
        for (uint32_t i = 0; i < function->instruction_count; i += 1)
        {
            loads += function->op[i] == IR_LOAD && ir_vector_type(function->type[i]);
        }
        work.assembled_constants = loads != 0 ? ir_allocate(scratch, loads, sizeof(IrSimdValue), &ok) : NULL;
        const uint64_t mark = scratch->length;
        IrSroaAssembly* states = loads != 0 ? ir_allocate(scratch, function->slot_count, sizeof(IrSroaAssembly), &ok) : NULL;
        uint32_t* touched = loads != 0 ? ir_allocate(scratch, function->instruction_count, 4, &ok) : NULL;
        if (ok && loads != 0)
        {
            memset(states, 0, (uint64_t)function->slot_count * sizeof(IrSroaAssembly));
            ir_sroa_assemble(&work, states, touched);
        }
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
        arena_pop_to(scratch, mark);
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
        check = ir_optimize_verify_result(&work.statistics.function, output, scratch);
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

// #3311: eightbytes. The typed stream copies a by-value aggregate's bytes by eightbytes (a parameter's into its object,
// a register-class result's out of the object its ret returns), while the source reads and writes the fields, an int's
// four bytes at a time: a slot of mixed widths, whose accesses overlap, which the split refuses, and which a result's ret
// takes the address of. So a struct passed and returned in registers was copied to and from the stack around every
// field. Before the split, ir_sroa_widen rewrites such slots as their eightbytes, as LLVM's SROA widens an alloca to an
// integer:
// - a slot not refused but for its overlaps or a ret (with `returns`, a ret's use of an address is understood), of at
//   most 64 bytes, all of whose loads and stores are of scalars (integers of 1 to 8 bytes, f32, f64 and ptr) each inside
//   one aligned eightbyte, has every access made one of its eightbyte's, an i64: a narrower load the eightbyte shifted
//   right and truncated (bitcast to f32), and a narrower store the eightbyte with the field's bits cleared, or'ed with the
//   value zero-extended and shifted left; an f64's and a ptr's the eightbyte's bits. Its size is rounded up to 8;
// - a reachable ret of such a slot's address (offset 0), where the function's result is a register class's of at most
//   16 bytes in eightbytes (general registers or xmm ones, no x87, no vector whole), returns a new slot of its own
//   instead, which the eightbytes are loaded from the object into and stored to just before the ret: the object is then
//   private, and only those stores and their ret stay memory.
// The split then makes each eightbyte a field of its own, which promotion makes values, and the optimizer folds the
// shifts and masks: what was three copies through memory is a value's bits. A function with debug information is left
// as it is, as its objects' homes are its locals'. The rewrite inserts each access's instructions before it in its block,
// with the unroller's remapping (ir_unroll_remap, ir_unroll_extras); blocks keep their numbers.
#define BUSTER_IR_SROA_WIDEN_BYTES 64
#define BUSTER_IR_SROA_WIDEN_KEEP 0
#define BUSTER_IR_SROA_WIDEN_LOAD 1
#define BUSTER_IR_SROA_WIDEN_STORE 2
#define BUSTER_IR_SROA_WIDEN_RET 3

// Whether a scalar of `type` may live in an eightbyte (#3311): an integer of 1 to 8 bytes, an f32, an f64 or a ptr.
static bool ir_sroa_widen_type(uint32_t type)
{
    return type == IR_I8 || type == IR_I16 || type == IR_I32 || type == IR_I64 || type == IR_F32 || type == IR_F64 || type == IR_PTR;
}

// The eightbytes a function's result returns in registers (#3311): its result's size over 8, rounded up, where the
// convention returns it in one or two general or xmm registers an eightbyte each; else 0.
static uint32_t ir_sroa_widen_result(const IrFunction* f)
{
    const AbiReturn result = f->result_abi;
    bool parts = result.kind == BUSTER_ABI_RESULT_REGISTERS && (result.flags & BUSTER_ABI_FLAG_HIGH) == 0 && result.part_bytes == 8 && result.registers[0] != BUSTER_ABI_REGISTER_NONE &&
                 result.registers[2] == BUSTER_ABI_REGISTER_NONE && result.registers[3] == BUSTER_ABI_REGISTER_NONE;
    for (uint32_t k = 0; k < 2; k += 1)
    {
        const uint8_t reg = result.registers[k];
        parts = parts && (reg == BUSTER_ABI_REGISTER_NONE || reg < 16 || (reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR);
    }
    const uint32_t count = (f->result_size + 7) / 8;
    return parts && f->result_size != 0 && f->result_size <= 16 && f->result_type == IR_PTR ? count : 0;
}

// The instructions ir_sroa_widen inserts before instruction `i` of `kind`, an access of `bytes` bytes of `type` at
// `offset` in its slot, or a ret of `chunks` eightbytes.
static uint32_t ir_sroa_widen_inserted(uint8_t kind, uint32_t type, uint64_t offset, uint32_t bytes, uint32_t chunks)
{
    const uint32_t shifted = (offset & 7) != 0;
    uint32_t count = 0;
    if (kind == BUSTER_IR_SROA_WIDEN_LOAD)
    {
        count = bytes == 8 ? (type == IR_I64 ? 0 : 1) : shifted + 1 + 2 * shifted + (type == IR_F32);
    }
    else if (kind == BUSTER_IR_SROA_WIDEN_STORE)
    {
        count = bytes == 8 ? (type == IR_I64 ? 0 : 1) : shifted + 3 + (type == IR_F32) + 1 + 2 * shifted + 1;
    }
    else if (kind == BUSTER_IR_SROA_WIDEN_RET)
    {
        count = 1 + 2 * (2 * chunks - 1);
    }
    return count;
}

// A new address row `offset` bytes from `base` at `at` (an IR_ADDRESS of no index).
static void ir_sroa_widen_address(IrUnrollEmit* emit, uint32_t at, uint32_t base, int64_t offset)
{
    emit->view->addresses[emit->address] = (IrAddress){ .offset = offset, .scale = 1 };
    ir_unroll_put(emit, at, IR_ADDRESS, IR_PTR, base, BUSTER_IR_NONE, emit->address);
    emit->address += 1;
}

// Instruction `i` of `kind` rewritten (#3311), its inserted instructions from `at` and itself at map[i]: see the header.
// `slot` is the new slot a ret returns.
static void ir_sroa_widen_emit(const IrFunction* f, const IrSroaWork* work, IrUnrollEmit* emit, const uint32_t* map, uint32_t i, uint8_t kind, uint32_t chunks, uint32_t slot)
{
    IrFunction* view = emit->view;
    uint32_t at = map[i] - ir_sroa_widen_inserted(kind, kind == BUSTER_IR_SROA_WIDEN_RET ? IR_VOID : (f->op[i] == IR_LOAD ? f->type[i] : f->type[f->b[i]]),
                                                  kind == BUSTER_IR_SROA_WIDEN_RET ? 0 : work->offsets[f->a[i]], kind == BUSTER_IR_SROA_WIDEN_RET ? 0 : ir_sroa_access_bytes(f, i), chunks);
    const uint32_t base = map[f->a[i]];
    if (kind == BUSTER_IR_SROA_WIDEN_RET)
    {
        uint32_t loads[2] = { BUSTER_IR_NONE, BUSTER_IR_NONE };
        for (uint32_t k = 0; k < chunks; k += 1)
        {
            uint32_t pointer = base;
            if (k != 0)
            {
                pointer = at++;
                ir_sroa_widen_address(emit, pointer, base, 8 * (int64_t)k);
            }
            loads[k] = at++;
            ir_unroll_put(emit, loads[k], IR_LOAD, IR_I64, pointer, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t returned = at++;
        ir_unroll_put(emit, returned, IR_STACK_ADDR, IR_PTR, slot, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t k = 0; k < chunks; k += 1)
        {
            uint32_t pointer = returned;
            if (k != 0)
            {
                pointer = at++;
                ir_sroa_widen_address(emit, pointer, returned, 8 * (int64_t)k);
            }
            ir_unroll_put(emit, at++, IR_STORE, IR_VOID, pointer, loads[k], BUSTER_IR_NONE);
        }
        ir_unroll_put(emit, map[i], IR_RET, IR_VOID, returned, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    else
    {
        const bool load = kind == BUSTER_IR_SROA_WIDEN_LOAD;
        const uint32_t type = load ? f->type[i] : f->type[f->b[i]];
        const uint64_t offset = work->offsets[f->a[i]];
        const uint32_t bytes = ir_sroa_access_bytes(f, i);
        const uint32_t shift = (uint32_t)(offset & 7) * 8;
        const uint8_t flags = view->flags[map[i]];
        if (bytes == 8)
        {
            // The eightbyte's bits: an f64's or a ptr's converted to or from the i64.
            const uint32_t converted = at++;
            if (load)
            {
                ir_unroll_put(emit, converted, IR_LOAD, IR_I64, base, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_unroll_put(emit, map[i], type == IR_PTR ? IR_INTTOPTR : IR_BITCAST, type, converted, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            else
            {
                ir_unroll_put(emit, converted, type == IR_PTR ? IR_PTRTOINT : IR_BITCAST, IR_I64, map[f->b[i]], BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_unroll_put(emit, map[i], IR_STORE, IR_VOID, base, converted, BUSTER_IR_NONE);
            }
        }
        else
        {
            uint32_t pointer = base;
            if (shift != 0)
            {
                pointer = at++;
                ir_sroa_widen_address(emit, pointer, base, -(int64_t)(offset & 7));
            }
            const uint32_t word = at++;
            ir_unroll_put(emit, word, IR_LOAD, IR_I64, pointer, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint64_t field = (bytes == 8 ? UINT64_MAX : (UINT64_C(1) << (8 * bytes)) - 1) << shift;
            if (load)
            {
                uint32_t value = word;
                if (shift != 0)
                {
                    const uint32_t count = at++;
                    ir_unroll_put_constant(emit, count, IR_I64, shift);
                    value = at++;
                    ir_unroll_put(emit, value, IR_LSHR, IR_I64, word, count, BUSTER_IR_NONE);
                }
                if (type == IR_F32)
                {
                    const uint32_t truncated = at++;
                    ir_unroll_put(emit, truncated, IR_TRUNC, IR_I32, value, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    ir_unroll_put(emit, map[i], IR_BITCAST, IR_F32, truncated, BUSTER_IR_NONE, BUSTER_IR_NONE);
                }
                else
                {
                    ir_unroll_put(emit, map[i], IR_TRUNC, type, value, BUSTER_IR_NONE, BUSTER_IR_NONE);
                }
            }
            else
            {
                const uint32_t mask = at++;
                ir_unroll_put_constant(emit, mask, IR_I64, ~field);
                const uint32_t kept = at++;
                ir_unroll_put(emit, kept, IR_AND, IR_I64, word, mask, BUSTER_IR_NONE);
                uint32_t value = map[f->b[i]];
                if (type == IR_F32)
                {
                    const uint32_t bits = at++;
                    ir_unroll_put(emit, bits, IR_BITCAST, IR_I32, value, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    value = bits;
                }
                uint32_t placed = at++;
                ir_unroll_put(emit, placed, IR_ZEXT, IR_I64, value, BUSTER_IR_NONE, BUSTER_IR_NONE);
                if (shift != 0)
                {
                    const uint32_t count = at++;
                    ir_unroll_put_constant(emit, count, IR_I64, shift);
                    const uint32_t moved = at++;
                    ir_unroll_put(emit, moved, IR_SHL, IR_I64, placed, count, BUSTER_IR_NONE);
                    placed = moved;
                }
                const uint32_t merged = at++;
                ir_unroll_put(emit, merged, IR_OR, IR_I64, kept, placed, BUSTER_IR_NONE);
                ir_unroll_put(emit, map[i], IR_STORE, IR_VOID, pointer, merged, BUSTER_IR_NONE);
            }
        }
        // A plain access (ir_sroa_widen admits no other) keeps its flags, none.
        view->flags[map[i]] = flags;
    }
}

// #3311's rewrite of `function` (see above) into *result, in `scratch`, with the counts of the slots widened and the rets
// rewritten; *result is `function` itself where there is nothing to rewrite. Its analyses go to `output` and are gone
// after.
static IrCheck ir_sroa_widen(const IrFunction* function, Arena* output, Arena* scratch, IrSroaOptions options, IrFunction* result, uint32_t* widened_slots, uint32_t* widened_returns)
{
    *result = *function;
    *widened_slots = 0;
    *widened_returns = 0;
    const uint64_t om = output->length;
    IrSroaWork work = { 0 };
    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    const IrFunction* f = function;
    const bool eligible = options.flagged && f->debug == NULL && f->slot_count != 0 && output != scratch;
    if (eligible)
    {
        check = ir_optimize_prepare(&work.ir, f, output, scratch, BUSTER_SCALAR_PATH(options.scalar), options.simd);
    }
    uint8_t* kinds = NULL;
    uint8_t* widen = NULL;
    uint32_t chunks = 0;
    uint32_t rets = 0;
    uint64_t inserted = 0;
    uint64_t constants = 0;
    uint64_t addresses = 0;
    if (eligible && check.error == IR_OK)
    {
        bool ok = true;
        const uint64_t n = f->instruction_count;
        work.roots = ir_allocate(scratch, n, 4, &ok);
        work.offsets = ir_allocate(scratch, n, 8, &ok);
        work.refused = ir_allocate(scratch, f->slot_count, 1, &ok);
        work.returned = ir_allocate(scratch, f->slot_count, 1, &ok);
        work.returns = true;
        kinds = ir_allocate(scratch, n, 1, &ok);
        widen = ir_allocate(scratch, f->slot_count, 1, &ok);
        uint8_t* mixed = ir_allocate(scratch, f->slot_count, 1, &ok);
        // Each slot's bytes' first access: its offset and its type, and a bit that one was seen. Two accesses of different
        // offsets or types that share a byte are a slot's mixed widths.
        uint32_t* keys = ir_allocate(scratch, (uint64_t)f->slot_count * BUSTER_IR_SROA_WIDEN_BYTES, 4, &ok);
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
        if (ok)
        {
            memset(kinds, 0, n);
            memset(widen, 0, f->slot_count);
            memset(mixed, 0, f->slot_count);
            memset(keys, 0, (uint64_t)f->slot_count * BUSTER_IR_SROA_WIDEN_BYTES * 4);
            ir_sroa_provenance(&work, BUSTER_SCALAR_PATH(options.scalar), true);
            chunks = ir_sroa_widen_result(f);
            // Every access of a slot not refused, reachable or not (the split takes both), decides it; widen[] is 1 for a
            // slot an access refuses, until the decision below.
            for (uint32_t i = 0; i < f->instruction_count; i += 1)
            {
                const uint32_t op = f->op[i];
                const uint32_t root = (op == IR_LOAD || op == IR_STORE || op == IR_RET) && f->a[i] != BUSTER_IR_NONE ? work.roots[f->a[i]] : BUSTER_IR_NONE;
                if (root != BUSTER_IR_NONE && work.refused[root] == 0)
                {
                    const uint64_t size = f->slots[root].size;
                    const uint64_t offset = work.offsets[f->a[i]];
                    const bool reachable = work.ir.dominators.rpo_index[work.ir.cfg.instruction_blocks[i]] != BUSTER_DOMINATOR_NONE;
                    if (op == IR_RET)
                    {
                        // A reachable ret returns the slot's bytes from its start, which the result's eightbytes cover.
                        const bool returned = reachable && chunks != 0 && offset == 0 && size >= f->result_size && size <= BUSTER_IR_SROA_WIDEN_BYTES;
                        widen[root] |= !returned;
                        kinds[i] = returned ? BUSTER_IR_SROA_WIDEN_RET : BUSTER_IR_SROA_WIDEN_KEEP;
                    }
                    else
                    {
                        const uint32_t type = op == IR_LOAD ? f->type[i] : f->type[f->b[i]];
                        const uint32_t bytes = ir_sroa_access_bytes(f, i);
                        const bool fits = f->flags[i] == 0 && ir_sroa_widen_type(type) && size <= BUSTER_IR_SROA_WIDEN_BYTES && offset < size && bytes <= size - offset &&
                                          (offset & 7) + bytes <= 8;
                        widen[root] |= !fits;
                        if (fits)
                        {
                            const uint32_t key = UINT32_C(1) << 31 | (uint32_t)offset << 8 | type;
                            uint32_t* first = &keys[(uint64_t)root * BUSTER_IR_SROA_WIDEN_BYTES + offset];
                            for (uint32_t k = 0; k < bytes; k += 1)
                            {
                                mixed[root] |= first[k] != 0 && first[k] != key;
                                first[k] = first[k] == 0 ? key : first[k];
                            }
                            kinds[i] = op == IR_LOAD ? BUSTER_IR_SROA_WIDEN_LOAD : BUSTER_IR_SROA_WIDEN_STORE;
                        }
                    }
                }
            }
            // The decision: a slot no access refused, which has mixed widths or is returned.
            for (uint32_t slot = 0; slot < f->slot_count; slot += 1)
            {
                widen[slot] = work.refused[slot] == 0 && widen[slot] == 0 && (mixed[slot] != 0 || work.returned[slot] != 0);
                *widened_slots += widen[slot];
            }
            for (uint32_t i = 0; i < f->instruction_count; i += 1)
            {
                const uint32_t root = kinds[i] != BUSTER_IR_SROA_WIDEN_KEEP ? work.roots[f->a[i]] : BUSTER_IR_NONE;
                // An eightbyte's i64 is already the eightbyte's access.
                const bool whole = kinds[i] != BUSTER_IR_SROA_WIDEN_RET && root != BUSTER_IR_NONE && ir_sroa_access_bytes(f, i) == 8 &&
                                   (f->op[i] == IR_LOAD ? f->type[i] : f->type[f->b[i]]) == IR_I64;
                kinds[i] = root != BUSTER_IR_NONE && widen[root] != 0 && !whole ? kinds[i] : BUSTER_IR_SROA_WIDEN_KEEP;
                if (kinds[i] != BUSTER_IR_SROA_WIDEN_KEEP)
                {
                    const bool ret = kinds[i] == BUSTER_IR_SROA_WIDEN_RET;
                    const uint32_t type = ret ? IR_VOID : f->op[i] == IR_LOAD ? f->type[i] : f->type[f->b[i]];
                    const uint64_t offset = ret ? 0 : work.offsets[f->a[i]];
                    const uint32_t bytes = ret ? 0 : ir_sroa_access_bytes(f, i);
                    const bool shifted = !ret && bytes != 8 && (offset & 7) != 0;
                    inserted += ir_sroa_widen_inserted(kinds[i], type, offset, bytes, chunks);
                    rets += ret;
                    constants += ret ? 0 : (kinds[i] == BUSTER_IR_SROA_WIDEN_STORE && bytes != 8) + (shifted ? 1 : 0);
                    addresses += ret ? 2 * ((uint64_t)chunks - 1) : shifted;
                }
            }
        }
    }
    *widened_returns = rets;
    const bool rewrite = check.error == IR_OK && eligible && *widened_slots != 0;
    const uint64_t total = (uint64_t)f->instruction_count + inserted;
    if (rewrite && (total >= UINT32_MAX || (uint64_t)f->constant_count + constants > UINT32_MAX || (uint64_t)f->address_count + addresses > UINT32_MAX ||
                    (uint64_t)f->slot_count + 1 > UINT32_MAX))
    {
        check.error = IR_ERROR_LIMIT;
    }
    if (rewrite && check.error == IR_OK)
    {
        bool ok = true;
        const uint64_t extras = (uint64_t)f->extra_count + ir_unroll_extra_words(f, 0, f->instruction_count);
        uint32_t* map = ir_allocate(scratch, f->instruction_count, 4, &ok);
        IrFunction view = *f;
        view.instruction_count = (uint32_t)total;
        view.op = ir_allocate(scratch, total, 1, &ok);
        view.type = ir_allocate(scratch, total, 1, &ok);
        view.flags = ir_allocate(scratch, total, 1, &ok);
        view.alignment_log2 = ir_allocate(scratch, total, 1, &ok);
        view.a = ir_allocate(scratch, total, 4, &ok);
        view.b = ir_allocate(scratch, total, 4, &ok);
        view.c = ir_allocate(scratch, total, 4, &ok);
        view.block_offsets = ir_allocate(scratch, (uint64_t)f->block_count + 1, 4, &ok);
        view.extra = ir_allocate(scratch, extras, 4, &ok);
        view.calls = ir_allocate(scratch, f->call_count, sizeof(IrCall), &ok);
        view.constants = ir_allocate(scratch, (uint64_t)f->constant_count + constants, sizeof(IrConstant), &ok);
        view.addresses = ir_allocate(scratch, (uint64_t)f->address_count + addresses, sizeof(IrAddress), &ok);
        view.slot_count = f->slot_count + (rets != 0);
        view.slots = ir_allocate(scratch, view.slot_count, sizeof(IrSlot), &ok);
        check.error = ok && extras <= UINT32_MAX ? IR_OK : !ok ? IR_ERROR_MEMORY : IR_ERROR_LIMIT;
        if (check.error == IR_OK)
        {
            // Each original instruction's new index, after the instructions inserted before it in its block.
            uint32_t out = 0;
            for (uint32_t block = 0; block < f->block_count; block += 1)
            {
                view.block_offsets[block] = out;
                for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
                {
                    const bool ret = kinds[i] == BUSTER_IR_SROA_WIDEN_RET;
                    out += kinds[i] == BUSTER_IR_SROA_WIDEN_KEEP ? 0 :
                           ir_sroa_widen_inserted(kinds[i], ret ? IR_VOID : f->op[i] == IR_LOAD ? f->type[i] : f->type[f->b[i]], ret ? 0 : work.offsets[f->a[i]],
                                                  ret ? 0 : ir_sroa_access_bytes(f, i), chunks);
                    map[i] = out;
                    out += 1;
                }
            }
            view.block_offsets[f->block_count] = out;
            if (f->extra_count != 0)
            {
                memcpy(view.extra, f->extra, (uint64_t)f->extra_count * 4);
            }
            if (f->constant_count != 0)
            {
                memcpy(view.constants, f->constants, (uint64_t)f->constant_count * sizeof(IrConstant));
            }
            if (f->address_count != 0)
            {
                memcpy(view.addresses, f->addresses, (uint64_t)f->address_count * sizeof(IrAddress));
            }
            for (uint32_t slot = 0; slot < f->slot_count; slot += 1)
            {
                view.slots[slot] = f->slots[slot];
                view.slots[slot].size = widen[slot] != 0 ? (view.slots[slot].size + 7) & ~UINT64_C(7) : view.slots[slot].size;
            }
            const uint32_t slot = f->slot_count;
            if (rets != 0)
            {
                view.slots[slot] = (IrSlot){ .size = 8 * (uint64_t)chunks, .alignment = 8, .type = IR_VOID, .flags = BUSTER_IR_SLOT_TEMPORARY };
            }
            IrUnrollEmit emit = {
                .view = &view,
                .extra = f->extra_count,
                .call = 0,
                .constant = f->constant_count,
                .address = f->address_count,
            };
            // Runs of kept instructions are contiguous in the output; each rewritten one is emitted after its run.
            for (uint32_t block = 0; block < f->block_count; block += 1)
            {
                uint32_t first = f->block_offsets[block];
                const uint32_t end = f->block_offsets[(uint64_t)block + 1];
                for (uint32_t i = first; i <= end; i += 1)
                {
                    const bool kept = i < end && kinds[i] == BUSTER_IR_SROA_WIDEN_KEEP;
                    if (!kept && first < i)
                    {
                        ir_unroll_remap(f, first, i - first, map, &view, map[first]);
                        ir_unroll_extras(&emit, f, first, i, map[first], map, NULL);
                    }
                    if (!kept && i < end)
                    {
                        ir_unroll_remap(f, i, 1, map, &view, map[i]);
                        ir_sroa_widen_emit(f, &work, &emit, map, i, kinds[i], chunks, slot);
                    }
                    first = kept ? first : i + 1;
                }
            }
            view.extra_count = emit.extra;
            view.call_count = emit.call;
            view.constant_count = emit.constant;
            view.address_count = emit.address;
            view.arena_bytes = 0;
            *result = view;
        }
    }
    *widened_slots = rewrite && check.error == IR_OK ? *widened_slots : 0;
    *widened_returns = rewrite && check.error == IR_OK ? *widened_returns : 0;
    // A function the optimizer's preparation refuses (a SIMD one without `simd`) is the split's to refuse.
    check.error = check.error == IR_ERROR_UNSUPPORTED ? IR_OK : check.error;
    arena_pop_to(output, om);
    return check;
}

static __attribute__((unused)) IrCheck ir_optimize_sroa(const IrFunction* function, Arena* output, Arena* scratch, IrSroaOptions options, IrSroaResult* result)
{
    const uint64_t sm = scratch->length;
    IrFunction widened = { 0 };
    uint32_t widened_slots = 0;
    uint32_t widened_returns = 0;
    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    if (options.widen)
    {
        check = ir_sroa_widen(function, output, scratch, options, &widened, &widened_slots, &widened_returns);
    }
    if (check.error == IR_OK)
    {
        check = ir_sroa_split(widened_slots != 0 ? &widened : function, output, scratch, options, result);
    }
    if (check.error == IR_OK)
    {
        result->widened_slots = widened_slots;
        result->widened_returns = widened_returns;
    }
    arena_pop_to(scratch, sm);
    return check;
}

// #3311: a function's own by-value aggregates in registers as scalars. After SROA's widening, a register-class
// aggregate parameter's ptr (IrPassed.size, its copy's address) is only read by its eightbytes' loads, and a one-eightbyte
// result's ret only returns the slot one store of that eightbyte filled, yet each still went through memory: the entry
// stored the parameter's registers into its copy, and the return loaded the result from its slot. ir_sroa_scalarize
// makes them what the convention already passes, on a function's own code alone (a candidate's inline summary keeps
// its aggregates, which its callers' expansions copy):
// - a parameter in one or two registers, an eightbyte each (general or xmm, no flags), of at most 16 bytes, whose ptr
//   only block 0 reads, by plain loads of a part's offset (its own, or an IR_ADDRESS of offset 8 without an index), of at
//   most the part's 8 bytes (a general part's i8 to i64, ptr or f64; an xmm part's f64 or i64), becomes a scalar
//   parameter a part (an i64 in its general register, an f64 in its xmm one: records of no size). The ptr's node becomes
//   its first part's parameter and its first IR_ADDRESS its second's; each load is that parameter's value, copied,
//   truncated or converted, and any other IR_ADDRESS of it an undef;
// - a result of at most 8 bytes in rax or xmm0 whose every reachable ret returns a stack_addr of a slot nothing else
//   refers to, stored by one i64 store of the same block before the ret and of a value defined before the stack_addr,
//   becomes that i64 (or its bits as an f64 in xmm0), the stack_addr its copy (or bitcast) and the store an undef; an
//   unreachable ret's undefined ptr becomes an undefined i64 or f64.
// Promotion and the optimizer then remove what no longer has a use. Parameters are renumbered; the records stay the
// function's own. A function with debug information, a variadic one or one whose shapes differ is left as it is.
#define BUSTER_IR_SCALARIZE_NONE 0u
// Per node: the parameter part a ptr reaches (its parameter + 1 in the low 24 bits, its part above), or none.
#define BUSTER_IR_SCALARIZE_PART_SHIFT 24u

// The part of parameter record `record` (#3311) that holds byte offset `k * 8`: its register, or NONE where the record
// isn't a register class's eightbytes.
static uint8_t ir_sroa_scalar_register(const IrPassed* record, uint32_t k)
{
    const AbiLocation location = record->location;
    const bool eightbytes = record->size != 0 && record->size <= 16 && location.kind == BUSTER_ABI_LOCATION_REGISTERS && location.flags == 0 &&
                            location.registers[0] != BUSTER_ABI_REGISTER_NONE && (record->size <= 8) == (location.registers[1] == BUSTER_ABI_REGISTER_NONE);
    const uint8_t reg = eightbytes && k < 2 ? location.registers[k] : BUSTER_ABI_REGISTER_NONE;
    const bool usable = reg < 16 || (reg != BUSTER_ABI_REGISTER_NONE && (reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR && (reg & 0x1f) < 16);
    return usable ? reg : BUSTER_ABI_REGISTER_NONE;
}

// Whether a load of `type` reads a part in register `reg` as its value (#3311): see the header.
static bool ir_sroa_scalar_load(uint8_t reg, uint32_t type)
{
    const bool vector = (reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
    return vector ? type == IR_F64 || type == IR_I64 : type == IR_I8 || type == IR_I16 || type == IR_I32 || type == IR_I64 || type == IR_PTR || type == IR_F64;
}

// The conversion of a part's parameter value (an i64, or an f64 for an xmm part) to a load's `type`: its op.
static uint32_t ir_sroa_scalar_conversion(uint8_t reg, uint32_t type)
{
    const bool vector = (reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
    const uint32_t part = vector ? IR_F64 : IR_I64;
    return type == part ? IR_COPY : type == IR_PTR ? IR_INTTOPTR : type == IR_F64 || type == IR_I64 ? IR_BITCAST : IR_TRUNC;
}

// #3311's scalars (see above) of `function`, whose result goes to `output` as a whole new set of the columns it changes
// (the others the input's), into *result; with the counts of the parameters and results it made scalars. *result is
// `function` itself where it made none.
static __attribute__((unused)) IrError ir_sroa_scalarize(const IrFunction* function, Arena* output, Arena* scratch, IrFunction* result, uint32_t* parameters, uint32_t* results)
{
    const IrFunction* f = function;
    *result = *f;
    *parameters = 0;
    *results = 0;
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    const uint64_t n = f->instruction_count;
    const bool eligible = f->parameter_passed != 0 && f->debug == NULL && !f->variadic && f->block_count != 0;
    bool ok = true;
    uint32_t* parts = eligible ? ir_allocate(scratch, n, 4, &ok) : NULL;
    uint32_t* blocks = eligible ? ir_allocate(scratch, n, 4, &ok) : NULL;
    uint32_t* uses = eligible ? ir_allocate(scratch, n, 4, &ok) : NULL;
    uint8_t* refused = eligible ? ir_allocate(scratch, (uint64_t)f->parameter_count + 1, 1, &ok) : NULL;
    // Each parameter's part's host node (its ptr's, or its first IR_ADDRESS's), two a parameter.
    uint32_t* hosts = eligible ? ir_allocate(scratch, 2 * ((uint64_t)f->parameter_count + 1), 4, &ok) : NULL;
    // Each slot's stack_addrs (a lifetime marker counting past any), and those a ret's pattern takes.
    uint32_t* referenced = eligible ? ir_allocate(scratch, (uint64_t)f->slot_count + 1, 4, &ok) : NULL;
    uint32_t* matched = eligible ? ir_allocate(scratch, (uint64_t)f->slot_count + 1, 4, &ok) : NULL;
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    bool scalar_result = false;
    uint8_t result_reg = BUSTER_ABI_REGISTER_NONE;
    if (eligible && error == IR_OK)
    {
        memset(parts, 0, n * 4);
        memset(uses, 0, n * 4);
        memset(refused, 0, (uint64_t)f->parameter_count + 1);
        memset(hosts, 0xff, 2 * ((uint64_t)f->parameter_count + 1) * 4);
        memset(referenced, 0, ((uint64_t)f->slot_count + 1) * 4);
        memset(matched, 0, ((uint64_t)f->slot_count + 1) * 4);
        for (uint32_t block = 0; block < f->block_count; block += 1)
        {
            for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
            {
                blocks[i] = block;
            }
        }
        for (uint32_t p = 0; p < f->parameter_count; p += 1)
        {
            const IrPassed* record = &f->passed[f->parameter_passed - 1 + p];
            refused[p] = ir_sroa_scalar_register(record, 0) == BUSTER_ABI_REGISTER_NONE || (record->size > 8 && ir_sroa_scalar_register(record, 1) == BUSTER_ABI_REGISTER_NONE);
        }
        // The ptrs: a parameter's node (part 0) and an IR_ADDRESS of offset 8 of it (part 1), in block 0, which comes first.
        for (uint32_t i = 0; i < f->instruction_count; i += 1)
        {
            const uint32_t op = f->op[i];
            if (op == IR_PARAM && f->type[i] == IR_PTR && f->a[i] < f->parameter_count && refused[f->a[i]] == 0)
            {
                const uint32_t p = f->a[i];
                const bool first = blocks[i] == 0 && hosts[2 * p] == BUSTER_IR_NONE;
                refused[p] |= !first;
                hosts[2 * p] = first ? i : hosts[2 * p];
                parts[i] = p + 1;
            }
            else if (op == IR_ADDRESS && parts[f->a[i]] != BUSTER_IR_SCALARIZE_NONE && (parts[f->a[i]] >> BUSTER_IR_SCALARIZE_PART_SHIFT) == 0)
            {
                const uint32_t p = (parts[f->a[i]] & ((UINT32_C(1) << BUSTER_IR_SCALARIZE_PART_SHIFT) - 1)) - 1;
                const IrAddress address = f->addresses[f->c[i]];
                const bool second = blocks[i] == 0 && f->b[i] == BUSTER_IR_NONE && address.offset == 8 && f->passed[f->parameter_passed - 1 + p].size > 8;
                refused[p] |= !second;
                hosts[2 * p + 1] = second && hosts[2 * p + 1] == BUSTER_IR_NONE ? i : hosts[2 * p + 1];
                parts[i] = second ? (p + 1) | UINT32_C(1) << BUSTER_IR_SCALARIZE_PART_SHIFT : BUSTER_IR_SCALARIZE_NONE;
            }
        }
        // Every use of a part's ptr: a plain load in block 0 that its part's register holds (an IR_ADDRESS's, above).
        for (uint32_t i = 0; i < f->instruction_count; i += 1)
        {
            const uint64_t count = ir_operand_count(f, i);
            for (uint32_t number = 0; number < count; number += 1)
            {
                const IrOperand operand = ir_operand(f, i, number, blocks[i]);
                const uint32_t part = operand.value < n ? parts[operand.value] : BUSTER_IR_SCALARIZE_NONE;
                uses[operand.value < n ? operand.value : 0] += operand.value < n;
                if (part != BUSTER_IR_SCALARIZE_NONE)
                {
                    const uint32_t p = (part & ((UINT32_C(1) << BUSTER_IR_SCALARIZE_PART_SHIFT) - 1)) - 1;
                    const uint32_t k = part >> BUSTER_IR_SCALARIZE_PART_SHIFT;
                    const bool address = f->op[i] == IR_ADDRESS && k == 0 && parts[i] != BUSTER_IR_SCALARIZE_NONE;
                    const uint8_t reg = ir_sroa_scalar_register(&f->passed[f->parameter_passed - 1 + p], k);
                    const bool load = f->op[i] == IR_LOAD && operand.position == 0 && blocks[i] == 0 && f->flags[i] == 0 && ir_sroa_scalar_load(reg, f->type[i]);
                    refused[p] |= !address && !load;
                }
            }
        }
        // The result's rets.
        const AbiReturn abi = f->result_abi;
        result_reg = abi.registers[0];
        scalar_result = f->result_size != 0 && f->result_size <= 8 && f->result_type == IR_PTR && abi.kind == BUSTER_ABI_RESULT_REGISTERS && abi.flags == 0 &&
                        (result_reg == BUSTER_ABI_RAX || result_reg == BUSTER_ABI_REGISTER_VECTOR) && abi.registers[1] == BUSTER_ABI_REGISTER_NONE &&
                        abi.registers[2] == BUSTER_ABI_REGISTER_NONE && abi.registers[3] == BUSTER_ABI_REGISTER_NONE;
        for (uint32_t i = 0; scalar_result && i < f->instruction_count; i += 1)
        {
            if (f->op[i] == IR_STACK_ADDR || f->op[i] == IR_LIFETIME)
            {
                referenced[f->a[i]] += f->op[i] == IR_STACK_ADDR ? 1 : f->slot_count + 1;
            }
        }
        for (uint32_t i = 0; scalar_result && i < f->instruction_count; i += 1)
        {
            if (f->op[i] == IR_RET)
            {
                const uint32_t a = f->a[i];
                const bool undefined = a < n && f->op[a] == IR_UNDEF && uses[a] == 1;
                // The stack_addr's two uses: the store before the ret, and the ret.
                const bool slot = a < n && f->op[a] == IR_STACK_ADDR && uses[a] == 2 && blocks[a] == blocks[i] && a < i;
                bool stored = false;
                for (uint32_t s = a + 1; slot && s < i; s += 1)
                {
                    const uint32_t w = f->b[s];
                    stored |= f->op[s] == IR_STORE && f->a[s] == a && f->flags[s] == 0 && f->type[w] == IR_I64 && (blocks[w] != blocks[a] || w < a);
                }
                matched[stored ? f->a[a] : f->slot_count] += 1;
                scalar_result = undefined || stored;
            }
        }
        // Each returned slot's every stack_addr is a ret's.
        for (uint32_t slot = 0; scalar_result && slot < f->slot_count; slot += 1)
        {
            scalar_result = matched[slot] == 0 || matched[slot] == referenced[slot];
        }
    }
    for (uint32_t p = 0; eligible && error == IR_OK && p < f->parameter_count; p += 1)
    {
        *parameters += refused[p] == 0;
    }
    *results = eligible && error == IR_OK && scalar_result;
    if (error == IR_OK && (*parameters != 0 || *results != 0))
    {
        // The new parameters: a scalarized one's parts in order, every other as it was.
        uint64_t count = 0;
        for (uint32_t p = 0; p < f->parameter_count; p += 1)
        {
            count += refused[p] == 0 ? (f->passed[f->parameter_passed - 1 + p].size > 8 ? 2 : 1) : 1;
        }
        IrFunction view = *f;
        view.op = ir_allocate(output, n, 1, &ok);
        view.type = ir_allocate(output, n, 1, &ok);
        view.flags = ir_allocate(output, n, 1, &ok);
        view.alignment_log2 = ir_allocate(output, n, 1, &ok);
        view.a = ir_allocate(output, n, 4, &ok);
        view.b = ir_allocate(output, n, 4, &ok);
        view.c = ir_allocate(output, n, 4, &ok);
        view.parameter_types = ir_allocate(output, count, 1, &ok);
        view.passed = ir_allocate(output, (uint64_t)f->passed_count - f->parameter_count + count, sizeof(IrPassed), &ok);
        uint32_t* firsts = ir_allocate(scratch, (uint64_t)f->parameter_count + 1, 4, &ok);
        error = ok && count <= UINT32_MAX ? IR_OK : !ok ? IR_ERROR_MEMORY : IR_ERROR_LIMIT;
        if (error == IR_OK)
        {
            memcpy(view.op, f->op, n);
            memcpy(view.type, f->type, n);
            memcpy(view.flags, f->flags, n);
            memcpy(view.alignment_log2, f->alignment_log2, n);
            memcpy(view.a, f->a, n * 4);
            memcpy(view.b, f->b, n * 4);
            memcpy(view.c, f->c, n * 4);
            // The function's records in place of its old ones; the calls' records after them move by the difference.
            const uint32_t own = f->parameter_passed - 1;
            const uint32_t after = own + f->parameter_count;
            const uint32_t grown = (uint32_t)count - f->parameter_count;
            uint32_t out = 0;
            for (uint32_t k = 0; k < own; k += 1)
            {
                view.passed[out++] = f->passed[k];
            }
            for (uint32_t k = after; k < f->passed_count; k += 1)
            {
                view.passed[(uint64_t)k + grown] = f->passed[k];
            }
            IrAbiCall* calls = f->abi_call_count != 0 ? ir_allocate(output, f->abi_call_count, sizeof(IrAbiCall), &ok) : NULL;
            error = ok ? error : IR_ERROR_MEMORY;
            for (uint32_t k = 0; error == IR_OK && k < f->abi_call_count; k += 1)
            {
                calls[k] = f->abi_calls[k];
                calls[k].passed += f->abi_calls[k].passed >= after ? grown : 0;
            }
            view.abi_calls = f->abi_call_count != 0 ? calls : f->abi_calls;
            uint32_t next = 0;
            for (uint32_t p = 0; error == IR_OK && p < f->parameter_count; p += 1)
            {
                const IrPassed* record = &f->passed[own + p];
                firsts[p] = next;
                const uint32_t pieces = refused[p] == 0 ? (record->size > 8 ? 2 : 1) : 1;
                for (uint32_t k = 0; k < pieces; k += 1)
                {
                    const uint8_t reg = refused[p] == 0 ? ir_sroa_scalar_register(record, k) : BUSTER_ABI_REGISTER_NONE;
                    const bool vector = reg != BUSTER_ABI_REGISTER_NONE && (reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
                    view.parameter_types[next] = refused[p] == 0 ? (uint8_t)(vector ? IR_F64 : IR_I64) : f->parameter_types[p];
                    view.passed[out++] = refused[p] == 0 ? (IrPassed){ .location = { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { reg, BUSTER_ABI_REGISTER_NONE } }, .size = 0 } : *record;
                    next += 1;
                }
            }
            view.parameter_count = next;
            view.passed_count = f->passed_count + grown;
            for (uint32_t i = 0; error == IR_OK && i < f->instruction_count; i += 1)
            {
                const uint32_t op = f->op[i];
                const uint32_t part = parts[i];
                const uint32_t p = (part & ((UINT32_C(1) << BUSTER_IR_SCALARIZE_PART_SHIFT) - 1)) - 1;
                const uint32_t k = part >> BUSTER_IR_SCALARIZE_PART_SHIFT;
                if (op == IR_PARAM)
                {
                    view.a[i] = refused[f->a[i]] == 0 ? firsts[f->a[i]] : firsts[f->a[i]];
                    view.type[i] = refused[f->a[i]] == 0 ? view.parameter_types[firsts[f->a[i]]] : f->type[i];
                }
                else if (op == IR_ADDRESS && part != BUSTER_IR_SCALARIZE_NONE && refused[p] == 0)
                {
                    // Its part's parameter where it is the part's host, else an undef no load reads any more.
                    const bool host = hosts[2 * p + k] == i;
                    view.op[i] = host ? IR_PARAM : IR_UNDEF;
                    view.type[i] = host ? view.parameter_types[firsts[p] + k] : IR_PTR;
                    view.a[i] = host ? firsts[p] + k : BUSTER_IR_NONE;
                    view.b[i] = BUSTER_IR_NONE;
                    view.c[i] = BUSTER_IR_NONE;
                }
                else if (op == IR_LOAD && f->a[i] < n && parts[f->a[i]] != BUSTER_IR_SCALARIZE_NONE)
                {
                    const uint32_t from = parts[f->a[i]];
                    const uint32_t q = (from & ((UINT32_C(1) << BUSTER_IR_SCALARIZE_PART_SHIFT) - 1)) - 1;
                    const uint32_t j = from >> BUSTER_IR_SCALARIZE_PART_SHIFT;
                    if (refused[q] == 0)
                    {
                        const uint8_t reg = ir_sroa_scalar_register(&f->passed[own + q], j);
                        view.op[i] = (uint8_t)ir_sroa_scalar_conversion(reg, f->type[i]);
                        view.a[i] = hosts[2 * q + j];
                        view.b[i] = BUSTER_IR_NONE;
                        view.c[i] = BUSTER_IR_NONE;
                        view.flags[i] = 0;
                        view.alignment_log2[i] = 0;
                    }
                }
            }
            if (*results != 0)
            {
                const uint8_t type = result_reg == BUSTER_ABI_RAX ? IR_I64 : IR_F64;
                for (uint32_t i = 0; i < f->instruction_count; i += 1)
                {
                    if (f->op[i] == IR_RET)
                    {
                        const uint32_t a = f->a[i];
                        if (f->op[a] == IR_UNDEF)
                        {
                            view.type[a] = type;
                        }
                        else
                        {
                            // The last store of the slot before the ret is what it returns; any earlier one an undef too.
                            uint32_t value = BUSTER_IR_NONE;
                            for (uint32_t s = a + 1; s < i; s += 1)
                            {
                                if (f->op[s] == IR_STORE && f->a[s] == a)
                                {
                                    value = f->b[s];
                                    view.op[s] = IR_UNDEF;
                                    view.type[s] = IR_I64;
                                    view.a[s] = BUSTER_IR_NONE;
                                    view.b[s] = BUSTER_IR_NONE;
                                    view.c[s] = BUSTER_IR_NONE;
                                    view.flags[s] = 0;
                                    view.alignment_log2[s] = 0;
                                }
                            }
                            view.op[a] = type == IR_I64 ? IR_COPY : IR_BITCAST;
                            view.type[a] = type;
                            view.a[a] = value;
                            view.b[a] = BUSTER_IR_NONE;
                            view.c[a] = BUSTER_IR_NONE;
                        }
                    }
                }
                view.result_type = type;
                view.result_size = 0;
                // The slot the result was returned through has no reference left: it goes, and its frame with it.
                view.slots = ir_allocate(output, f->slot_count, sizeof(IrSlot), &ok);
                error = ok ? IR_OK : IR_ERROR_MEMORY;
                if (error == IR_OK && f->slot_count != 0)
                {
                    memcpy(view.slots, f->slots, (uint64_t)f->slot_count * sizeof(IrSlot));
                }
                error = error == IR_OK ? ir_sroa_compact_slots(&view, scratch) : error;
            }
            // A function left with no aggregate at all, whose parameters are each where its type's turn puts it (a
            // general register in order for an integer or a ptr, an xmm one for a float, none on the stack), needs no
            // records of its own: without them its frame has no rows for its parameters' copies.
            bool plain = error == IR_OK && view.result_size == 0 && view.result_abi.kind != BUSTER_ABI_RESULT_MEMORY && view.result_type != IR_F80;
            uint32_t general = 0;
            uint32_t vector = 0;
            for (uint32_t p = 0; plain && p < view.parameter_count; p += 1)
            {
                const IrPassed record = view.passed[view.parameter_passed - 1 + p];
                const uint32_t type = view.parameter_types[p];
                const bool floating = type == IR_F32 || type == IR_F64;
                const uint8_t reg = record.location.registers[0];
                const uint8_t expected = floating ? (vector < BUSTER_ABI_VECTOR_ARGUMENTS ? (uint8_t)(BUSTER_ABI_REGISTER_VECTOR | vector) : BUSTER_ABI_REGISTER_NONE) :
                                                    (general < BUSTER_ABI_INTEGER_ARGUMENTS ? abi_integer_arguments[general] : BUSTER_ABI_REGISTER_NONE);
                plain = record.size == 0 && record.location.kind == BUSTER_ABI_LOCATION_REGISTERS && (record.location.flags & ~(BUSTER_ABI_FLAG_SIGN | BUSTER_ABI_FLAG_ZERO)) == 0 &&
                        record.location.registers[1] == BUSTER_ABI_REGISTER_NONE && reg != BUSTER_ABI_REGISTER_NONE && reg == expected &&
                        (floating || type == IR_I32 || type == IR_I64 || type == IR_PTR);
                general += !floating;
                vector += floating;
            }
            view.parameter_passed = plain ? 0 : view.parameter_passed;
            *result = view;
        }
    }
    if (error != IR_OK || (*parameters == 0 && *results == 0))
    {
        *result = *f;
        *parameters = 0;
        *results = 0;
        arena_pop_to(output, om);
    }
    arena_pop_to(scratch, sm);
    return error;
}

// #3311's call sites: a call's by-value aggregate argument in one or two eightbyte registers (ir_sroa_scalar_register's
// shape: general or xmm, no flags, at most 16 bytes) was its object's ptr, which Tier 1 copied through the red zone into
// those registers, and which made the object escape, so that SROA left it in memory: a struct passed in a loop went
// through memory twice an iteration. ir_sroa_scalarize_calls, for a function's own code alone (a candidate's inline
// summary keeps its calls' aggregates, which an expansion maps to its callee's parameters), passes such an argument as
// its parts instead: before the call, each part's eightbyte loaded from the ptr (an i64 for a general register, an f64
// for an xmm one, the second through an IR_ADDRESS of offset 8), and the call takes those values, with records of no
// size in the part's register. The object's only use is then those loads, which SROA's widening and promotion make
// values. A variadic call, a row's or an asm body's, and an argument of any other shape stay as they are. The call's
// argument list, its parameter types and its records are new slices; the inserted loads go before the call in its
// block, with the unroller's remapping, as ir_sroa_widen's do.

// The parts of call argument `argument`'s record that ir_sroa_scalarize_calls passes as scalars: 1 or 2, or 0.
static uint32_t ir_sroa_call_parts(const IrFunction* f, const IrCall* call, uint32_t argument)
{
    const IrPassed* record = call->abi != 0 && !call->variadic && call->row == 0 && call->body == 0 && argument < call->parameter_count ?
                                 &f->passed[f->abi_calls[call->abi - 1].passed + argument] : NULL;
    const uint32_t value = record != NULL ? f->extra[call->argument_first + argument] : BUSTER_IR_NONE;
    const bool first = record != NULL && value < f->instruction_count && f->type[value] == IR_PTR && ir_sroa_scalar_register(record, 0) != BUSTER_ABI_REGISTER_NONE;
    const bool second = first && record->size > 8;
    return first && (!second || ir_sroa_scalar_register(record, 1) != BUSTER_ABI_REGISTER_NONE) ? (second ? 2u : 1u) : 0u;
}

// The scalar type a call's aggregate result of one part comes back as (#3311): an integer of its 1, 2, 4 or 8 bytes in
// rax, or an f32 or f64 of its 4 or 8 in xmm0; IR_VOID where it isn't such a result. The call then returns that value,
// and a store of it after the call fills the object its `into` operand points to, which is then private.
static uint32_t ir_sroa_call_result(const IrFunction* f, const IrCall* call)
{
    const IrAbiCall* abi = call->abi != 0 && call->row == 0 && call->body == 0 ? &f->abi_calls[call->abi - 1] : NULL;
    const AbiReturn result = abi != NULL ? abi->call.result : (AbiReturn){ 0 };
    const uint32_t size = abi != NULL ? abi->result_size : 0;
    const bool one = abi != NULL && size != 0 && result.kind == BUSTER_ABI_RESULT_REGISTERS && result.flags == 0 && result.registers[1] == BUSTER_ABI_REGISTER_NONE &&
                     result.registers[2] == BUSTER_ABI_REGISTER_NONE && result.registers[3] == BUSTER_ABI_REGISTER_NONE && call->result_type == IR_VOID;
    const bool general = one && result.registers[0] == BUSTER_ABI_RAX;
    const bool vector = one && result.registers[0] == BUSTER_ABI_REGISTER_VECTOR;
    return general ? (size == 8 ? IR_I64 : size == 4 ? IR_I32 : size == 2 ? IR_I16 : size == 1 ? IR_I8 : IR_VOID) :
           vector  ? (size == 8 ? IR_F64 : size == 4 ? IR_F32 : IR_VOID) :
                     IR_VOID;
}

static __attribute__((unused)) IrError ir_sroa_scalarize_calls(const IrFunction* function, Arena* output, Arena* scratch, IrFunction* result, uint32_t* arguments)
{
    const IrFunction* f = function;
    *result = *f;
    *arguments = 0;
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    // The instructions inserted before each call, and the totals.
    uint64_t inserted = 0;
    uint64_t addresses = 0;
    uint64_t extra_words = 0;
    uint64_t type_words = 0;
    uint64_t passed_words = 0;
    const bool eligible = f->abi_call_count != 0 && f->debug == NULL;
    for (uint32_t i = 0; eligible && i < f->instruction_count; i += 1)
    {
        if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
        {
            const IrCall* call = &f->calls[f->b[i]];
            uint32_t parts = 0;
            uint32_t taken = 0;
            for (uint32_t k = 0; k < call->argument_count; k += 1)
            {
                const uint32_t count = ir_sroa_call_parts(f, call, k);
                parts += count;
                taken += count != 0;
                inserted += count == 0 ? 0 : 2 * (uint64_t)count - 1;
                addresses += count == 2;
            }
            const bool returned = ir_sroa_call_result(f, call) != IR_VOID;
            inserted += returned;
            *arguments += taken + returned;
            const uint64_t grown = (uint64_t)call->argument_count + parts - taken;
            const bool changed = taken != 0 || returned;
            extra_words += changed ? grown : 0;
            type_words += changed ? grown : 0;
            passed_words += changed ? grown : 0;
        }
    }
    bool ok = true;
    IrError error = IR_OK;
    if (*arguments != 0)
    {
        const uint64_t n = f->instruction_count;
        const uint64_t total = n + inserted;
        const uint64_t extras = (uint64_t)f->extra_count + ir_unroll_extra_words(f, 0, f->instruction_count) + extra_words;
        uint32_t* map = ir_allocate(scratch, n, 4, &ok);
        uint32_t* before = ir_allocate(scratch, n, 4, &ok);
        IrFunction view = *f;
        view.instruction_count = (uint32_t)total;
        view.op = ir_allocate(output, total, 1, &ok);
        view.type = ir_allocate(output, total, 1, &ok);
        view.flags = ir_allocate(output, total, 1, &ok);
        view.alignment_log2 = ir_allocate(output, total, 1, &ok);
        view.a = ir_allocate(output, total, 4, &ok);
        view.b = ir_allocate(output, total, 4, &ok);
        view.c = ir_allocate(output, total, 4, &ok);
        view.block_offsets = ir_allocate(output, (uint64_t)f->block_count + 1, 4, &ok);
        view.extra = ir_allocate(output, extras, 4, &ok);
        view.calls = ir_allocate(output, f->call_count, sizeof(IrCall), &ok);
        view.addresses = ir_allocate(output, (uint64_t)f->address_count + addresses, sizeof(IrAddress), &ok);
        view.call_types = ir_allocate(output, (uint64_t)f->call_type_count + type_words, 1, &ok);
        view.passed = ir_allocate(output, (uint64_t)f->passed_count + passed_words, sizeof(IrPassed), &ok);
        view.abi_calls = ir_allocate(output, f->abi_call_count, sizeof(IrAbiCall), &ok);
        const bool fits = total < UINT32_MAX && extras <= UINT32_MAX && (uint64_t)f->address_count + addresses <= UINT32_MAX &&
                          (uint64_t)f->call_type_count + type_words <= UINT32_MAX && (uint64_t)f->passed_count + passed_words <= UINT32_MAX;
        error = !ok ? IR_ERROR_MEMORY : !fits ? IR_ERROR_LIMIT : IR_OK;
        if (error == IR_OK)
        {
            uint32_t out = 0;
            for (uint32_t block = 0; block < f->block_count; block += 1)
            {
                view.block_offsets[block] = out;
                for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
                {
                    uint32_t count = 0;
                    if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
                    {
                        const IrCall* call = &f->calls[f->b[i]];
                        for (uint32_t k = 0; k < call->argument_count; k += 1)
                        {
                            const uint32_t parts = ir_sroa_call_parts(f, call, k);
                            count += parts == 0 ? 0 : 2 * parts - 1;
                        }
                    }
                    // A one-part result's store goes after the call.
                    const bool returned = (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT) && ir_sroa_call_result(f, &f->calls[f->b[i]]) != IR_VOID;
                    before[i] = count + returned;
                    out += count;
                    map[i] = out;
                    out += 1 + returned;
                }
            }
            view.block_offsets[f->block_count] = out;
            if (f->extra_count != 0)
            {
                memcpy(view.extra, f->extra, (uint64_t)f->extra_count * 4);
            }
            if (f->address_count != 0)
            {
                memcpy(view.addresses, f->addresses, (uint64_t)f->address_count * sizeof(IrAddress));
            }
            if (f->call_type_count != 0)
            {
                memcpy(view.call_types, f->call_types, f->call_type_count);
            }
            if (f->passed_count != 0)
            {
                memcpy(view.passed, f->passed, (uint64_t)f->passed_count * sizeof(IrPassed));
            }
            memcpy(view.abi_calls, f->abi_calls, (uint64_t)f->abi_call_count * sizeof(IrAbiCall));
            IrUnrollEmit emit = {
                .view = &view,
                .extra = f->extra_count,
                .call = 0,
                .constant = f->constant_count,
                .address = f->address_count,
            };
            uint32_t types = f->call_type_count;
            uint32_t records = f->passed_count;
            for (uint32_t block = 0; block < f->block_count; block += 1)
            {
                uint32_t first = f->block_offsets[block];
                const uint32_t end = f->block_offsets[(uint64_t)block + 1];
                for (uint32_t i = first; i <= end; i += 1)
                {
                    const bool kept = i < end && before[i] == 0;
                    if (!kept && first < i)
                    {
                        ir_unroll_remap(f, first, i - first, map, &view, map[first]);
                        ir_unroll_extras(&emit, f, first, i, map[first], map, NULL);
                    }
                    if (!kept && i < end)
                    {
                        // The call's parts' loads, then the call with its new argument, type and record slices.
                        const IrCall* call = &f->calls[f->b[i]];
                        const IrAbiCall* abi = &f->abi_calls[call->abi - 1];
                        ir_unroll_remap(f, i, 1, map, &view, map[i]);
                        const uint32_t returned = ir_sroa_call_result(f, call);
                        uint32_t at = map[i] - (before[i] - (returned != IR_VOID));
                        IrCall rewritten = *call;
                        rewritten.argument_first = emit.extra;
                        rewritten.type_first = types;
                        view.abi_calls[call->abi - 1].passed = records;
                        uint32_t count = 0;
                        for (uint32_t k = 0; k < call->argument_count; k += 1)
                        {
                            const uint32_t parts = ir_sroa_call_parts(f, call, k);
                            const uint32_t value = map[f->extra[call->argument_first + k]];
                            const IrPassed* record = &f->passed[abi->passed + k];
                            for (uint32_t j = 0; j < parts; j += 1)
                            {
                                const uint8_t reg = ir_sroa_scalar_register(record, j);
                                const uint32_t type = (reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR ? IR_F64 : IR_I64;
                                uint32_t pointer = value;
                                if (j != 0)
                                {
                                    pointer = at++;
                                    ir_sroa_widen_address(&emit, pointer, value, 8);
                                }
                                const uint32_t loaded = at++;
                                ir_unroll_put(&emit, loaded, IR_LOAD, type, pointer, BUSTER_IR_NONE, BUSTER_IR_NONE);
                                view.extra[emit.extra++] = loaded;
                                view.call_types[types++] = (uint8_t)type;
                                view.passed[records++] = (IrPassed){ .location = { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { reg, BUSTER_ABI_REGISTER_NONE } }, .size = 0 };
                                count += 1;
                            }
                            if (parts == 0)
                            {
                                view.extra[emit.extra++] = value;
                                view.call_types[types++] = f->call_types[call->type_first + k];
                                view.passed[records++] = *record;
                                count += 1;
                            }
                        }
                        rewritten.argument_count = count;
                        rewritten.parameter_count = count;
                        if (returned != IR_VOID)
                        {
                            // The call returns the part's value, which a store after it puts where `into` points.
                            rewritten.result_type = (uint8_t)returned;
                            view.abi_calls[call->abi - 1].result_size = 0;
                            view.type[map[i]] = (uint8_t)returned;
                            view.c[map[i]] = BUSTER_IR_NONE;
                            ir_unroll_put(&emit, map[i] + 1, IR_STORE, IR_VOID, map[f->c[i]], map[i], BUSTER_IR_NONE);
                        }
                        view.calls[emit.call] = rewritten;
                        view.b[map[i]] = emit.call;
                        emit.call += 1;
                    }
                    first = kept ? first : i + 1;
                }
            }
            view.extra_count = emit.extra;
            view.call_count = emit.call;
            view.address_count = emit.address;
            view.call_type_count = types;
            view.passed_count = records;
            view.arena_bytes = 0;
            *result = view;
        }
    }
    if (error != IR_OK)
    {
        *result = *f;
        *arguments = 0;
        arena_pop_to(output, om);
    }
    arena_pop_to(scratch, sm);
    return error;
}
