// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's preallocation block scheduling (#172): the C driver's --tier1-schedule=pre reorders each block's IR
// instructions before the optimizer's final verification, so that Tier 1's selection, allocation and emission take the
// new order. Include after ir.c. The postallocation variant is tier1_schedule.c's, on machine records.
//
// This is a target pass on the IR, not a machine-independent one: its weights are x86 classes for Zen 5 and its glue
// mirrors Tier 1's x86 selection. It runs on the IR because Tier 1 has no machine IR before allocation: the selector's
// plan is a pattern per IR node, which liveness, allocation and emission walk in the IR's order, and the records are
// made with physical registers. Scheduling before selection reorders the trees the selector then makes, which folds a
// single-use child of the same block wherever it is (tier1_select_tree_child), but for the load fold below.
//
// A block's instructions between its barriers form a segment, list-scheduled on its own: each ready instruction of
// greatest priority first, ties by original position. A barrier keeps its place and nothing moves across it: a phi, a
// parameter, a terminator, a call, a stack allocation, save or restore, a lifetime marker, a fence, an atomic access,
// an intrinsic and a volatile or atomic load. In a segment, an instruction follows its operands; the loads, stores and
// divisions keep their order among themselves (no alias analysis, volatile stores and possible division traps where
// the program put them). Priority is the longest path from an instruction to the segment's end, each instruction
// weighed by its class (a load 4, a multiplication or a floating operation or conversion 3, a division 13, a floating
// division 13, a constant or an address of a slot or a symbol 0, any other 1): relative priorities, not cycle
// predictions, as tier1_schedule.c's.
//
// Tier 1's selection folds a load into its consumer's memory operand when the load is the last memory access before
// the consumer (tier1_select_load_folds). A load that does so in the original order stays glued to its consumer here:
// one node of the segment's graph, the load placed right before the consumer, its weight added to the consumer's.
// Scheduling it apart would trade the fold for a register, which is what this variant measures for every other value.
//
// tier1_schedule_ir_function mutates exclusively owned, verified columns in place, as ir_truth_simplify does: no use
// lists or verified-analysis handles may be live across it. Blocks keep their instruction counts, so block_offsets, the extra
// column, the side tables and the labels are unchanged; the instruction columns are permuted within blocks and every
// value operand (a, b, c by ir_value_mask, phi incomings and call arguments) renamed. The caller runs the optimizer and
// its final SSA verifier afterwards. The pass is scalar: its graph is the operands' (at most three edges an instruction
// in a segment, every instruction with one is a barrier) and the memory order's chain, built in one pass, and its
// selection is a binary heap; the 9700X's measurement of its compile time is in docs/tier1-scheduling.md.

#ifndef BUSTER_TIER1_SCHEDULE_IR_C
#define BUSTER_TIER1_SCHEDULE_IR_C

typedef struct Tier1ScheduleIrStats Tier1ScheduleIrStats;
struct Tier1ScheduleIrStats
{
    // Segments of two instructions or more, and instructions that changed places.
    uint32_t segments;
    uint32_t moved;
    // Loads kept glued to the consumer they fold into.
    uint32_t glued;
};

static bool tier1_schedule_ir_barrier(const IrFunction* f, uint32_t i)
{
    const uint32_t op = f->op[i];
    return op == IR_PHI || op == IR_PARAM || ir_terminator_table[op] != 0 || op == IR_CALL || op == IR_CALL_INDIRECT || op == IR_ALLOCA ||
           op == IR_STACK_SAVE || op == IR_STACK_RESTORE || op == IR_LIFETIME || op == IR_FENCE || op == IR_ATOMIC_RMW || op == IR_INTRINSIC ||
           op == IR_MASKED_LOAD || op == IR_MASKED_STORE || op == IR_COMPRESS_STORE || op == IR_GATHER ||
           (op == IR_LOAD && (f->flags[i] & BUSTER_IR_OBSERVABLE) != 0);
}

// The instructions that keep their order among themselves in a segment: memory accesses and divisions.
static bool tier1_schedule_ir_ordered(uint32_t op)
{
    return op == IR_LOAD || op == IR_STORE || op == IR_SDIV || op == IR_UDIV || op == IR_SREM || op == IR_UREM;
}

