// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// PROTOTYPE (#2718 survey): value numbering with memory, scalar only. Included by ir_optimize.c.

#include <stdlib.h>

typedef struct IrMemoryAddress IrMemoryAddress;
struct IrMemoryAddress
{
    uint32_t root;
    uint32_t index;
    int64_t offset;
    uint32_t scale;
};

typedef struct IrMemoryEntry IrMemoryEntry;
struct IrMemoryEntry
{
    uint32_t family;
    uint32_t type;
    int64_t offset;
    uint32_t value;
    uint32_t class_count;
    uint32_t family_count;
    uint32_t epoch;
    bool used;
};

typedef struct IrMemoryFrame IrMemoryFrame;
struct IrMemoryFrame
{
    uint32_t block;
    uint32_t child;
    uint32_t mark;
    bool entered;
};

typedef struct IrMemoryUndo IrMemoryUndo;
struct IrMemoryUndo
{
    uint32_t* cell;
    uint32_t old;
    IrMemoryEntry* entry;
    IrMemoryEntry saved;
};

static uint64_t ir_memory_stats[8];
static void ir_memory_report(void)
{
    fprintf(stderr, "memory-vn: loads=%llu forwarded=%llu same-block=%llu from-store=%llu from-load=%llu private=%llu\n", (unsigned long long)ir_memory_stats[0],
            (unsigned long long)ir_memory_stats[1], (unsigned long long)ir_memory_stats[2], (unsigned long long)ir_memory_stats[3], (unsigned long long)ir_memory_stats[4],
            (unsigned long long)ir_memory_stats[5]);
}

static int ir_memory_knob(const char* name, int fallback)
{
    const char* text = getenv(name);
    return text != NULL ? atoi(text) : fallback;
}

static bool ir_memory_constant(const IrOptimizeWork* work, uint32_t value, int64_t* result)
{
    const bool known = value != BUSTER_IR_NONE && work->states[value] == IROPT_CONSTANT && work->function->type[value] == IR_I64;
    *result = known ? (int64_t)work->values[value].low : 0;
    return known;
}

static bool ir_memory_through(const IrFunction* f, uint32_t v)
{
    const uint32_t op = f->op[v];
    const uint32_t type = f->type[v];
    const bool pointerish = type == IR_PTR || type == IR_I64;
    bool result = false;
    if ((op == IR_INTTOPTR || op == IR_PTRTOINT || op == IR_COPY || op == IR_BITCAST) && pointerish)
    {
        const uint32_t source = f->type[f->a[v]];
        result = source == IR_PTR || source == IR_I64;
    }
    return result;
}

static IrMemoryAddress ir_memory_address(const IrOptimizeWork* work, const uint32_t* slot_first, uint32_t value)
{
    const IrFunction* f = work->function;
    IrMemoryAddress result = { .root = ir_optimize_forwarded(work, value), .index = BUSTER_IR_NONE };
    bool going = true;
    for (uint32_t step = 0; going && step < 64; step += 1)
    {
        const uint32_t v = result.root;
        const uint32_t op = f->op[v];
        int64_t constant = 0;
        int64_t sum = 0;
        if (ir_memory_through(f, v))
        {
            result.root = ir_optimize_forwarded(work, f->a[v]);
        }
        else if (op == IR_ADDRESS)
        {
            const IrAddress address = f->addresses[f->c[v]];
            const uint32_t b = f->b[v];
            int64_t product = 0;
            if (b == BUSTER_IR_NONE && !__builtin_add_overflow(result.offset, address.offset, &sum))
            {
                result.offset = sum;
                result.root = ir_optimize_forwarded(work, f->a[v]);
            }
            else if (b != BUSTER_IR_NONE && ir_memory_constant(work, ir_optimize_forwarded(work, b), &constant) && !__builtin_mul_overflow(constant, (int64_t)address.scale, &product) &&
                     !__builtin_add_overflow(result.offset, address.offset, &sum) && !__builtin_add_overflow(sum, product, &sum))
            {
                result.offset = sum;
                result.root = ir_optimize_forwarded(work, f->a[v]);
            }
            else if (b != BUSTER_IR_NONE && result.index == BUSTER_IR_NONE && !__builtin_add_overflow(result.offset, address.offset, &sum))
            {
                result.offset = sum;
                result.index = ir_optimize_forwarded(work, b);
                result.scale = address.scale;
                result.root = ir_optimize_forwarded(work, f->a[v]);
            }
            else
            {
                going = false;
            }
        }
        else if ((op == IR_ADD || op == IR_SUB) && f->type[v] == IR_I64 && ir_memory_constant(work, ir_optimize_forwarded(work, f->b[v]), &constant) &&
                 !(op == IR_SUB && constant == INT64_MIN) && !__builtin_add_overflow(result.offset, op == IR_ADD ? constant : -constant, &sum))
        {
            result.offset = sum;
            result.root = ir_optimize_forwarded(work, f->a[v]);
        }
        else if (op == IR_ADD && f->type[v] == IR_I64 && ir_memory_constant(work, ir_optimize_forwarded(work, f->a[v]), &constant) && !__builtin_add_overflow(result.offset, constant, &sum))
        {
            result.offset = sum;
            result.root = ir_optimize_forwarded(work, f->b[v]);
        }
        else
        {
            going = false;
        }
    }
    if (f->op[result.root] == IR_STACK_ADDR)
    {
        result.root = slot_first[f->a[result.root]];
    }
    return result;
}

