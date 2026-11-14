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

// The operations that are barriers whatever their flags, but for the terminators (ir_terminator_table's): a byte each, so
// that the operand sink's scan looks them up 64 at a time.
static const uint8_t tier1_schedule_ir_barriers[BUSTER_IR_OP_TABLE] = {
    [IR_PHI] = 1,
    [IR_PARAM] = 1,
    [IR_CALL] = 1,
    [IR_CALL_INDIRECT] = 1,
    [IR_ALLOCA] = 1,
    [IR_STACK_SAVE] = 1,
    [IR_STACK_RESTORE] = 1,
    [IR_LIFETIME] = 1,
    [IR_FENCE] = 1,
    [IR_ATOMIC_RMW] = 1,
    [IR_INTRINSIC] = 1,
    [IR_MASKED_LOAD] = 1,
    [IR_MASKED_STORE] = 1,
    [IR_COMPRESS_STORE] = 1,
    [IR_GATHER] = 1,
};

static bool tier1_schedule_ir_barrier(const IrFunction* f, uint32_t i)
{
    const uint32_t op = f->op[i];
    return tier1_schedule_ir_barriers[op] != 0 || ir_terminator_table[op] != 0 || (op == IR_LOAD && (f->flags[i] & BUSTER_IR_OBSERVABLE) != 0);
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

// Value `v` renamed by `place`, the new places of [first, end)'s instructions (`place[old - first]`); others keep theirs.
static uint32_t tier1_schedule_ir_renamed(uint32_t v, uint32_t first, uint32_t end, const uint32_t* place)
{
    return v >= first && v < end ? place[v - first] : v;
}

// The value operands of instruction i renamed (tier1_schedule_ir_renamed): a, b and c by ir_value_mask, a phi's incomings
// and a call's arguments in extra. `renamed` marks the extra slots done, so that none is renamed twice.
static void tier1_schedule_ir_rename(IrFunction* f, uint32_t i, uint32_t first, uint32_t end, const uint32_t* place, uint64_t* renamed)
{
    const uint32_t mask = ir_value_mask(f, i);
    f->a[i] = (mask & 1) ? tier1_schedule_ir_renamed(f->a[i], first, end, place) : f->a[i];
    f->b[i] = (mask & 2) ? tier1_schedule_ir_renamed(f->b[i], first, end, place) : f->b[i];
    f->c[i] = (mask & 4) ? tier1_schedule_ir_renamed(f->c[i], first, end, place) : f->c[i];
    const uint32_t op = f->op[i];
    const uint32_t count = op == IR_PHI ? f->b[i] : op == IR_CALL || op == IR_CALL_INDIRECT ? f->calls[f->b[i]].argument_count : 0;
    for (uint32_t p = 0; p < count; p += 1)
    {
        const uint32_t slot = op == IR_PHI ? f->a[i] + p * 2 + 1 : f->calls[f->b[i]].argument_first + p;
        const uint64_t bit = UINT64_C(1) << (slot & 63);
        if ((renamed[slot / 64] & bit) == 0)
        {
            f->extra[slot] = tier1_schedule_ir_renamed(f->extra[slot], first, end, place);
            renamed[slot / 64] |= bit;
        }
    }
}

// Applies a permutation of [first, end) within blocks, `order[new - first] = old` (every other instruction keeps its
// place), to `f`'s instruction columns, its debug origins, bindings and regions, and renames every value operand by its
// inverse, which it writes to `place` (`place[old - first] = new`). The range's columns' copies come from `scratch`,
// popped before it returns. False, with `f` unchanged, where there's no room.
static bool tier1_schedule_ir_permute(IrFunction* f, uint32_t first, uint32_t end, const uint32_t* order, uint32_t* place, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint32_t n = f->instruction_count;
    const uint32_t span = end - first;
    uint8_t* bytes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)span + 1);
    uint32_t* words = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)span + 1);
    uint64_t* renamed = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)f->extra_count / 64 + 1);
    const bool ready = bytes != 0 && words != 0 && renamed != 0;
    if (ready)
    {
        for (uint32_t k = 0; k < span; k += 1)
        {
            place[order[k] - first] = first + k;
        }
        uint8_t* byte_columns[] = { f->op, f->type, f->flags, f->alignment_log2 };
        uint32_t* word_columns[] = { f->a, f->b, f->c };
        for (uint32_t c = 0; c < 4; c += 1)
        {
            memcpy(bytes, byte_columns[c] + first, span);
            for (uint32_t k = 0; k < span; k += 1)
            {
                byte_columns[c][first + k] = bytes[order[k] - first];
            }
        }
        for (uint32_t c = 0; c < 3; c += 1)
        {
            memcpy(words, word_columns[c] + first, (uint64_t)span * sizeof(uint32_t));
            for (uint32_t k = 0; k < span; k += 1)
            {
                word_columns[c][first + k] = words[order[k] - first];
            }
        }
        if (f->debug != NULL)
        {
            memcpy(words, f->debug->origins + first, (uint64_t)span * sizeof(uint32_t));
            for (uint32_t k = 0; k < span; k += 1)
            {
                f->debug->origins[first + k] = words[order[k] - first];
            }
            for (uint32_t k = 0; k < f->debug->binding_count; k += 1)
            {
                IrDebugBinding* binding = f->debug->bindings + k;
                binding->anchor = binding->anchor != BUSTER_IR_NONE ? tier1_schedule_ir_renamed(binding->anchor, first, end, place) : BUSTER_IR_NONE;
                if (binding->kind == IR_DEBUG_BIND_VALUE)
                {
                    binding->value = tier1_schedule_ir_renamed(binding->value, first, end, place);
                }
            }
            for (uint32_t k = 0; k < f->debug->region_count; k += 1)
            {
                IrDebugRegion* region = f->debug->regions + k;
                if (region->kind == IR_DEBUG_BIND_VALUE)
                {
                    region->value = tier1_schedule_ir_renamed(region->value, first, end, place);
                }
            }
        }
        memset(renamed, 0, ((uint64_t)f->extra_count / 64 + 1) * sizeof(uint64_t));
        for (uint32_t i = 0; i < n; i += 1)
        {
            tier1_schedule_ir_rename(f, i, first, end, place, renamed);
        }
    }
    arena_pop_to(scratch, mark);
    return ready;
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
    ready = ready && (stats->moved == 0 || tier1_schedule_ir_permute(f, 0, n, w.order, w.place, scratch));
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

