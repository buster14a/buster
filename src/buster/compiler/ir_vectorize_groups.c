// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Shared loads for the loop vectorizer's small, positive strides (#2808). Included by ir_vectorize_loops.c after its
// dependence analysis. ir_vectorize_group_plan keeps at most eight groups of eight loads, each in a store-free region.
// Constant-offset normalization proposes members; a relation across a narrow extension also needs G's equality test
// at the initial induction. The existing no-wrap guards then prove that relation throughout the vector iterations.
// A group never extends beyond its lowest member's first byte or its highest member's last byte.

#ifndef BUSTER_IR_VECTORIZE_GROUPS_C
#define BUSTER_IR_VECTORIZE_GROUPS_C

typedef struct IrVectorizeGroupKey IrVectorizeGroupKey;
struct IrVectorizeGroupKey
{
    uint32_t base;
    uint32_t index;
    uint64_t scale;
    uint64_t offset;
    bool guarded;
};

// Remove exact constant offsets from a pointer's chain. Arithmetic is modulo 2^64, as IR addresses are. A nonconstant
// index or addend stays in the key; it may be shared only when the structural comparison proves it is the same value.
static uint32_t ir_vectorize_group_pointer(const IrFunction* f, uint32_t value, uint64_t* offset)
{
    uint32_t at = ir_vectorize_root(f, value);
    bool more = true;
    for (uint32_t depth = 0; more && at < f->instruction_count && depth < BUSTER_IR_VECTORIZE_DEPTH; depth += 1)
    {
        const uint32_t op = f->op[at];
        uint64_t constant = 0;
        more = false;
        if (op == IR_ADDRESS)
        {
            const IrAddress address = f->addresses[f->c[at]];
            const uint32_t index = f->b[at];
            if (index == BUSTER_IR_NONE || ir_vectorize_constant(f, index, &constant))
            {
                *offset += (uint64_t)address.offset + constant * address.scale;
                at = ir_vectorize_root(f, f->a[at]);
                more = true;
            }
        }
        else if ((op == IR_ADD || op == IR_SUB) && f->type[at] == IR_I64)
        {
            const uint32_t a = f->a[at];
            const uint32_t b = f->b[at];
            if (ir_vectorize_constant(f, b, &constant))
            {
                *offset += op == IR_ADD ? constant : 0 - constant;
                at = ir_vectorize_root(f, a);
                more = true;
            }
            else if (op == IR_ADD && ir_vectorize_constant(f, a, &constant))
            {
                *offset += constant;
                at = ir_vectorize_root(f, b);
                more = true;
            }
        }
    }
    return at;
}

// Normalize an index as scale * core + offset. Distributing through i32 arithmetic or its extension is a candidate
// equality, not an unconditional identity: G must compare the original pointers. Each admitted pointer separately
// advances by its classified coefficient, under the existing narrow-index guards, so equality at the start suffices.
static void ir_vectorize_group_index(const IrFunction* f, uint32_t value, IrVectorizeGroupKey* key)
{
    uint32_t at = ir_vectorize_root(f, value);
    uint64_t scale = key->scale;
    uint64_t offset = key->offset;
    bool guarded = key->guarded;
    bool more = true;
    for (uint32_t depth = 0; more && at < f->instruction_count && depth < BUSTER_IR_VECTORIZE_DEPTH; depth += 1)
    {
        const uint32_t op = f->op[at];
        const uint32_t type = f->type[at];
        more = false;
        if ((op == IR_SEXT || op == IR_ZEXT) && type == IR_I64 && f->type[f->a[at]] == IR_I32)
        {
            guarded = true;
            at = ir_vectorize_root(f, f->a[at]);
            more = true;
        }
        else if ((type == IR_I64 || type == IR_I32) && (op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_SHL))
        {
            const uint32_t a = f->a[at];
            const uint32_t b = f->b[at];
            uint64_t bits = 0;
            const bool right = ir_vectorize_constant(f, b, &bits);
            const bool left = !right && op != IR_SHL && ir_vectorize_constant(f, a, &bits);
            const uint64_t constant = type == IR_I32 ? (uint64_t)(int64_t)(int32_t)(uint32_t)bits : bits;
            if ((right || left) && op != IR_SHL)
            {
                if (op == IR_ADD)
                {
                    offset += scale * constant;
                }
                else if (op == IR_SUB)
                {
                    offset += right ? 0 - scale * constant : scale * constant;
                    scale = right ? scale : 0 - scale;
                }
                else
                {
                    scale *= constant;
                }
                at = ir_vectorize_root(f, right ? a : b);
                guarded = guarded || type == IR_I32;
                more = true;
            }
            else if (right && bits < (type == IR_I32 ? 32u : 64u))
            {
                scale <<= bits;
                at = ir_vectorize_root(f, a);
                guarded = guarded || type == IR_I32;
                more = true;
            }
        }
    }
    key->index = at;
    key->scale = scale;
    key->offset = offset;
    key->guarded = guarded;
}

static IrVectorizeGroupKey ir_vectorize_group_key(const IrFunction* f, uint32_t pointer)
{
    IrVectorizeGroupKey key = {
        .base = BUSTER_IR_NONE,
        .index = BUSTER_IR_NONE,
    };
    const uint32_t at = ir_vectorize_group_pointer(f, pointer, &key.offset);
    if (at < f->instruction_count && f->op[at] == IR_ADDRESS && f->b[at] != BUSTER_IR_NONE)
    {
        const IrAddress address = f->addresses[f->c[at]];
        key.offset += (uint64_t)address.offset;
        key.base = ir_vectorize_group_pointer(f, f->a[at], &key.offset);
        key.scale = address.scale;
        ir_vectorize_group_index(f, f->b[at], &key);
    }
    else
    {
        key.base = at;
    }
    return key;
}