static uint32_t ir_memory_bytes(uint32_t type)
{
    return ((uint32_t)ir_type_bits[type] + 7) / 8;
}

static bool ir_memory_plain(const IrFunction* f, uint32_t i)
{
    return (f->op[i] == IR_LOAD || f->op[i] == IR_STORE) && (f->flags[i] & BUSTER_IR_OBSERVABLE) == 0;
}

static uint64_t ir_memory_hash(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    uint64_t h = UINT64_C(1469598103934665603);
    h = (h ^ a) * UINT64_C(1099511628211);
    h = (h ^ b) * UINT64_C(1099511628211);
    h = (h ^ c) * UINT64_C(1099511628211);
    h = (h ^ d) * UINT64_C(1099511628211);
    return h ^ (h >> 29);
}

static void ir_memory_set(IrMemoryUndo* undo, uint32_t* undo_count, uint32_t* cell, uint32_t value)
{
    undo[*undo_count] = (IrMemoryUndo){ .cell = cell, .old = *cell };
    *undo_count += 1;
    *cell = value;
}

// Whether Tier 1 would fold load i into its one consumer: a single use, later in its block, past no other memory access.
static bool ir_memory_foldable(const IrOptimizeWork* work, uint32_t i)
{
    const IrFunction* f = work->function;
    bool result = f->op[i] == IR_LOAD && work->uses.offsets[(uint64_t)i + 1] - work->uses.offsets[i] == 1;
    if (result)
    {
        const uint32_t user = work->uses.uses[work->uses.offsets[i]].instruction;
        result = work->cfg.instruction_blocks[user] == work->cfg.instruction_blocks[i] && user > i && f->op[user] != IR_PHI;
        for (uint32_t k = i + 1; result && k < user; k += 1)
        {
            const uint32_t op = f->op[k];
            result = !(op == IR_LOAD || op == IR_STORE || op == IR_CALL || op == IR_CALL_INDIRECT || op == IR_FENCE || op == IR_ATOMIC_RMW || op == IR_MASKED_LOAD ||
                       op == IR_MASKED_STORE || op == IR_GATHER || op == IR_SCATTER || op == IR_COMPRESS_STORE);
        }
    }
    return result;
}