// The operand sink (#2592). Tier 1's C driver runs tier1_schedule_ir_sink on every function, after
// --tier1-schedule=pre's order where that runs and before the optimizer's final verification, on the same owned columns
// and with the same contract as tier1_schedule_ir_function. Its candidates are the phis' incoming values: each such root
// is its phi's next value, on an edge out of its block. Its expression is the root and the operands in its segment (the
// pre-scheduler's: nothing moves across a barrier) that no other of its instructions reads, each a pure, total, cheap
// operation of general integers or pointers (tier1_sink_ir_operations, tier1_sink_ir_types): constants, copies,
// conversions, address arithmetic and the integer operations that can't trap. An expression moves where:
// - its operands from outside it are one value `a` and constants, and `a` is of the phi's family (tier1_sink_ir_family):
//   the phi, a value computed from it, or a join of its values, which takes the phi's register;
// - some instruction of its segment after the root reads `a`: the last of them, its anchor (counting the readers of
//   the set's readers, which selection folds into their consumers), is none of the set and no truth value;
// - nothing reads the root before the anchor, nor any other of its instructions but the one of them that it is an
//   operand of: each of their values is defined before every reader that remains in the block.
// It then moves to right after its anchor, where `a`'s register is free and the root, defined there, takes it. Lua's
// `i = *(pc++)` defines pc + 4 before the load that reads pc, so that the incremented pc took another register and every
// dispatch moved pc and i back to their phis' registers through the scratch register (#2592). Where `a` has its own
// successor that wants its register more, as a list's `p = p->next` beside `pp = &p->next` does, `a` is no value of the
// other expression's phi's family, and the expression keeps its place: moving it would give that register to it.
//
// It never raises register pressure: between the expression's place and its new one, its root was live before and isn't
// now, and `a` is live now and may have been before; its inner values move with it and stay adjacent. It changes no
// selection: its anchor stays where it is, a truth value's branch or extension still takes it from right before them
// (tier1_select_truth), and the expression has no memory access, so a load still folds into its consumer where it did
// (tier1_select_load_folds). Each expression has at most BUSTER_TIER1_SINK_NODES instructions within
// BUSTER_TIER1_SINK_WINDOW before its root, its anchor is in the BUSTER_TIER1_SINK_WINDOW instructions after the root,
// and each walk up to the phi is bounded too: the pass's work is bounded by the instructions' and the phis' operands.
//
// No use count is taken: the window's readers say what the moves need. Two scans are data-parallel, each with an exact
// scalar twin: the classification, 64 instructions' opcode, type and flag bytes at a time into the set's, the barriers'
// and the phis' bits (tier1_sink_ir_classify); and a value's readers among at most 64 instructions, 16 instructions'
// value operands at a time (tier1_sink_ir_readers). The expressions' walks are short dependent chains, scalar. A function
// with nothing to move allocates nothing per instruction but those bits; one with something permutes only the span from
// its lowest moved instruction to its last anchor.