static uint32_t tier1_schedule_ir_weight(uint32_t op)
{
    uint32_t weight = 1;
    switch (op)
    {
        case IR_CONST:
        case IR_VCONST:
        case IR_UNDEF:
        case IR_STACK_ADDR:
        case IR_SYMBOL_ADDR:
        case IR_COPY:
            weight = 0;
            break;
        case IR_LOAD:
            weight = 4;
            break;
        case IR_MUL:
        case IR_FADD:
        case IR_FSUB:
        case IR_FMUL:
        case IR_SITOFP:
        case IR_UITOFP:
        case IR_FPTOSI:
        case IR_FPTOUI:
        case IR_FTRUNC:
        case IR_FEXT:
            weight = 3;
            break;
        case IR_SDIV:
        case IR_UDIV:
        case IR_SREM:
        case IR_UREM:
        case IR_FDIV:
            weight = 13;
            break;
        default:
            break;
    }
    return weight;
}

// The segment's scratch, each column of the function's instruction count (indexed by original instruction).
typedef struct Tier1ScheduleIrWork Tier1ScheduleIrWork;
struct Tier1ScheduleIrWork
{
    uint32_t* uses;
    // The single consumer a load folds into (glued), or BUSTER_IR_NONE; and each node's glued load.
    uint32_t* glued_to;
    uint32_t* glued_from;
    uint32_t* pending;
    uint32_t* height;
    uint32_t* successor_offsets;
    uint32_t* successors;
    uint32_t* heap;
    // The new order, order[new] = old, and its inverse.
    uint32_t* order;
    uint32_t* place;
};

// The node an instruction belongs to in its segment: its consumer for a glued load.
static uint32_t tier1_schedule_ir_node(const Tier1ScheduleIrWork* w, uint32_t i)
{
    return w->glued_to[i] != BUSTER_IR_NONE ? w->glued_to[i] : i;
}

// Whether heap entry x goes before y: greater height, then smaller original position.
static bool tier1_schedule_ir_before(const Tier1ScheduleIrWork* w, uint32_t x, uint32_t y)
{
    return w->height[x] > w->height[y] || (w->height[x] == w->height[y] && x < y);
}

static void tier1_schedule_ir_push(Tier1ScheduleIrWork* w, uint32_t* size, uint32_t node)
{
    uint32_t at = *size;
    *size += 1;
    while (at != 0 && tier1_schedule_ir_before(w, node, w->heap[(at - 1) / 2]))
    {
        w->heap[at] = w->heap[(at - 1) / 2];
        at = (at - 1) / 2;
    }
    w->heap[at] = node;
}

static uint32_t tier1_schedule_ir_pop(Tier1ScheduleIrWork* w, uint32_t* size)
{
    const uint32_t top = w->heap[0];
    *size -= 1;
    const uint32_t last = w->heap[*size];
    uint32_t at = 0;
    bool sifting = *size != 0;
    while (sifting)
    {
        const uint32_t left = 2 * at + 1;
        uint32_t child = left;
        child = left + 1 < *size && tier1_schedule_ir_before(w, w->heap[left + 1], w->heap[left]) ? left + 1 : child;
        sifting = left < *size && tier1_schedule_ir_before(w, w->heap[child], last);
        if (sifting)
        {
            w->heap[at] = w->heap[child];
            at = child;
        }
    }
    if (*size != 0)
    {
        w->heap[at] = last;
    }
    return top;
}

// The edge from instruction `from` to instruction `to` of the segment [first, end), between their nodes; counted in
// `pending` (the node's unscheduled predecessors) and, on the second pass, stored in its successor list.
static void tier1_schedule_ir_edge(Tier1ScheduleIrWork* w, uint32_t first, uint32_t end, uint32_t from, uint32_t to, bool store)
{
    if (from >= first && from < end)
    {
        const uint32_t source = tier1_schedule_ir_node(w, from);
        const uint32_t target = tier1_schedule_ir_node(w, to);
        if (source != target && store)
        {
            w->successors[w->successor_offsets[source]] = target;
            w->successor_offsets[source] += 1;
            w->pending[target] += 1;
        }
        else if (source != target)
        {
            w->successor_offsets[source + 1] += 1;
        }
    }
}

