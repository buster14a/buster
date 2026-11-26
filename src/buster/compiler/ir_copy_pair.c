// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Late pairing of a local COPY's surviving i64 pieces (#2818). Include after ir.c. The scalar accesses stay visible
// to SROA and promotion; only afterwards do two loads and their stores become one V4I32 load/store. Selection reads
// that marked vector in two 8-byte halves, then stores its 16 bytes whole. The removed rows become unused UNDEFs,
// which the following optimizer drops. No live use lists or verified-analysis revision may span this in-place pass.
//
// COPY_PIECE is a candidate hint, never an alias certificate: forwarding can connect different original copies.
// The current addresses must prove equal or disjoint 16-byte ranges, with an ordinary current-frame slot as source.
// A distinct slot, a symbol, and an incoming parameter cannot refer to that slot. Inlined parameters have already
// become their caller's actual values, so their aliases are checked as those values. An address loaded from memory
// may capture an escaped slot and is deliberately unknown. Bounds are checked for every named slot.
//
// Opcode/flag screens and ordered event windows use 16 bounded lanes, including partial chunks and block tails.
// Only candidate functions allocate scratch: one u32 use count per instruction, populated by ir_uses_batch. Each
// address walk has at most 32 links. Input columns have no padding and are exclusively owned; scratch is disjoint,
// restored on every exit. Allocation/operand-count failure precedes any rewrite. Debug scalar bindings count as uses.

#ifndef BUSTER_IR_COPY_PAIR_C
#define BUSTER_IR_COPY_PAIR_C

#define BUSTER_IR_COPY_PAIR_LINKS 32u

typedef struct IrCopyAddress IrCopyAddress;
struct IrCopyAddress
{
    uint32_t kind;
    uint32_t root;
    int64_t offset;
};

static bool ir_copy_pair_constant(const IrFunction* f, uint32_t node, int64_t* result)
{
    for (uint32_t link = 0; link < BUSTER_IR_COPY_PAIR_LINKS && f->op[node] == IR_COPY; link += 1)
    {
        node = f->a[node];
    }
    const bool constant = f->op[node] == IR_CONST && f->type[node] == IR_I64;
    if (constant)
    {
        *result = (int64_t)f->constants[f->a[node]].low;
    }
    return constant;
}

static IrCopyAddress ir_copy_pair_address(const IrFunction* f, uint32_t node)
{
    IrCopyAddress result = { 0 };
    int64_t offset = 0;
    bool valid = true;
    for (uint32_t link = 0; valid && link < BUSTER_IR_COPY_PAIR_LINKS; link += 1)
    {
        const uint32_t op = f->op[node];
        const uint32_t type = f->type[node];
        const uint32_t a = f->a[node];
        const uint32_t b = f->b[node];
        if (op == IR_STACK_ADDR || op == IR_PARAM || op == IR_SYMBOL_ADDR)
        {
            result = (IrCopyAddress){ .kind = op, .root = a, .offset = offset };
            break;
        }
        if ((op == IR_COPY || op == IR_BITCAST || op == IR_INTTOPTR || op == IR_PTRTOINT) &&
            (type == IR_I64 || type == IR_PTR) && (f->type[a] == IR_I64 || f->type[a] == IR_PTR))
        {
            node = a;
        }
        else if (op == IR_ADDRESS)
        {
            const IrAddress address = f->addresses[f->c[node]];
            int64_t index = 0;
            int64_t scaled = 0;
            valid = (b == BUSTER_IR_NONE || ir_copy_pair_constant(f, b, &index)) &&
                    !__builtin_mul_overflow(index, (int64_t)address.scale, &scaled) &&
                    !__builtin_add_overflow(offset, address.offset, &offset) && !__builtin_add_overflow(offset, scaled, &offset);
            node = a;
        }
        else if ((op == IR_ADD || op == IR_SUB) && type == IR_I64)
        {
            int64_t delta = 0;
            const bool right = ir_copy_pair_constant(f, b, &delta);
            valid = right || (op == IR_ADD && ir_copy_pair_constant(f, a, &delta));
            valid = valid && !(op == IR_SUB ? __builtin_sub_overflow(offset, delta, &offset) : __builtin_add_overflow(offset, delta, &offset));
            node = right ? a : b;
        }
        else
        {
            valid = false;
        }
    }
    return result;
}