#define BUSTER_TIER1_SINK_NODES 8u
#define BUSTER_TIER1_SINK_WINDOW 32u
// The longest chain of operations tier1_sink_ir_origin walks up from a value to the one it comes from.
#define BUSTER_TIER1_SINK_FOLLOW 8u

typedef struct Tier1SinkIrStats Tier1SinkIrStats;
struct Tier1SinkIrStats
{
    // Expressions moved, and the instructions they hold.
    uint32_t trees;
    uint32_t moved;
};

// The operations an expression may hold.
static const uint8_t tier1_sink_ir_operations[BUSTER_IR_OP_TABLE] = {
    [IR_CONST] = 1,
    [IR_COPY] = 1,
    [IR_ADD] = 1,
    [IR_SUB] = 1,
    [IR_MUL] = 1,
    [IR_AND] = 1,
    [IR_OR] = 1,
    [IR_XOR] = 1,
    [IR_SHL] = 1,
    [IR_LSHR] = 1,
    [IR_ASHR] = 1,
    [IR_NEG] = 1,
    [IR_NOT] = 1,
    [IR_TRUNC] = 1,
    [IR_ZEXT] = 1,
    [IR_SEXT] = 1,
    [IR_INTTOPTR] = 1,
    [IR_PTRTOINT] = 1,
    [IR_ADDRESS] = 1,
};

// The types of an expression's values and of its operands: the general registers' (IrType's fit 64 entries, one byte
// permute).
static const uint8_t tier1_sink_ir_types[64] = {
    [IR_I8] = 1,
    [IR_I16] = 1,
    [IR_I32] = 1,
    [IR_I64] = 1,
    [IR_PTR] = 1,
};

_Static_assert(IR_TYPE_COUNT <= 64, "the operand sink's type table is one byte permute");

// The most expressions a function's sink moves: past it, the rest keep their places.
#define BUSTER_TIER1_SINK_TREES 4096u

// An expression that moves: its root, its anchor (the last reader it goes after) and its instructions in place order.
typedef struct Tier1SinkIrTree Tier1SinkIrTree;
struct Tier1SinkIrTree
{
    uint32_t root;
    uint32_t anchor;
    uint32_t count;
    uint32_t nodes[BUSTER_TIER1_SINK_NODES];
};