// The segment's edges: each instruction's operands within it, and the order of its memory accesses and divisions.
static void tier1_schedule_ir_edges(const IrFunction* f, Tier1ScheduleIrWork* w, uint32_t first, uint32_t end, bool store)
{
    uint32_t previous = BUSTER_IR_NONE;
    for (uint32_t i = first; i < end; i += 1)
    {
        const uint64_t count = ir_operand_count(f, i);
        for (uint32_t p = 0; p < count; p += 1)
        {
            tier1_schedule_ir_edge(w, first, end, ir_operand(f, i, p, 0).value, i, store);
        }
        if (tier1_schedule_ir_ordered(f->op[i]))
        {
            if (previous != BUSTER_IR_NONE)
            {
                tier1_schedule_ir_edge(w, first, end, previous, i, store);
            }
            previous = i;
        }
    }
}

// Schedules the segment [first, end) of non-barrier instructions into w->order[first..end).
static uint32_t tier1_schedule_ir_segment(const IrFunction* f, Tier1ScheduleIrWork* w, uint32_t first, uint32_t end, Tier1ScheduleIrStats* stats)
{
    // Glue each load that folds into its consumer in the original order: its only use, in the segment, not a memory
    // access itself, with no other ordered instruction between them.
    uint32_t previous = BUSTER_IR_NONE;
    for (uint32_t i = first; i < end; i += 1)
    {
        w->glued_to[i] = BUSTER_IR_NONE;
        w->glued_from[i] = BUSTER_IR_NONE;
    }
    for (uint32_t i = first; i < end; i += 1)
    {
        const uint32_t op = f->op[i];
        const uint64_t count = ir_operand_count(f, i);
        uint32_t load = BUSTER_IR_NONE;
        for (uint32_t p = 0; !tier1_schedule_ir_ordered(op) && p < count; p += 1)
        {
            const uint32_t value = ir_operand(f, i, p, 0).value;
            const bool folds = value >= first && value < i && f->op[value] == IR_LOAD && w->uses[value] == 1 && value == previous && w->glued_to[value] == BUSTER_IR_NONE;
            load = folds ? value : load;
        }
        if (load != BUSTER_IR_NONE)
        {
            w->glued_to[load] = i;
            w->glued_from[i] = load;
            stats->glued += 1;
        }
        previous = tier1_schedule_ir_ordered(op) ? i : previous;
    }
    for (uint32_t i = first; i <= end; i += 1)
    {
        w->successor_offsets[i] = 0;
        w->pending[i] = 0;
    }
    tier1_schedule_ir_edges(f, w, first, end, false);
    for (uint32_t i = first; i < end; i += 1)
    {
        w->successor_offsets[i + 1] += w->successor_offsets[i];
    }
    // successor_offsets[node] is its list's start; the second pass advances it to its end.
    tier1_schedule_ir_edges(f, w, first, end, true);
    // Heights, from the segment's end: a node's weight (with its glued load's) and its longest successor's. Its successors
    // are its list, which ends at successor_offsets[node] and starts at its predecessor's end.
    for (uint32_t remaining = end; remaining != first; remaining -= 1)
    {
        const uint32_t i = remaining - 1;
        uint32_t height = 0;
        const uint32_t start = i == first ? 0 : w->successor_offsets[i - 1];
        for (uint32_t k = start; w->glued_to[i] == BUSTER_IR_NONE && k < w->successor_offsets[i]; k += 1)
        {
            height = height > w->height[w->successors[k]] ? height : w->height[w->successors[k]];
        }
        const uint32_t glued = w->glued_from[i] != BUSTER_IR_NONE ? tier1_schedule_ir_weight(IR_LOAD) : 0;
        w->height[i] = tier1_schedule_ir_weight(f->op[i]) + glued + height;
    }
    uint32_t size = 0;
    for (uint32_t i = first; i < end; i += 1)
    {
        if (w->glued_to[i] == BUSTER_IR_NONE && w->pending[i] == 0)
        {
            tier1_schedule_ir_push(w, &size, i);
        }
    }
    uint32_t output = first;
    uint32_t moved = 0;
    while (size != 0)
    {
        const uint32_t node = tier1_schedule_ir_pop(w, &size);
        const uint32_t load = w->glued_from[node];
        if (load != BUSTER_IR_NONE)
        {
            moved += load != output;
            w->order[output] = load;
            output += 1;
        }
        moved += node != output;
        w->order[output] = node;
        output += 1;
        const uint32_t start = node == first ? 0 : w->successor_offsets[node - 1];
        for (uint32_t k = start; k < w->successor_offsets[node]; k += 1)
        {
            const uint32_t successor = w->successors[k];
            w->pending[successor] -= 1;
            if (w->pending[successor] == 0)
            {
                tier1_schedule_ir_push(w, &size, successor);
            }
        }
    }
    return moved;
}