static bool ir_copy_pair_slot(const IrFunction* f, IrCopyAddress address)
{
    bool valid = address.kind == IR_STACK_ADDR && address.root < f->slot_count && address.offset >= 0;
    if (valid)
    {
        const IrSlot slot = f->slots[address.root];
        const uint32_t refused = BUSTER_IR_SLOT_VOLATILE | BUSTER_IR_SLOT_REGISTER_SAVE_AREA | BUSTER_IR_SLOT_OVERFLOW_AREA;
        valid = (slot.flags & refused) == 0 && slot.size >= 16 && (uint64_t)address.offset <= slot.size - 16;
    }
    return valid;
}

static bool ir_copy_pair_next(IrCopyAddress first, IrCopyAddress second)
{
    return first.kind != 0 && first.kind == second.kind && first.root == second.root && first.offset <= INT64_MAX - 8 && second.offset == first.offset + 8;
}

static bool ir_copy_pair_ranges(const IrFunction* f, const uint32_t rows[4])
{
    const IrCopyAddress source = ir_copy_pair_address(f, f->a[rows[0]]);
    const IrCopyAddress destination = ir_copy_pair_address(f, f->a[rows[1]]);
    bool safe = ir_copy_pair_slot(f, source) && ir_copy_pair_next(source, ir_copy_pair_address(f, f->a[rows[2]])) &&
                ir_copy_pair_next(destination, ir_copy_pair_address(f, f->a[rows[3]]));
    if (safe && destination.kind == IR_STACK_ADDR)
    {
        safe = ir_copy_pair_slot(f, destination) && (source.root != destination.root || source.offset == destination.offset ||
               (source.offset < destination.offset ? destination.offset - source.offset >= 16 : source.offset - destination.offset >= 16));
    }
    else
    {
        safe = safe && (destination.kind == IR_PARAM || destination.kind == IR_SYMBOL_ADDR);
    }
    return safe;
}

// Pure address/value definitions may sit between COPY's memory events. Every other instruction breaks the window:
// another access, call, fence, lifetime marker, control flow, or potentially trapping/effectful arithmetic.
static const uint8_t ir_copy_pair_transparent[BUSTER_IR_OP_TABLE] = {
    [IR_CONST] = 1,
    [IR_UNDEF] = 1,
    [IR_PARAM] = 1,
    [IR_COPY] = 1,
    [IR_ADD] = 1,
    [IR_SUB] = 1,
    [IR_MUL] = 1,
    [IR_INTTOPTR] = 1,
    [IR_PTRTOINT] = 1,
    [IR_BITCAST] = 1,
    [IR_STACK_ADDR] = 1,
    [IR_SYMBOL_ADDR] = 1,
    [IR_ADDRESS] = 1,
};

static void ir_copy_pair_dead(IrFunction* f, uint32_t node)
{
    f->op[node] = IR_UNDEF;
    f->type[node] = IR_I64;
    f->flags[node] = 0;
    f->alignment_log2[node] = 0;
    f->a[node] = BUSTER_IR_NONE;
    f->b[node] = BUSTER_IR_NONE;
    f->c[node] = BUSTER_IR_NONE;
}

// The common unmarked function reads flags alone. Cached summaries use the same screen before copying their columns.
static bool ir_copy_pair_candidates(const IrFunction* f)
{
    const uint32_t count = f->instruction_count;
    uint32_t candidates = 0;
    for (uint64_t first = 0; first < count && candidates < 2; first += BUSTER_IR_VECTOR_COUNT)
    {
        const uint32_t lanes = count - first < BUSTER_IR_VECTOR_COUNT ? (uint32_t)(count - first) : BUSTER_IR_VECTOR_COUNT;
        const __mmask16 active = (__mmask16)((1u << lanes) - 1);
        const __m128i flags = _mm_maskz_loadu_epi8(active, f->flags + first);
        const __mmask16 marked = active & _mm_cmpeq_epi8_mask(flags, _mm_set1_epi8((char)BUSTER_IR_COPY_PIECE));
        if (marked != 0)
        {
            const __m128i op = _mm_maskz_loadu_epi8(marked, f->op + first);
            const __m128i types = _mm_maskz_loadu_epi8(marked, f->type + first);
            const __mmask16 loads = marked & _mm_cmpeq_epi8_mask(op, _mm_set1_epi8(IR_LOAD)) & _mm_cmpeq_epi8_mask(types, _mm_set1_epi8(IR_I64));
            candidates += (uint32_t)__builtin_popcount(loads);
        }
    }
    return candidates >= 2;
}