typedef struct Tier1SinkIrWork Tier1SinkIrWork;
struct Tier1SinkIrWork
{
    // A bit per instruction: an operation of the set of a general type, a barrier, a phi, and an instruction that moves.
    uint64_t* pure;
    uint64_t* barriers;
    uint64_t* phis;
    uint64_t* moved;
    Tier1SinkIrTree* trees;
    uint32_t capacity;
};

static bool tier1_sink_ir_bit(const uint64_t* bits, uint32_t i)
{
    return ((bits[i / 64] >> (i % 64)) & 1) != 0;
}

// The first set bit of `bits` at or after `i` and before `count`, or `count`.
static uint32_t tier1_sink_ir_next(const uint64_t* bits, uint32_t i, uint32_t count)
{
    uint32_t word = i / 64;
    uint64_t rest = i < count ? bits[word] & (UINT64_MAX << (i % 64)) : 0;
    const uint32_t words = (count + 63) / 64;
    while (rest == 0 && word + 1 < words && i < count)
    {
        word += 1;
        rest = bits[word];
    }
    const uint32_t found = rest != 0 ? word * 64 + (uint32_t)__builtin_ctzll(rest) : count;
    return found < count ? found : count;
}

// The last set bit of `bits` before `i` and at or after `floor`, or BUSTER_IR_NONE.
static uint32_t tier1_sink_ir_previous(const uint64_t* bits, uint32_t i, uint32_t floor)
{
    uint32_t word = i / 64;
    uint64_t rest = i % 64 != 0 ? bits[word] & (UINT64_MAX >> (64 - i % 64)) : 0;
    while (rest == 0 && word != 0 && word * 64 > floor)
    {
        word -= 1;
        rest = bits[word];
    }
    const uint32_t found = rest != 0 ? word * 64 + 63 - (uint32_t)__builtin_clzll(rest) : BUSTER_IR_NONE;
    return found != BUSTER_IR_NONE && found >= floor ? found : BUSTER_IR_NONE;
}

// The bits of the set, of the barriers and of the phis, 64 instructions at a time; a word's bits past the count are clear.
static void tier1_sink_ir_classify(const IrFunction* f, uint64_t* pure, uint64_t* barriers, uint64_t* phis, bool scalar)
{
    const uint32_t count = f->instruction_count;
    for (uint32_t first = 0; first < count; first += 64)
    {
        const uint32_t lanes = count - first < 64 ? count - first : 64;
        uint64_t set = 0;
        uint64_t cut = 0;
        uint64_t joins = 0;
#if BUSTER_HOST_VECTOR
        if (!scalar)
        {
            const __mmask64 valid = UINT64_MAX >> (64 - lanes);
            const __m512i op = _mm512_maskz_loadu_epi8(valid, f->op + first);
            const __m512i type = _mm512_maskz_loadu_epi8(valid, f->type + first);
            const __m512i flags = _mm512_maskz_loadu_epi8(valid, f->flags + first);
            const __m512i operations = _mm512_permutex2var_epi8(_mm512_loadu_si512(tier1_sink_ir_operations), op, _mm512_loadu_si512(tier1_sink_ir_operations + 64));
            const __m512i types = _mm512_permutexvar_epi8(type, _mm512_loadu_si512(tier1_sink_ir_types));
            const __m512i fixed = _mm512_permutex2var_epi8(_mm512_loadu_si512(tier1_schedule_ir_barriers), op, _mm512_loadu_si512(tier1_schedule_ir_barriers + 64));
            const __m512i terminators = _mm512_permutex2var_epi8(_mm512_loadu_si512(ir_terminator_table), op, _mm512_loadu_si512(ir_terminator_table + 64));
            const __mmask64 observable = _mm512_cmpeq_epi8_mask(op, _mm512_set1_epi8(IR_LOAD)) & _mm512_test_epi8_mask(flags, _mm512_set1_epi8((char)BUSTER_IR_OBSERVABLE));
            set = valid & _mm512_test_epi8_mask(operations, types);
            cut = valid & (_mm512_test_epi8_mask(fixed, fixed) | _mm512_test_epi8_mask(terminators, terminators) | observable);
            joins = valid & _mm512_cmpeq_epi8_mask(op, _mm512_set1_epi8(IR_PHI));
        }
        else
#endif
        {
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                const uint32_t i = first + lane;
                const uint64_t member = tier1_sink_ir_operations[f->op[i]] != 0 && tier1_sink_ir_types[f->type[i]] != 0;
                const uint64_t barrier = tier1_schedule_ir_barrier(f, i);
                const uint64_t join = f->op[i] == IR_PHI;
                set |= member << lane;
                cut |= barrier << lane;
                joins |= join << lane;
            }
        }
        pure[first / 64] = set;
        barriers[first / 64] = cut;
        phis[first / 64] = joins;
    }
    (void)scalar;
}