// The value operands of instruction i renamed by `place` (old to new): a, b and c by ir_value_mask, a phi's incomings
// and a call's arguments in extra. `renamed` marks the extra slots done, so that none is renamed twice.
static void tier1_schedule_ir_rename(IrFunction* f, uint32_t i, const uint32_t* place, uint64_t* renamed)
{
    const uint32_t mask = ir_value_mask(f, i);
    f->a[i] = (mask & 1) ? place[f->a[i]] : f->a[i];
    f->b[i] = (mask & 2) ? place[f->b[i]] : f->b[i];
    f->c[i] = (mask & 4) ? place[f->c[i]] : f->c[i];
    const uint32_t op = f->op[i];
    const uint32_t count = op == IR_PHI ? f->b[i] : op == IR_CALL || op == IR_CALL_INDIRECT ? f->calls[f->b[i]].argument_count : 0;
    for (uint32_t p = 0; p < count; p += 1)
    {
        const uint32_t slot = op == IR_PHI ? f->a[i] + p * 2 + 1 : f->calls[f->b[i]].argument_first + p;
        const uint64_t bit = UINT64_C(1) << (slot & 63);
        if ((renamed[slot / 64] & bit) == 0)
        {
            f->extra[slot] = place[f->extra[slot]];
            renamed[slot / 64] |= bit;
        }
    }
}

// Schedules every block of `f` in place (see the file's comment), with scratch from `scratch`, popped before it
// returns, and with `order` not null, stores the permutation there, order[new] = old, of instruction_count elements.
// Returns IR_ERROR_MEMORY, f unchanged, when there's no room.
static __attribute__((unused)) IrError tier1_schedule_ir_function(IrFunction* f, Arena* scratch, uint32_t* order, Tier1ScheduleIrStats* stats)
{
    const uint64_t mark = scratch->length;
    const uint32_t n = f->instruction_count;
    *stats = (Tier1ScheduleIrStats){ 0 };
    Tier1ScheduleIrWork w = {
        .uses = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)n + 1),
        .glued_to = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)n + 1),
        .glued_from = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)n + 1),
        .pending = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)n + 1),
        .height = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)n + 1),
        .successor_offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)n + 2),
        .successors = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)n * 4 + 1),
        .heap = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)n + 1),
        .order = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)n + 1),
        .place = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)n + 1),
    };
    bool ready = w.uses != 0 && w.glued_to != 0 && w.glued_from != 0 && w.pending != 0 && w.height != 0 && w.successor_offsets != 0 && w.successors != 0 &&
                 w.heap != 0 && w.order != 0 && w.place != 0;
    if (ready)
    {
        memset(w.uses, 0, (uint64_t)n * sizeof(uint32_t));
        for (uint32_t i = 0; i < n; i += 1)
        {
            const uint64_t count = ir_operand_count(f, i);
            for (uint32_t p = 0; p < count; p += 1)
            {
                w.uses[ir_operand(f, i, p, 0).value] += 1;
            }
            w.order[i] = i;
        }
        for (uint32_t b = 0; b < f->block_count; b += 1)
        {
            uint32_t first = f->block_offsets[b];
            const uint32_t end = f->block_offsets[b + 1];
            for (uint32_t i = first; i <= end; i += 1)
            {
                const bool cut = i == end || tier1_schedule_ir_barrier(f, i);
                if (cut && i - first >= 2)
                {
                    stats->segments += 1;
                    stats->moved += tier1_schedule_ir_segment(f, &w, first, i, stats);
                }
                first = cut ? i + 1 : first;
            }
        }
    }
    // Each segment's operand edges are at most three an instruction, its memory edge one more: 4n bounds the lists.
    if (ready && stats->moved != 0)
    {
        uint8_t* bytes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)n * 3 + 1);
        uint32_t* words = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)n * 3 + 1);
        uint64_t* renamed = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)f->extra_count / 64 + 1);
        ready = bytes != 0 && words != 0 && renamed != 0;
        if (ready)
        {
            for (uint32_t i = 0; i < n; i += 1)
            {
                w.place[w.order[i]] = i;
            }
            uint8_t* byte_columns[] = { f->op, f->type, f->flags, f->alignment_log2 };
            uint32_t* word_columns[] = { f->a, f->b, f->c };
            for (uint32_t c = 0; c < 4; c += 1)
            {
                memcpy(bytes, byte_columns[c], n);
                for (uint32_t i = 0; i < n; i += 1)
                {
                    byte_columns[c][i] = bytes[w.order[i]];
                }
            }
            for (uint32_t c = 0; c < 3; c += 1)
            {
                memcpy(words, word_columns[c], (uint64_t)n * sizeof(uint32_t));
                for (uint32_t i = 0; i < n; i += 1)
                {
                    word_columns[c][i] = words[w.order[i]];
                }
            }
            if (f->debug != NULL)
            {
                memcpy(words, f->debug->origins, (uint64_t)n * sizeof(uint32_t));
                for (uint32_t i = 0; i < n; i += 1)
                {
                    f->debug->origins[i] = words[w.order[i]];
                }
                for (uint32_t k = 0; k < f->debug->binding_count; k += 1)
                {
                    IrDebugBinding* binding = f->debug->bindings + k;
                    binding->anchor = binding->anchor != BUSTER_IR_NONE ? w.place[binding->anchor] : BUSTER_IR_NONE;
                    if (binding->kind == IR_DEBUG_BIND_VALUE)
                    {
                        binding->value = w.place[binding->value];
                    }
                }
                for (uint32_t k = 0; k < f->debug->region_count; k += 1)
                {
                    IrDebugRegion* region = f->debug->regions + k;
                    if (region->kind == IR_DEBUG_BIND_VALUE)
                    {
                        region->value = w.place[region->value];
                    }
                }
            }
            memset(renamed, 0, ((uint64_t)f->extra_count / 64 + 1) * sizeof(uint64_t));
            for (uint32_t i = 0; i < n; i += 1)
            {
                tier1_schedule_ir_rename(f, i, w.place, renamed);
            }
        }
    }
    if (ready && order != 0 && n != 0)
    {
        memcpy(order, w.order, (uint64_t)n * sizeof(uint32_t));
    }
    arena_pop_to(scratch, mark);
    if (!ready)
    {
        *stats = (Tier1ScheduleIrStats){ 0 };
    }
    return ready ? IR_OK : IR_ERROR_MEMORY;
}