// Returns false where scratch has no room.
static bool ir_optimize_memory(IrOptimizeWork* work, Arena* scratch, uint32_t* forwarded_count)
{
    const IrFunction* f = work->function;
    const uint32_t n = f->instruction_count;
    const int family_rule = ir_memory_knob("BUSTER_MEM_FAMILY", 1);
    const int private_rule = ir_memory_knob("BUSTER_MEM_PRIVATE", 1);
    const int cross_rule = ir_memory_knob("BUSTER_MEM_CROSS", 1);
    const int fold_rule = ir_memory_knob("BUSTER_MEM_FOLD", 0);
    static int registered = 0;
    if (__atomic_exchange_n(&registered, 1, __ATOMIC_RELAXED) == 0 && getenv("BUSTER_MEM_REPORT") != NULL)
    {
        atexit(ir_memory_report);
    }
    bool ok = true;
    uint32_t* forward = work->forward;
    if (forward == 0)
    {
        forward = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        if (ok)
        {
            memset(forward, 0xff, (uint64_t)n * sizeof(uint32_t));
            work->forward = forward;
        }
    }
    uint32_t* slot_first = ir_allocate(scratch, f->slot_count, sizeof(uint32_t), &ok);
    uint32_t* derived = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
    uint8_t* escaped = ir_allocate(scratch, f->slot_count, 1, &ok);
    IrMemoryAddress* addresses = ir_allocate(scratch, n, sizeof(IrMemoryAddress), &ok);
    uint32_t* family = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
    uint8_t* dirty = ir_allocate(scratch, f->block_count, 1, &ok);
    uint8_t* clear = ir_allocate(scratch, f->block_count, 1, &ok);
    if (!ok)
    {
        return false;
    }
    for (uint32_t s = 0; s < f->slot_count; s += 1)
    {
        slot_first[s] = BUSTER_IR_NONE;
        escaped[s] = private_rule == 0 || (f->slots[s].flags & (BUSTER_IR_SLOT_VOLATILE | BUSTER_IR_SLOT_REGISTER_SAVE_AREA | BUSTER_IR_SLOT_OVERFLOW_AREA)) != 0;
    }
    for (uint32_t i = 0; i < n; i += 1)
    {
        derived[i] = BUSTER_IR_NONE;
        family[i] = BUSTER_IR_NONE;
        if (f->op[i] == IR_STACK_ADDR && slot_first[f->a[i]] == BUSTER_IR_NONE)
        {
            slot_first[f->a[i]] = i;
        }
    }
    // Derivation from slots, in reverse postorder (operands before users, but for phis).
    const Dominators* d = &work->dominators;
    for (uint32_t r = 0; r < d->reachable_count; r += 1)
    {
        const uint32_t block = d->rpo[r];
        for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
        {
            const uint32_t op = f->op[i];
            int64_t constant = 0;
            if (op == IR_STACK_ADDR)
            {
                derived[i] = f->a[i];
            }
            else if (ir_memory_through(f, i) || op == IR_ADDRESS)
            {
                derived[i] = derived[f->a[i]];
            }
            else if ((op == IR_ADD || op == IR_SUB) && f->type[i] == IR_I64 && ir_memory_constant(work, f->b[i], &constant))
            {
                derived[i] = derived[f->a[i]];
            }
            else if (op == IR_ADD && f->type[i] == IR_I64 && ir_memory_constant(work, f->a[i], &constant))
            {
                derived[i] = derived[f->b[i]];
            }
        }
    }
    for (uint32_t v = 0; v < n; v += 1)
    {
        if (derived[v] != BUSTER_IR_NONE)
        {
            for (uint32_t use = work->uses.offsets[v]; use < work->uses.offsets[(uint64_t)v + 1]; use += 1)
            {
                const uint32_t user = work->uses.uses[use].instruction;
                const uint32_t position = work->uses.uses[use].operand;
                const uint32_t op = f->op[user];
                const bool fine = (ir_memory_plain(f, user) && position == 0) || ((ir_memory_through(f, user) || op == IR_ADDRESS) && position == 0) ||
                                  ((op == IR_ADD || op == IR_SUB) && derived[user] == derived[v]);
                escaped[derived[v]] = escaped[derived[v]] || !fine;
            }
        }
    }
    // Addresses, families and block dirtiness. Families: (root, index, scale) by a linear hash over accesses.
    uint32_t access_count = 0;
    for (uint32_t i = 0; i < n; i += 1)
    {
        access_count += ir_memory_plain(f, i);
    }
    uint64_t buckets = 16;
    while (buckets < (uint64_t)access_count * 2)
    {
        buckets *= 2;
    }
    uint32_t* heads = ir_allocate(scratch, buckets, sizeof(uint32_t), &ok);
    uint32_t* family_root = ir_allocate(scratch, access_count + 1, sizeof(uint32_t), &ok);
    uint32_t* family_index = ir_allocate(scratch, access_count + 1, sizeof(uint32_t), &ok);
    uint32_t* family_scale = ir_allocate(scratch, access_count + 1, sizeof(uint32_t), &ok);
    uint8_t* family_overlap = ir_allocate(scratch, access_count + 1, 1, &ok);
    uint32_t* family_count = ir_allocate(scratch, access_count + 1, sizeof(uint32_t), &ok);
    uint32_t* class_count = ir_allocate(scratch, (uint64_t)f->slot_count + 1, sizeof(uint32_t), &ok);
    if (!ok)
    {
        return false;
    }
    memset(heads, 0xff, buckets * sizeof(uint32_t));
    uint32_t families = 0;
    for (uint32_t i = 0; i < n; i += 1)
    {
        if (ir_memory_plain(f, i))
        {
            addresses[i] = ir_memory_address(work, slot_first, f->a[i]);
            const IrMemoryAddress a = addresses[i];
            uint64_t bucket = ir_memory_hash(a.root, a.index, a.index != BUSTER_IR_NONE ? a.scale : 0, 0) & (buckets - 1);
            while (heads[bucket] != BUSTER_IR_NONE &&
                   !(family_root[heads[bucket]] == a.root && family_index[heads[bucket]] == a.index && (a.index == BUSTER_IR_NONE || family_scale[heads[bucket]] == a.scale)))
            {
                bucket = (bucket + 1) & (buckets - 1);
            }
            if (heads[bucket] == BUSTER_IR_NONE)
            {
                heads[bucket] = families;
                family_root[families] = a.root;
                family_index[families] = a.index;
                family_scale[families] = a.scale;
                family_overlap[families] = 0;
                family_count[families] = 0;
                families += 1;
            }
            family[i] = heads[bucket];
        }
    }
    // Overlap: any two distinct (offset, type) keys of one family whose bytes meet. Quadratic per family (prototype).
    for (uint32_t i = 0; i < n; i += 1)
    {
        if (family[i] != BUSTER_IR_NONE && family_overlap[family[i]] == 0)
        {
            const uint32_t ti = f->op[i] == IR_STORE ? f->type[f->b[i]] : f->type[i];
            for (uint32_t j = i + 1; j < n; j += 1)
            {
                if (family[j] == family[i])
                {
                    const uint32_t tj = f->op[j] == IR_STORE ? f->type[f->b[j]] : f->type[j];
                    const int64_t oi = addresses[i].offset;
                    const int64_t oj = addresses[j].offset;
                    const bool same = oi == oj && ti == tj;
                    const bool meet = oi < oj + (int64_t)ir_memory_bytes(tj) && oj < oi + (int64_t)ir_memory_bytes(ti);
                    family_overlap[family[i]] = family_overlap[family[i]] || (!same && meet) || family_rule == 0;
                }
            }
        }
        if (family[i] != BUSTER_IR_NONE && family_rule == 0)
        {
            family_overlap[family[i]] = 1;
        }
    }
    // A write's class: 0 for general memory, 1 + s for private slot s.
    for (uint32_t b = 0; b < f->block_count; b += 1)
    {
        dirty[b] = 0;
        clear[b] = 3;
        for (uint32_t i = f->block_offsets[b]; i < f->block_offsets[(uint64_t)b + 1]; i += 1)
        {
            const uint32_t op = f->op[i];
            const bool opaque = op == IR_CALL || op == IR_CALL_INDIRECT || op == IR_FENCE || op == IR_ATOMIC_RMW || op == IR_MASKED_STORE || op == IR_COMPRESS_STORE ||
                                op == IR_SCATTER || op == IR_ALLOCA || op == IR_STACK_RESTORE || ((op == IR_LOAD || op == IR_STORE) && !ir_memory_plain(f, i));
            const bool store = op == IR_STORE && ir_memory_plain(f, i);
            const uint32_t slot = store ? derived[f->a[i]] : op == IR_LIFETIME ? f->a[i] : BUSTER_IR_NONE;
            const bool private_slot = slot != BUSTER_IR_NONE && escaped[slot] == 0;
            dirty[b] |= (uint8_t)((opaque || ((store || op == IR_LIFETIME) && !private_slot)) ? 1 : 0);
            dirty[b] |= (uint8_t)(((store || op == IR_LIFETIME) && private_slot) ? 2 : 0);
        }
    }
    // Clear in-paths, the greatest fixed point, for each class kind (bit 0 general, bit 1 private).
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (uint32_t r = 1; r < d->reachable_count; r += 1)
        {
            const uint32_t block = d->rpo[r];
            const uint32_t idom = d->idom[block];
            uint8_t value = 3;
            for (uint32_t edge = work->cfg.predecessor_offsets[block]; edge < work->cfg.predecessor_offsets[(uint64_t)block + 1]; edge += 1)
            {
                uint32_t y = work->cfg.predecessors[edge];
                while (d->rpo_index[y] != BUSTER_DOMINATOR_NONE && y != idom)
                {
                    value &= (uint8_t)(~dirty[y] & clear[y]);
                    y = d->idom[y];
                }
            }
            value = cross_rule != 0 ? value : 0;
            changed = changed || value != clear[block];
            clear[block] = value;
        }
    }
    // The dominator-tree walk, with an undo log.
    uint64_t table_size = 16;
    while (table_size < (uint64_t)access_count * 2)
    {
        table_size *= 2;
    }
    IrMemoryEntry* table = ir_allocate(scratch, table_size, sizeof(IrMemoryEntry), &ok);
    IrMemoryUndo* undo = ir_allocate(scratch, (uint64_t)n * 4 + (uint64_t)f->block_count * 2 + 16, sizeof(IrMemoryUndo), &ok);
    IrMemoryFrame* frames = ir_allocate(scratch, f->block_count + 1, sizeof(IrMemoryFrame), &ok);
    if (!ok)
    {
        return false;
    }
    memset(table, 0, table_size * sizeof(IrMemoryEntry));
    memset(class_count, 0, ((uint64_t)f->slot_count + 1) * sizeof(uint32_t));
    uint32_t epochs[2] = { 0, 0 };
    uint32_t undo_count = 0;
    uint32_t depth = f->block_count != 0;
    uint32_t next_epoch = 1;
    frames[0] = (IrMemoryFrame){ .block = 0 };
    while (depth != 0)
    {
        IrMemoryFrame* frame = &frames[depth - 1];
        if (!frame->entered)
        {
            frame->entered = true;
            frame->mark = undo_count;
            frame->child = d->tree_offsets[frame->block];
            for (uint32_t k = 0; k < 2; k += 1)
            {
                if (((clear[frame->block] >> k) & 1) == 0)
                {
                    ir_memory_set(undo, &undo_count, &epochs[k], next_epoch++);
                }
            }
            const bool executable = work->blocks[frame->block] != 0;
            for (uint32_t i = f->block_offsets[frame->block]; i < f->block_offsets[(uint64_t)frame->block + 1]; i += 1)
            {
                const uint32_t op = f->op[i];
                const bool opaque = op == IR_CALL || op == IR_CALL_INDIRECT || op == IR_FENCE || op == IR_ATOMIC_RMW || op == IR_MASKED_STORE || op == IR_COMPRESS_STORE ||
                                    op == IR_SCATTER || op == IR_ALLOCA || op == IR_STACK_RESTORE || ((op == IR_LOAD || op == IR_STORE) && !ir_memory_plain(f, i));
                if (opaque)
                {
                    ir_memory_set(undo, &undo_count, &class_count[0], class_count[0] + 1);
                }
                else if (op == IR_LIFETIME)
                {
                    const uint32_t cls = escaped[f->a[i]] == 0 ? 1 + f->a[i] : 0;
                    ir_memory_set(undo, &undo_count, &class_count[cls], class_count[cls] + 1);
                }
                else if (ir_memory_plain(f, i))
                {
                    const uint32_t slot = derived[f->a[i]];
                    const uint32_t cls = slot != BUSTER_IR_NONE && escaped[slot] == 0 ? 1 + slot : 0;
                    const uint32_t kind = cls != 0;
                    const uint32_t fam = family[i];
                    const bool store = op == IR_STORE;
                    const uint32_t type = store ? f->type[f->b[i]] : f->type[i];
                    const int64_t offset = addresses[i].offset;
                    uint64_t bucket = ir_memory_hash(fam, type, (uint64_t)offset, 1) & (table_size - 1);
                    while (table[bucket].used && !(table[bucket].family == fam && table[bucket].type == type && table[bucket].offset == offset))
                    {
                        bucket = (bucket + 1) & (table_size - 1);
                    }
                    IrMemoryEntry* entry = &table[bucket];
                    if (store)
                    {
                        ir_memory_set(undo, &undo_count, &class_count[cls], class_count[cls] + 1);
                        ir_memory_set(undo, &undo_count, &family_count[fam], family_count[fam] + 1);
                    }
                    bool valid = !store && entry->used && entry->epoch == epochs[kind];
                    if (valid)
                    {
                        const uint32_t intervening = class_count[cls] - entry->class_count;
                        valid = family_overlap[fam] != 0 ? intervening == 0 : intervening == family_count[fam] - entry->family_count;
                    }
                    if (!store)
                    {
                        __atomic_fetch_add(&ir_memory_stats[0], 1, __ATOMIC_RELAXED);
                    }
                    if (valid && executable)
                    {
                        forward[i] = entry->value;
                        *forwarded_count += 1;
                        __atomic_fetch_add(&ir_memory_stats[1], 1, __ATOMIC_RELAXED);
                        __atomic_fetch_add(&ir_memory_stats[2], work->cfg.instruction_blocks[entry->value] == frame->block, __ATOMIC_RELAXED);
                        __atomic_fetch_add(&ir_memory_stats[3], f->op[entry->value] != IR_LOAD, __ATOMIC_RELAXED);
                        __atomic_fetch_add(&ir_memory_stats[4], f->op[entry->value] == IR_LOAD, __ATOMIC_RELAXED);
                        __atomic_fetch_add(&ir_memory_stats[5], cls != 0, __ATOMIC_RELAXED);
                    }
                    else
                    {
                        const uint32_t stored = store ? ir_optimize_forwarded(work, f->b[i]) : i;
                        undo[undo_count] = (IrMemoryUndo){ .entry = entry, .saved = *entry };
                        undo_count += 1;
                        const bool usable = f->op[stored] != IR_UNDEF && !(fold_rule != 0 && !store && ir_memory_foldable(work, i));
                        *entry = (IrMemoryEntry){
                            .family = fam,
                            .type = type,
                            .offset = offset,
                            .value = stored,
                            .class_count = class_count[cls],
                            .family_count = family_count[fam],
                            .epoch = usable ? epochs[kind] : UINT32_MAX,
                            .used = true,
                        };
                    }
                }
            }
        }
        if (frame->child < d->tree_offsets[(uint64_t)frame->block + 1])
        {
            const uint32_t child = d->tree_children[frame->child++];
            frames[depth++] = (IrMemoryFrame){ .block = child };
        }
        else
        {
            while (undo_count > frame->mark)
            {
                undo_count -= 1;
                if (undo[undo_count].entry != NULL)
                {
                    *undo[undo_count].entry = undo[undo_count].saved;
                }
                else
                {
                    *undo[undo_count].cell = undo[undo_count].old;
                }
            }
            depth -= 1;
        }
    }
    // An identity's operand may be a load forwarded since: every forward names a value that isn't forwarded.
    for (uint32_t i = 0; i < n; i += 1)
    {
        uint32_t root = forward[i];
        while (root != BUSTER_IR_NONE && forward[root] != BUSTER_IR_NONE)
        {
            root = forward[root];
        }
        forward[i] = root;
    }
    return true;
}