// The instructions of [from, to), at most 64 of a segment's (whose operands are their a, b and c alone, by their value
// masks), that read `value`, as bits from `from`; 16 at a time.
static uint64_t tier1_sink_ir_readers(const IrFunction* f, uint32_t from, uint32_t to, uint32_t value, bool scalar)
{
    uint64_t read = 0;
#if BUSTER_HOST_VECTOR
    if (!scalar)
    {
        const uint32_t* columns[3] = { f->a, f->b, f->c };
        for (uint32_t first = from; first < to; first += 16)
        {
            const uint32_t lanes = to - first < 16 ? to - first : 16;
            const __mmask16 valid = (__mmask16)(0xffffu >> (16 - lanes));
            const __m128i op = _mm_maskz_loadu_epi8(valid, f->op + first);
            const __m512i fixed = ir_lookup_bytes(ir_operand_table, op);
            const __m512i optional = ir_lookup_bytes(ir_optional_table, op);
            __mmask16 reads = 0;
            for (uint32_t k = 0; k < 3; k += 1)
            {
                const __m512i bit = _mm512_set1_epi32((int)(1u << k));
                const __m512i operand = _mm512_maskz_loadu_epi32(valid, columns[k] + first);
                const __mmask16 present = _mm512_cmpneq_epi32_mask(operand, _mm512_set1_epi32(-1));
                const __mmask16 live = valid & (_mm512_test_epi32_mask(fixed, bit) | (_mm512_test_epi32_mask(optional, bit) & present));
                reads |= live & _mm512_cmpeq_epi32_mask(operand, _mm512_set1_epi32((int)value));
            }
            read |= (uint64_t)reads << (first - from);
        }
    }
    else
#endif
    {
        for (uint32_t j = from; j < to; j += 1)
        {
            const uint32_t mask = ir_value_mask(f, j);
            const bool reads = ((mask & 1) != 0 && f->a[j] == value) || ((mask & 2) != 0 && f->b[j] == value) || ((mask & 4) != 0 && f->c[j] == value);
            read |= (uint64_t)reads << (j - from);
        }
    }
    (void)scalar;
    return read;
}

// The value `v` comes from: through the set's operations of one value operand (and constants), at most
// BUSTER_TIER1_SINK_FOLLOW of them, the first value that isn't one.
static uint32_t tier1_sink_ir_origin(const IrFunction* f, uint32_t v)
{
    uint32_t value = v;
    bool walking = true;
    for (uint32_t step = 0; walking && step < BUSTER_TIER1_SINK_FOLLOW; step += 1)
    {
        const uint32_t mask = tier1_sink_ir_operations[f->op[value]] != 0 ? ir_value_mask(f, value) : 0;
        uint32_t operand = BUSTER_IR_NONE;
        uint32_t operands = 0;
        for (uint32_t p = 0; p < 3; p += 1)
        {
            const uint32_t w = p == 0 ? f->a[value] : p == 1 ? f->b[value] : f->c[value];
            const bool counted = ((mask >> p) & 1) != 0 && f->op[w] != IR_CONST && f->op[w] != IR_UNDEF;
            operand = counted ? w : operand;
            operands += counted;
        }
        walking = operands == 1;
        value = walking ? operand : value;
    }
    return value;
}