// The columns tier1_schedule_ir_function changes, copied before it runs, for tier1_schedule_ir_verify.
typedef struct Tier1ScheduleIrOriginal Tier1ScheduleIrOriginal;
struct Tier1ScheduleIrOriginal
{
    IrFunction function;
};

// Copies `f`'s instruction columns and extra into `arena` as `original`'s function, the other columns borrowed.
static __attribute__((unused)) bool tier1_schedule_ir_copy(const IrFunction* f, Arena* arena, Tier1ScheduleIrOriginal* original)
{
    const uint64_t n = f->instruction_count;
    IrFunction copy = *f;
    copy.op = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, n + 1);
    copy.type = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, n + 1);
    copy.flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, n + 1);
    copy.alignment_log2 = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, n + 1);
    copy.a = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, n + 1);
    copy.b = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, n + 1);
    copy.c = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, n + 1);
    copy.extra = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)f->extra_count + 1);
    const bool ready = copy.op != 0 && copy.type != 0 && copy.flags != 0 && copy.alignment_log2 != 0 && copy.a != 0 && copy.b != 0 && copy.c != 0 && copy.extra != 0 &&
                       ir_debug_create(arena, &copy, f->debug);
    if (ready)
    {
        ir_debug_remap(f, &copy, NULL, NULL, NULL);
        memcpy(copy.op, f->op, n);
        memcpy(copy.type, f->type, n);
        memcpy(copy.flags, f->flags, n);
        memcpy(copy.alignment_log2, f->alignment_log2, n);
        memcpy(copy.a, f->a, n * sizeof(uint32_t));
        memcpy(copy.b, f->b, n * sizeof(uint32_t));
        memcpy(copy.c, f->c, n * sizeof(uint32_t));
        // An empty column may hold no pointer at all: copying nothing from null is still undefined behavior.
        if (f->extra_count != 0)
        {
            memcpy(copy.extra, f->extra, (uint64_t)f->extra_count * sizeof(uint32_t));
        }
        original->function = copy;
    }
    return ready;
}