// The ordered walk preserves store boundaries. Size and stride filter the eight active keys together; the few matching
// candidates need bounded structural equality, offset and G-placement checks. Unmatched singletons may be replaced,
// and a store closes every active group. Only groups with at least two members survive into the loop's plan.
static void ir_vectorize_group_plan(IrVectorizeWork* work, uint32_t loop, IrVectorizeLoop* plan)
{
    _Static_assert(BUSTER_IR_VECTORIZE_GROUP_LIMIT == 8, "the group filter has eight lanes");
    const IrFunction* f = work->function;
    IrVectorizeGroupKey keys[BUSTER_IR_VECTORIZE_GROUP_LIMIT] = { 0 };
    uint32_t sizes[BUSTER_IR_VECTORIZE_GROUP_LIMIT] = { 0 };
    uint32_t steps[BUSTER_IR_VECTORIZE_GROUP_LIMIT] = { 0 };
    uint32_t active = 0;
    uint32_t occupied = 0;
    uint32_t singletons = 0;
    plan->group_count = 0;
    for (uint32_t k = 0; k < plan->row_count; k += 1)
    {
        const uint32_t row = work->rows[k];
        const uint32_t op = f->op[row];
        if (op == IR_STORE)
        {
            active = 0;
            occupied &= ~singletons;
            singletons = 0;
        }
        else if (op == IR_LOAD && work->classes[row] == IR_VECTORIZE_CLASS_VECTOR)
        {
            const uint32_t size = work->sizes[row];
            const uint32_t element = 1u << size;
            const int64_t coefficient = work->coefficients[f->a[row]];
            const bool candidate = coefficient > (int64_t)element && coefficient <= (int64_t)element * BUSTER_IR_VECTORIZE_STRIDE_LIMIT && coefficient % element == 0;
            if (candidate)
            {
                const uint32_t step = (uint32_t)coefficient;
                const IrVectorizeGroupKey key = ir_vectorize_group_key(f, f->a[row]);
                const __m256i group_sizes = _mm256_loadu_si256((const __m256i*)sizes);
                const __m256i group_steps = _mm256_loadu_si256((const __m256i*)steps);
                const uint32_t same_sizes = (uint32_t)_mm256_cmpeq_epi32_mask(group_sizes, _mm256_set1_epi32((int32_t)size));
                const uint32_t same_steps = (uint32_t)_mm256_cmpeq_epi32_mask(group_steps, _mm256_set1_epi32((int32_t)step));
                uint32_t matches = active & same_sizes & same_steps;
                bool placed = false;
                while (!placed && matches != 0)
                {
                    const uint32_t g = (uint32_t)__builtin_ctz(matches);
                    matches &= matches - 1;
                    IrVectorizeGroup* group = &plan->groups[g];
                    const uint32_t count = group->count;
                    const IrVectorizeGroupKey previous = keys[g];
                    const int64_t offset = (int64_t)(key.offset - previous.offset);
                    const int64_t first = offset < group->first ? offset : group->first;
                    const int64_t last = offset > group->last ? offset : group->last;
                    const bool bounded = offset > -(int64_t)step && offset < (int64_t)step && (offset & (element - 1)) == 0;
                    bool same = bounded && last - first < step && count < BUSTER_IR_VECTORIZE_GROUP_MEMBERS && key.scale == previous.scale;
                    same = same && ir_vectorize_same(f, key.base, previous.base, BUSTER_IR_VECTORIZE_DEPTH);
                    same = same && ir_vectorize_same(f, key.index, previous.index, BUSTER_IR_VECTORIZE_DEPTH);
                    const bool guarded = key.guarded || previous.guarded;
                    if (same && guarded)
                    {
                        same = ir_vectorize_chain_in_g(work, loop, plan, f->a[row]) && ir_vectorize_chain_in_g(work, loop, plan, f->a[group->rows[0]]);
                    }
                    if (same)
                    {
                        group->rows[count] = row;
                        group->offsets[count] = (int32_t)offset;
                        group->first = (int32_t)first;
                        group->last = (int32_t)last;
                        group->guarded |= guarded ? 1u << count : 0;
                        group->count = count + 1;
                        singletons &= ~(1u << g);
                        placed = true;
                    }
                }
                if (!placed)
                {
                    const uint32_t free = ~occupied & ((1u << BUSTER_IR_VECTORIZE_GROUP_LIMIT) - 1);
                    const uint32_t slots = free != 0 ? free : singletons;
                    if (slots != 0)
                    {
                        const uint32_t slot = (uint32_t)__builtin_ctz(slots);
                        plan->groups[slot] = (IrVectorizeGroup){ .count = 1 };
                        plan->groups[slot].rows[0] = row;
                        keys[slot] = key;
                        sizes[slot] = size;
                        steps[slot] = step;
                        active |= 1u << slot;
                        occupied |= 1u << slot;
                        singletons |= 1u << slot;
                    }
                }
            }
        }
    }
    for (uint32_t g = 0; g < BUSTER_IR_VECTORIZE_GROUP_LIMIT; g += 1)
    {
        if (((occupied & ~singletons) >> g & 1) != 0)
        {
            plan->groups[plan->group_count] = plan->groups[g];
            plan->group_count += 1;
        }
    }
}

#endif