// Whether `a` is of `phi`'s family: it comes from the phi (tier1_sink_ir_origin), or from a phi one of whose first
// BUSTER_TIER1_SINK_WINDOW incoming values does, as a dispatch's handler's join of pc's paths.
static bool tier1_sink_ir_family(const IrFunction* f, uint32_t a, uint32_t phi)
{
    const uint32_t origin = tier1_sink_ir_origin(f, a);
    bool family = origin == phi;
    const uint32_t incoming = !family && f->op[origin] == IR_PHI ? (f->b[origin] < BUSTER_TIER1_SINK_WINDOW ? f->b[origin] : BUSTER_TIER1_SINK_WINDOW) : 0;
    for (uint32_t p = 0; !family && p < incoming; p += 1)
    {
        family = tier1_sink_ir_origin(f, f->extra[f->a[origin] + p * 2 + 1]) == phi;
    }
    return family;
}

// Root's expression as `phi`'s next value, recorded where it moves (see the section's comment). Its instructions are in
// [start, root], start past the last barrier before the root, at most BUSTER_TIER1_SINK_WINDOW before it; its readers
// are found in [root + 1, to), to the next barrier, at most BUSTER_TIER1_SINK_WINDOW after it.
static void tier1_sink_ir_candidate(const IrFunction* f, Tier1SinkIrWork* w, uint32_t root, uint32_t phi, uint32_t start, uint32_t to, bool scalar, Tier1SinkIrStats* stats)
{
    // The expression: the root and the operands of the set in its segment that no other of its instructions reads; each
    // one's parent is the instruction of it that reads it.
    uint32_t nodes[BUSTER_TIER1_SINK_NODES];
    uint32_t parents[BUSTER_TIER1_SINK_NODES];
    uint32_t count = 1;
    nodes[0] = root;
    parents[0] = BUSTER_IR_NONE;
    uint32_t leaf = BUSTER_IR_NONE;
    bool valid = true;
    for (uint32_t k = 0; valid && k < count; k += 1)
    {
        const uint32_t node = nodes[k];
        const uint32_t mask = ir_value_mask(f, node);
        for (uint32_t p = 0; valid && p < 3; p += 1)
        {
            const uint32_t v = p == 0 ? f->a[node] : p == 1 ? f->b[node] : f->c[node];
            const bool operand = ((mask >> p) & 1) != 0;
            bool repeated = false;
            for (uint32_t q = 0; operand && q < count; q += 1)
            {
                repeated |= nodes[q] == v;
            }
            const bool own = operand && !repeated && v >= start && v < node && tier1_sink_ir_bit(w->pure, v) && !tier1_sink_ir_bit(w->moved, v);
            const bool constant = operand && !own && (f->op[v] == IR_CONST || f->op[v] == IR_UNDEF);
            valid = !operand || (tier1_sink_ir_types[f->type[v]] != 0 && !repeated && (own ? count < BUSTER_TIER1_SINK_NODES : constant || leaf == BUSTER_IR_NONE || leaf == v));
            if (valid && own)
            {
                nodes[count] = v;
                parents[count] = node;
                count += 1;
            }
            leaf = valid && operand && !own && !constant ? v : leaf;
        }
    }
    // `a` is of the phi's family (tier1_sink_ir_family): the register the expression's result wants is the phi's, which
    // `a` holds. Where another value wants `a`'s register more, as a list's `p = p->next` beside `pp = &p->next` does,
    // the expression keeps its place.
    valid = valid && leaf != BUSTER_IR_NONE && tier1_sink_ir_family(f, leaf, phi);
    // `a`'s readers after the root, and the readers of those of the set, as selection folds them into their consumers:
    // the last of them is the anchor.
    uint32_t members[BUSTER_TIER1_SINK_NODES];
    uint32_t known = 1;
    members[0] = leaf;
    uint64_t read = 0;
    for (uint32_t k = 0; valid && k < known; k += 1)
    {
        const uint64_t readers = tier1_sink_ir_readers(f, root + 1, to, members[k], scalar);
        read |= readers;
        for (uint64_t left = readers; valid && left != 0; left &= left - 1)
        {
            const uint32_t x = root + 1 + (uint32_t)__builtin_ctzll(left);
            bool listed = false;
            for (uint32_t q = 0; q < known; q += 1)
            {
                listed |= members[q] == x;
            }
            const bool member = tier1_sink_ir_bit(w->pure, x) && !listed;
            valid = !member || known < BUSTER_TIER1_SINK_NODES;
            if (valid && member)
            {
                members[known] = x;
                known += 1;
            }
        }
    }
    const uint32_t anchor = read != 0 ? root + 1 + 63 - (uint32_t)__builtin_clzll(read) : root;
    valid = valid && anchor != root && !tier1_sink_ir_bit(w->pure, anchor) && f->type[anchor] != IR_I1;
    // Nothing reads the expression's values before their new place but their parents in it, and nothing the root: each
    // instruction's readers up to the anchor, at most 2 * BUSTER_TIER1_SINK_WINDOW + 1 of them.
    for (uint32_t k = 0; valid && k < count; k += 1)
    {
        const uint64_t readers = tier1_sink_ir_readers(f, nodes[k] + 1, anchor + 1, nodes[k], scalar);
        valid = readers == (parents[k] != BUSTER_IR_NONE ? UINT64_C(1) << (parents[k] - nodes[k] - 1) : 0);
    }
    valid = valid && stats->trees < w->capacity;
    if (valid)
    {
        Tier1SinkIrTree* tree = &w->trees[stats->trees];
        tree->root = root;
        tree->anchor = anchor;
        tree->count = count;
        // Place order: an insertion sort of at most BUSTER_TIER1_SINK_NODES.
        for (uint32_t k = 0; k < count; k += 1)
        {
            uint32_t at = k;
            while (at != 0 && tree->nodes[at - 1] > nodes[k])
            {
                tree->nodes[at] = tree->nodes[at - 1];
                at -= 1;
            }
            tree->nodes[at] = nodes[k];
            w->moved[nodes[k] / 64] |= UINT64_C(1) << (nodes[k] % 64);
        }
        stats->trees += 1;
        stats->moved += count;
    }
}