static __attribute__((unused)) IrError ir_copy_pair(IrFunction* f, Arena* scratch, uint32_t* paired)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = f->instruction_count;
    IrError error = IR_OK;
    uint32_t pairs = 0;
    if (ir_copy_pair_candidates(f))
    {
        bool allocated = true;
        uint32_t* const uses = ir_allocate(scratch, count, sizeof(uint32_t), &allocated);
        error = allocated ? IR_OK : IR_ERROR_MEMORY;
        uint64_t total = 0;
        for (uint64_t first = 0; error == IR_OK && first < count; first += BUSTER_IR_VECTOR_COUNT)
        {
            const uint32_t lanes = count - first < BUSTER_IR_VECTOR_COUNT ? (uint32_t)(count - first) : BUSTER_IR_VECTOR_COUNT;
            total += ir_uses_batch(f, (uint32_t)first, lanes, uses, NULL);
            error = total <= UINT32_MAX ? IR_OK : IR_ERROR_LIMIT;
        }
        // A scalar debugger binding must not silently become a vector value or an undef. Its anchor is a use too.
        const IrDebugInfo* const debug = f->debug;
        for (uint32_t k = 0; error == IR_OK && debug != NULL && k < debug->binding_count; k += 1)
        {
            const IrDebugBinding binding = debug->bindings[k];
            if (binding.kind == IR_DEBUG_BIND_VALUE)
            {
                uses[binding.value] = 2;
            }
            if (binding.anchor != BUSTER_IR_NONE)
            {
                uses[binding.anchor] = 2;
            }
        }
        for (uint32_t k = 0; error == IR_OK && debug != NULL && k < debug->region_count; k += 1)
        {
            const IrDebugRegion region = debug->regions[k];
            if (region.kind == IR_DEBUG_BIND_VALUE)
            {
                uses[region.value] = 2;
            }
        }
        for (uint32_t block = 0; error == IR_OK && block < f->block_count; block += 1)
        {
            uint32_t rows[4] = { 0 };
            uint32_t pending = 0;
            const uint32_t end = f->block_offsets[(uint64_t)block + 1];
            for (uint64_t first = f->block_offsets[block]; first < end; first += BUSTER_IR_VECTOR_COUNT)
            {
                const uint32_t lanes = end - first < BUSTER_IR_VECTOR_COUNT ? (uint32_t)(end - first) : BUSTER_IR_VECTOR_COUNT;
                const __mmask16 active = (__mmask16)((1u << lanes) - 1);
                const __m128i op = _mm_maskz_loadu_epi8(active, f->op + first);
                const __m128i flags = _mm_maskz_loadu_epi8(active, f->flags + first);
                const __mmask16 marked = _mm_cmpeq_epi8_mask(flags, _mm_set1_epi8((char)BUSTER_IR_COPY_PIECE));
                const __mmask16 loads = marked & _mm_cmpeq_epi8_mask(op, _mm_set1_epi8(IR_LOAD));
                const __mmask16 stores = marked & _mm_cmpeq_epi8_mask(op, _mm_set1_epi8(IR_STORE));
                uint32_t events = active & ~_mm512_test_epi32_mask(ir_lookup_bytes(ir_copy_pair_transparent, op), _mm512_set1_epi32(1));
                while (events != 0)
                {
                    const uint32_t lane = (uint32_t)__builtin_ctz(events);
                    const uint32_t node = (uint32_t)first + lane;
                    const bool load = ((loads >> lane) & 1) != 0 && f->type[node] == IR_I64 && uses[node] == 1;
                    const bool store = ((stores >> lane) & 1) != 0 && uses[node] == 0 && pending != 0 && f->b[node] == rows[pending - 1];
                    if ((pending & 1) != 0 ? store : load)
                    {
                        rows[pending++] = node;
                        if (pending == 4)
                        {
                            if (ir_copy_pair_ranges(f, rows))
                            {
                                f->type[rows[0]] = IR_V4I32;
                                ir_copy_pair_dead(f, rows[2]);
                                ir_copy_pair_dead(f, rows[3]);
                                pairs += 1;
                                pending = 0;
                            }
                            else
                            {
                                rows[0] = rows[2];
                                rows[1] = rows[3];
                                pending = 2;
                            }
                        }
                    }
                    else
                    {
                        rows[0] = node;
                        pending = load ? 1 : 0;
                    }
                    events &= events - 1;
                }
            }
        }
    }
    arena_pop_to(scratch, mark);
    *paired = pairs;
    return error;
}

#endif