// The scalar reference for a schedule (--verify's, and the tests'): from the original columns alone, `order` must be a
// permutation that keeps every instruction in its block and every barrier in its place; the scheduled function must hold
// each original instruction at its new place with its operands renamed to their new places; and in each block every
// operand defined there must come first, and the loads, stores and divisions must keep their order. `place` is
// instruction_count words of scratch.
static __attribute__((unused)) bool tier1_schedule_ir_verify(const Tier1ScheduleIrOriginal* original, const IrFunction* scheduled, const uint32_t* order, uint32_t* place)
{
    const IrFunction* f = &original->function;
    const uint32_t n = f->instruction_count;
    bool valid = scheduled->instruction_count == n && scheduled->block_count == f->block_count && scheduled->extra_count == f->extra_count;
    for (uint32_t i = 0; valid && i < n; i += 1)
    {
        place[i] = UINT32_MAX;
    }
    for (uint32_t i = 0; valid && i < n; i += 1)
    {
        valid = order[i] < n && place[order[i]] == UINT32_MAX;
        if (valid)
        {
            place[order[i]] = i;
        }
    }
    valid = valid && ((f->debug == NULL && scheduled->debug == NULL) ||
                     (f->debug != NULL && scheduled->debug != NULL && f->debug->binding_count == scheduled->debug->binding_count &&
                      f->debug->region_count == scheduled->debug->region_count));
    for (uint32_t k = 0; valid && f->debug != NULL && k < f->debug->binding_count; k += 1)
    {
        const IrDebugBinding before = f->debug->bindings[k];
        const IrDebugBinding after = scheduled->debug->bindings[k];
        const uint32_t anchor = before.anchor != BUSTER_IR_NONE ? place[before.anchor] : BUSTER_IR_NONE;
        const uint32_t value = before.kind == IR_DEBUG_BIND_VALUE ? place[before.value] : BUSTER_IR_NONE;
        valid = before.local == after.local && before.entry == after.entry && before.literal == after.literal && before.kind == after.kind &&
                before.bytes == after.bytes && before.type == after.type && after.anchor == anchor && after.value == value;
    }
    for (uint32_t k = 0; valid && f->debug != NULL && k < f->debug->region_count; k += 1)
    {
        const IrDebugRegion before = f->debug->regions[k];
        const IrDebugRegion after = scheduled->debug->regions[k];
        const uint32_t value = before.kind == IR_DEBUG_BIND_VALUE ? place[before.value] : BUSTER_IR_NONE;
        valid = before.local == after.local && before.block == after.block && before.begin == after.begin && before.end == after.end &&
                before.literal == after.literal && before.kind == after.kind && before.bytes == after.bytes && before.type == after.type &&
                before.next == after.next && before.block_end == after.block_end && after.value == value;
    }
    for (uint32_t b = 0; valid && b < f->block_count; b += 1)
    {
        const uint32_t first = f->block_offsets[b];
        const uint32_t end = f->block_offsets[b + 1];
        uint32_t ordered = BUSTER_IR_NONE;
        for (uint32_t o = first; valid && o < end; o += 1)
        {
            const uint32_t i = place[o];
            valid = i >= first && i < end && (!tier1_schedule_ir_barrier(f, o) || i == o);
            valid = valid && scheduled->op[i] == f->op[o] && scheduled->type[i] == f->type[o] && scheduled->flags[i] == f->flags[o] &&
                    scheduled->alignment_log2[i] == f->alignment_log2[o];
            valid = valid && ((f->debug == NULL && scheduled->debug == NULL) ||
                             (f->debug != NULL && scheduled->debug != NULL && scheduled->debug->origins[i] == f->debug->origins[o]));
            const uint64_t count = valid ? ir_operand_count(f, o) : 0;
            valid = valid && ir_operand_count(scheduled, i) == count;
            for (uint32_t p = 0; valid && p < count; p += 1)
            {
                const uint32_t value = ir_operand(f, o, p, 0).value;
                valid = value < n && ir_operand(scheduled, i, p, 0).value == place[value];
                // A phi's incoming value may be defined later in its block (a loop's back edge): only the others' order.
                valid = valid && (f->op[o] == IR_PHI || value < first || value >= end || place[value] < i);
            }
            if (valid && tier1_schedule_ir_ordered(f->op[o]))
            {
                valid = ordered == BUSTER_IR_NONE || place[ordered] < i;
                ordered = o;
            }
        }
    }
    return valid;
}

#endif