// Moves `f`'s expressions in place (see the section's comment), by the vector scans or their scalar twins, with scratch
// from `scratch`, popped before it returns; with `order` not null and an expression moved, stores the permutation there,
// order[new] = old, of instruction_count elements. Returns IR_ERROR_MEMORY, f unchanged, when there's no room.
static __attribute__((unused)) IrError tier1_schedule_ir_sink(IrFunction* f, Arena* scratch, uint32_t* order, bool scalar, Tier1SinkIrStats* stats)
{
    const uint64_t mark = scratch->length;
    const uint32_t n = f->instruction_count;
    const uint64_t words = (uint64_t)n / 64 + 1;
    *stats = (Tier1SinkIrStats){ 0 };
    Tier1SinkIrWork w = {
        .pure = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, words),
        .barriers = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, words),
        .phis = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, words),
        .moved = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, words),
        .capacity = n < BUSTER_TIER1_SINK_TREES ? n : BUSTER_TIER1_SINK_TREES,
    };
    w.trees = BUSTER_ARENA_PUSH_ARRAY(scratch, Tier1SinkIrTree, (uint64_t)w.capacity + 1);
    bool ready = w.pure != 0 && w.barriers != 0 && w.phis != 0 && w.moved != 0 && w.trees != 0;
    if (ready && n != 0)
    {
        memset(w.moved, 0, words * sizeof(uint64_t));
        tier1_sink_ir_classify(f, w.pure, w.barriers, w.phis, scalar);
        // Each phi's incoming values of the set, in place order: a value that already moved, as another phi's, stays.
        for (uint32_t phi = tier1_sink_ir_next(w.phis, 0, n); phi < n; phi = tier1_sink_ir_next(w.phis, phi + 1, n))
        {
            for (uint32_t p = 0; p < f->b[phi]; p += 1)
            {
                const uint32_t root = f->extra[f->a[phi] + p * 2 + 1];
                if (root < n && tier1_sink_ir_bit(w.pure, root) && !tier1_sink_ir_bit(w.moved, root))
                {
                    const uint32_t floor = root > BUSTER_TIER1_SINK_WINDOW ? root - BUSTER_TIER1_SINK_WINDOW : 0;
                    const uint32_t previous = tier1_sink_ir_previous(w.barriers, root, floor);
                    const uint32_t start = previous != BUSTER_IR_NONE ? previous + 1 : floor;
                    const uint32_t limit = n - root - 1 < BUSTER_TIER1_SINK_WINDOW ? n : root + 1 + BUSTER_TIER1_SINK_WINDOW;
                    const uint32_t to = tier1_sink_ir_next(w.barriers, root + 1, limit);
                    tier1_sink_ir_candidate(f, &w, root, phi, start, to, scalar, stats);
                }
            }
        }
    }
    // The new order of [low, high], from the lowest instruction that moves to the last anchor, every other instruction
    // keeping its place: each one that stays, each anchor followed by its expressions, by their roots.
    uint32_t low = n;
    uint32_t high = 0;
    for (uint32_t t = 0; t < stats->trees; t += 1)
    {
        low = w.trees[t].nodes[0] < low ? w.trees[t].nodes[0] : low;
        high = w.trees[t].anchor > high ? w.trees[t].anchor : high;
    }
    const uint32_t span = stats->trees != 0 ? high - low + 1 : 0;
    uint32_t* first = ready && span != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)span + 1) : 0;
    uint32_t* next = first != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)stats->trees + 1) : 0;
    uint32_t* moves = next != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)span + 1) : 0;
    uint32_t* place = moves != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)span + 1) : 0;
    ready = ready && (span == 0 || place != 0);
    if (ready && span != 0)
    {
        memset(first, 0xff, (uint64_t)span * sizeof(uint32_t));
        for (uint32_t t = 0; t < stats->trees; t += 1)
        {
            uint32_t* link = &first[w.trees[t].anchor - low];
            while (*link != BUSTER_IR_NONE && w.trees[*link].root < w.trees[t].root)
            {
                link = &next[*link];
            }
            next[t] = *link;
            *link = t;
        }
        uint32_t output = 0;
        for (uint32_t i = low; i <= high; i += 1)
        {
            if (!tier1_sink_ir_bit(w.moved, i))
            {
                moves[output] = i;
                output += 1;
            }
            for (uint32_t t = first[i - low]; t != BUSTER_IR_NONE; t = next[t])
            {
                for (uint32_t k = 0; k < w.trees[t].count; k += 1)
                {
                    moves[output] = w.trees[t].nodes[k];
                    output += 1;
                }
            }
        }
        ready = tier1_schedule_ir_permute(f, low, high + 1, moves, place, scratch);
        for (uint32_t i = 0; ready && order != 0 && i < n; i += 1)
        {
            order[i] = i >= low && i <= high ? moves[i - low] : i;
        }
    }
    arena_pop_to(scratch, mark);
    if (!ready)
    {
        *stats = (Tier1SinkIrStats){ 0 };
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
