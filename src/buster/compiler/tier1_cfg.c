// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's register allocation over a CFG (#140). Include after lane.c, dataflow.c and tier1_allocate.c.
//
// tier1_cfg_allocate takes a machine CFG in layout order. Block b occupies positions [block_starts[b],
// block_starts[b + 1]); block 0 is the entry, without predecessors or phis. Every value is defined once: a phi at its
// block's start, by the moves on the block's incoming edges, any other value at its definition position, before its
// uses in that block (SSA). A phi has one operand per predecessor, in the predecessor CSR's order. A use of a value
// other than a phi at its own definition position is the definition's requirement: a required one asks that the
// definition write a register, as an instruction's destination does (tier1_emit_cfg.c's).
//
// 1. Liveness is dataflow.c's, on the values used outside their definition's block (phi operands included), with
//    each phi operand a use on its edge: USE[b] = U[b] | (P[b] & ~D[b]), then OUT[b] |= P[b] after the solve.
// 2. A value gets an interval per block it is defined in or live into, from its definition or the block's start to its
//    last use or the block's end, and an unused definition one position. A block continues its layout predecessor's
//    ranges when it has no phis and every value live into it leaves that predecessor live: then every value of its
//    entry state continues, so that a fragment transition at its start, which runs inside the block, never meets a
//    value an edge brought there. A range is cut around each fixed range of its class that blocks every register the
//    value may take (a call's clobbers for a double): the allocator takes a fixed range's registers from every
//    interval it overlaps, so the piece over it goes to memory and the pieces around it keep their registers, where
//    the whole range couldn't have one for a use that needs it. Intervals are numbered by value, then position, so
//    that each one's uses are a slice of the value's: the allocator borrows the input's use columns. Each value's
//    blocks come from the liveness rows block by block, counted and then filled, so the cost is the live pairs', not
//    values times blocks.
// 3. tier1_allocate, or its scalar twin, assigns the intervals.
// 4. Each edge moves its successor's live-in values and phis from their locations at the predecessor's last position
//    to their entry locations (tier1_cfg_entry_location). tier1_resolve_moves orders each class's moves, breaking a
//    cycle with the class's scratch register or, where one of the class's moves goes slot to slot, with the class's
//    temporary slot; a slot-to-slot move then goes through the scratch register, which holds nothing from one move to
//    the next. Recomputing a rematerializable value comes last. The moves go at the predecessor's end when it has one
//    successor (after its last instruction: a jump there must read no allocated value), at the successor's start when
//    it has one predecessor, and otherwise on the split edge.
// 5. Each split, a position inside a block where pieces of values' ranges meet, moves each of those values from its
//    location just before the position to its new piece's, resolved as an edge's moves are (a value's slot is its
//    own, so none goes slot to slot). They run at the position after its stores and before its reloads.
//
// Location ids: class * 32 + register for registers, BUSTER_TIER1_CFG_SPILL + the value's spill id for its slot, and
// BUSTER_TIER1_CFG_SPILL + spill_count + class for the class's temporary slot, which temp_slots marks where a move
// uses it. A move whose source is BUSTER_TIER1_CFG_REMATERIALIZE recomputes move_values[m] into its destination.
//
// tier1_cfg_verify is an independent oracle for tests and qualification, not the production path. It computes
// liveness its own way (each use walked back to its definition), simulates every block's locations as the emitter
// would run the fragments and splits (stores, then a split's moves, then loads and rematerializations, at each
// position; fixed ranges clobber their registers), checks that every use finds its value and that no write lands on
// another value the allocation still keeps there, and applies each edge's moves in order to the predecessor's final
// state, which must then hold every live-in value and each phi's operand where the successor expects them.
//
// This first form keeps interval construction, resolution and the oracle scalar, around the vector liveness solver and
// the vector allocator: their share of the cost is to be measured on the 9700X before they get vector paths.
// Inputs are borrowed and immutable. Output and scratch are distinct arenas; failure restores both lengths and leaves
// the result unchanged.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BUSTER_TIER1_CFG_SPILL 64u
#define BUSTER_TIER1_CFG_REMATERIALIZE 0x80000000u
#define BUSTER_TIER1_CFG_NONE UINT32_MAX

typedef enum Tier1CfgPlacement
{
    TIER1_CFG_EDGE_EMPTY,
    TIER1_CFG_EDGE_PREDECESSOR,
    TIER1_CFG_EDGE_SUCCESSOR,
    TIER1_CFG_EDGE_SPLIT,
} Tier1CfgPlacement;

typedef struct Tier1CfgInput Tier1CfgInput;
struct Tier1CfgInput
{
    // block_count + 1 entries: block b is [block_starts[b], block_starts[b + 1]); the last entry is position_count.
    const uint32_t* block_starts;
    // Both CSR views of the edges, without duplicates. Phi operands follow the predecessor view's order.
    const uint32_t* successor_offsets;
    const uint32_t* successors;
    const uint32_t* predecessor_offsets;
    const uint32_t* predecessors;
    // Per value.
    const uint8_t* classes;
    const uint32_t* allowed_registers;
    const uint32_t* definitions;
    // Optional: a value that may be recomputed rather than spilled.
    const uint8_t* rematerializable;
    // Ordinary uses, sorted per value: not phi operands. use_required is optional, as tier1_allocate takes it.
    const uint32_t* use_offsets;
    const uint32_t* use_positions;
    const uint8_t* use_required;
    // Block b's phis are phi_values[phi_offsets[b] .. phi_offsets[b + 1]); phi k's operand from its block's j-th
    // predecessor is phi_operands[phi_operand_offsets[k] + j].
    const uint32_t* phi_offsets;
    const uint32_t* phi_values;
    const uint32_t* phi_operand_offsets;
    const uint32_t* phi_operands;
    uint32_t block_count;
    uint32_t edge_count;
    uint32_t value_count;
    uint32_t use_count;
    uint32_t phi_count;
    uint32_t position_count;
    uint32_t registers[BUSTER_TIER1_ALLOCATE_CLASSES];
    // Each class's register for move cycles and memory-to-memory moves: outside `registers`, never allocated.
    uint8_t scratch_registers[BUSTER_TIER1_ALLOCATE_CLASSES];
    Tier1FixedIntervals fixed;
};

typedef struct Tier1CfgAllocation Tier1CfgAllocation;
struct Tier1CfgAllocation
{
    // The allocator's input, which borrows the CFG input's use columns, and its result.
    Tier1AllocationInput intervals;
    Tier1Allocation allocation;
    // Value v's intervals, in position order, are [value_interval_offsets[v], value_interval_offsets[v + 1]).
    uint32_t* value_interval_offsets;
    // Edge e (a successor CSR index) has moves [edge_move_offsets[e], edge_move_offsets[e + 1]), in their order.
    uint8_t* edge_placements;
    uint32_t* edge_move_offsets;
    uint32_t* move_sources;
    uint32_t* move_destinations;
    uint32_t* move_values;
    uint32_t move_count;
    // Positions inside blocks where a value's range is cut around a fixed range that blocks it: split k, at
    // split_positions[k], has moves [split_move_offsets[k], split_move_offsets[k + 1]), after every edge's, which run
    // at that position after its stores and before its reloads.
    uint32_t* split_positions;
    uint32_t* split_move_offsets;
    uint32_t split_count;
    uint32_t split_edges;
    uint32_t cross_block_values;
    // Bit c: a move uses class c's temporary slot, which the frame must then reserve.
    uint8_t temp_slots;
    uint64_t liveness_iterations;
    uint64_t retained_bytes;
    uint64_t scratch_bytes;
};

typedef enum Tier1CfgError
{
    TIER1_CFG_OK,
    TIER1_CFG_INVALID,
    TIER1_CFG_CAPACITY,
    TIER1_CFG_CONSTRAINT,
} Tier1CfgError;

static uint32_t tier1_cfg_block_of(const Tier1CfgInput* input, uint32_t position)
{
    uint32_t low = 0;
    uint32_t high = input->block_count;
    while (high - low > 1)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (input->block_starts[middle] <= position)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

static bool tier1_cfg_input_valid(const Tier1CfgInput* input, Arena* scratch)
{
    const uint64_t saved = scratch->length;
    bool valid = input->block_count != 0 && input->block_count <= BUSTER_TIER1_ALLOCATE_MAX_INTERVALS && input->value_count <= BUSTER_TIER1_ALLOCATE_MAX_INTERVALS && input->position_count <= BUSTER_TIER1_ALLOCATE_MAX_POSITIONS && input->use_count <= BUSTER_TIER1_ALLOCATE_MAX_USES && input->edge_count <= BUSTER_TIER1_ALLOCATE_MAX_USES;
    valid = valid && input->phi_count <= input->value_count && input->block_starts != 0 && input->successor_offsets != 0 && input->predecessor_offsets != 0 && input->phi_offsets != 0 && input->phi_operand_offsets != 0;
    valid = valid && (input->edge_count == 0 || (input->successors != 0 && input->predecessors != 0));
    valid = valid && (input->value_count == 0 || (input->classes != 0 && input->allowed_registers != 0 && input->definitions != 0 && input->use_offsets != 0));
    valid = valid && (input->use_count == 0 || input->use_positions != 0) && (input->phi_count == 0 || (input->phi_values != 0 && input->phi_operands != 0));
    for (uint32_t c = 0; valid && c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
    {
        valid = input->scratch_registers[c] < 32 && ((input->registers[c] >> input->scratch_registers[c]) & 1) == 0;
    }
    valid = valid && (input->registers[0] & 0xffff0000u) == 0 && input->scratch_registers[0] < 16;
    valid = valid && input->block_starts[0] == 0 && input->block_starts[input->block_count] == input->position_count;
    valid = valid && input->fixed.count <= BUSTER_TIER1_ALLOCATE_MAX_INTERVALS && (input->fixed.count == 0 || (input->fixed.starts != 0 && input->fixed.ends != 0 && input->fixed.registers != 0 && input->fixed.classes != 0));
    for (uint32_t r = 0; valid && r < input->fixed.count; r += 1)
    {
        valid = input->fixed.starts[r] < input->fixed.ends[r] && input->fixed.ends[r] <= input->position_count && input->fixed.classes[r] < BUSTER_TIER1_ALLOCATE_CLASSES;
    }
    for (uint32_t b = 0; valid && b < input->block_count; b += 1)
    {
        valid = input->block_starts[b] < input->block_starts[b + 1];
    }
    valid = valid && input->successor_offsets[0] == 0 && input->successor_offsets[input->block_count] == input->edge_count;
    valid = valid && input->predecessor_offsets[0] == 0 && input->predecessor_offsets[input->block_count] == input->edge_count;
    valid = valid && input->predecessor_offsets[1] == 0;
    // The two views must describe one edge set: count each edge from both sides in one array.
    uint32_t* counts = valid ? arena_push_array(scratch, input->block_count, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    valid = valid && counts != 0;
    if (valid)
    {
        memset(counts, 0, (uint64_t)input->block_count * sizeof(uint32_t));
    }
    for (uint32_t b = 0; valid && b < input->block_count; b += 1)
    {
        valid = input->successor_offsets[b] <= input->successor_offsets[b + 1] && input->predecessor_offsets[b] <= input->predecessor_offsets[b + 1];
        for (uint32_t e = input->successor_offsets[b]; valid && e < input->successor_offsets[b + 1]; e += 1)
        {
            valid = input->successors[e] < input->block_count;
            for (uint32_t f = input->successor_offsets[b]; valid && f < e; f += 1)
            {
                valid = input->successors[f] != input->successors[e];
            }
            counts[valid ? input->successors[e] : 0] += valid;
        }
    }
    for (uint32_t b = 0; valid && b < input->block_count; b += 1)
    {
        for (uint32_t e = input->predecessor_offsets[b]; valid && e < input->predecessor_offsets[b + 1]; e += 1)
        {
            const uint32_t source = input->predecessors[e];
            valid = source < input->block_count;
            bool found = false;
            for (uint32_t f = valid ? input->successor_offsets[source] : 0; valid && !found && f < input->successor_offsets[source + 1]; f += 1)
            {
                found = input->successors[f] == b;
            }
            valid = valid && found;
        }
    }
    for (uint32_t b = 0; valid && b < input->block_count; b += 1)
    {
        valid = counts[b] == input->predecessor_offsets[b + 1] - input->predecessor_offsets[b];
    }
    // Phis: none in the entry; each value at most once, defined at its block's start, one operand of its class per
    // predecessor.
    valid = valid && input->phi_offsets[0] == 0 && input->phi_offsets[1] == 0 && input->phi_offsets[input->block_count] == input->phi_count && input->phi_operand_offsets[0] == 0;
    uint8_t* phi = valid ? arena_push(scratch, (uint64_t)input->value_count + 1, 1) : 0;
    valid = valid && phi != 0;
    if (valid)
    {
        memset(phi, 0, (uint64_t)input->value_count + 1);
    }
    for (uint32_t b = 0; valid && b < input->block_count; b += 1)
    {
        valid = input->phi_offsets[b] <= input->phi_offsets[b + 1];
        const uint32_t predecessors = input->predecessor_offsets[b + 1] - input->predecessor_offsets[b];
        for (uint32_t k = input->phi_offsets[b]; valid && k < input->phi_offsets[b + 1]; k += 1)
        {
            const uint32_t v = input->phi_values[k];
            // A phi has no recipe: its edges define it.
            valid = v < input->value_count && phi[v] == 0 && input->classes[v] < BUSTER_TIER1_ALLOCATE_CLASSES && input->definitions[v] == input->block_starts[b] && (input->rematerializable == 0 || input->rematerializable[v] == 0);
            valid = valid && input->phi_operand_offsets[k] + predecessors == input->phi_operand_offsets[k + 1];
            for (uint32_t j = 0; valid && j < predecessors; j += 1)
            {
                const uint32_t operand = input->phi_operands[input->phi_operand_offsets[k] + j];
                valid = operand < input->value_count && input->classes[operand] == input->classes[v];
            }
            if (valid)
            {
                phi[v] = 1;
            }
        }
    }
    // Every value: its class, registers and definition; its uses sorted, none before a definition in its own block (a
    // phi's definition precedes its block's first position, which may use it; another value's use at its own definition
    // is the definition's: required, the definition writes a register).
    valid = valid && (input->value_count == 0 || input->use_offsets[0] == 0) && (input->value_count == 0 || input->use_offsets[input->value_count] == input->use_count);
    for (uint32_t v = 0; valid && v < input->value_count; v += 1)
    {
        valid = input->classes[v] < BUSTER_TIER1_ALLOCATE_CLASSES && (input->allowed_registers[v] & ~input->registers[input->classes[v]]) == 0 && input->definitions[v] < input->position_count;
        valid = valid && (input->rematerializable == 0 || input->rematerializable[v] <= 1) && input->use_offsets[v] <= input->use_offsets[v + 1];
        const uint32_t block = valid ? tier1_cfg_block_of(input, input->definitions[v]) : 0;
        for (uint32_t u = input->use_offsets[v]; valid && u < input->use_offsets[v + 1]; u += 1)
        {
            const uint32_t position = input->use_positions[u];
            valid = position < input->position_count && (u == input->use_offsets[v] || position >= input->use_positions[u - 1]);
            valid = valid && (tier1_cfg_block_of(input, position) != block || position >= input->definitions[v]);
            valid = valid && (input->use_required == 0 || input->use_required[u] <= 1);
        }
    }
    arena_pop_to(scratch, saved);
    return valid;
}

// Value v's interval that covers `position`, or BUSTER_TIER1_CFG_NONE.
static uint32_t tier1_cfg_interval_at(const Tier1CfgAllocation* result, uint32_t value, uint32_t position)
{
    const uint32_t last = result->value_interval_offsets[value + 1];
    uint32_t low = result->value_interval_offsets[value];
    uint32_t high = last;
    // The first interval that ends after `position`: the only one that may cover it.
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (result->intervals.ends[middle] <= position)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low < last && result->intervals.starts[low] <= position ? low : BUSTER_TIER1_CFG_NONE;
}

// The location of value v at position q: its interval's fragment there. Returns false if no interval of v covers q.
static bool tier1_cfg_location(const Tier1CfgAllocation* result, uint32_t value, uint32_t position, uint32_t* location, uint8_t* kind)
{
    const uint32_t interval = tier1_cfg_interval_at(result, value, position);
    const Tier1Allocation* allocation = &result->allocation;
    bool found = false;
    if (interval != BUSTER_TIER1_CFG_NONE)
    {
        // Fragments tile the interval in order: the first that ends after `position` holds it.
        uint32_t low = allocation->fragment_offsets[interval];
        uint32_t high = allocation->fragment_offsets[interval + 1];
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            if (allocation->ends[middle] <= position)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        found = low < allocation->fragment_offsets[interval + 1] && allocation->starts[low] <= position;
        if (found)
        {
            const uint8_t fragment_kind = allocation->kinds[low];
            *kind = fragment_kind;
            *location = fragment_kind == BUSTER_TIER1_ALLOCATE_REGISTER ? (uint32_t)result->intervals.classes[interval] * 32 + allocation->registers[low] : fragment_kind == BUSTER_TIER1_ALLOCATE_SPILL ? BUSTER_TIER1_CFG_SPILL + allocation->spill_ids[value] : BUSTER_TIER1_CFG_REMATERIALIZE;
        }
    }
    return found;
}

// Where a value must be when control enters its block at `entry`: for an interval that continues from the block's
// layout predecessor, where it was there, since a fragment transition at `entry` runs inside the block; otherwise its
// location at `entry`.
static bool tier1_cfg_entry_location(const Tier1CfgAllocation* result, uint32_t value, uint32_t entry, uint32_t* location, uint8_t* kind)
{
    const uint32_t interval = tier1_cfg_interval_at(result, value, entry);
    const bool continues = interval != BUSTER_TIER1_CFG_NONE && result->intervals.starts[interval] < entry;
    return interval != BUSTER_TIER1_CFG_NONE && tier1_cfg_location(result, value, continues ? entry - 1 : entry, location, kind);
}

// A location's class: a register's, a slot's value's, a temporary slot's; BUSTER_TIER1_CFG_NONE for no location.
static uint32_t tier1_cfg_location_class(const Tier1CfgInput* input, const Tier1CfgAllocation* result, uint32_t location)
{
    const uint32_t spills = result->allocation.spill_count;
    uint32_t class = BUSTER_TIER1_CFG_NONE;
    if (location < BUSTER_TIER1_CFG_SPILL)
    {
        class = location / 32;
    }
    else if (location - BUSTER_TIER1_CFG_SPILL < spills)
    {
        class = input->classes[result->allocation.spill_values[location - BUSTER_TIER1_CFG_SPILL]];
    }
    else if (location - BUSTER_TIER1_CFG_SPILL - spills < BUSTER_TIER1_ALLOCATE_CLASSES)
    {
        class = location - BUSTER_TIER1_CFG_SPILL - spills;
    }
    return class;
}

static bool tier1_cfg_bit(const uint64_t* row, uint32_t bit)
{
    return ((row[bit >> 6] >> (bit & 63)) & 1) != 0;
}

static void tier1_cfg_set(uint64_t* row, uint32_t bit)
{
    row[bit >> 6] |= UINT64_C(1) << (bit & 63);
}

// One edge's moves while they are gathered and resolved. Each class's pending moves are triples (from, to, carried
// value); carried_at gives the value a location holds at the current step of the edge, valid where its stamp is the
// edge's.
typedef struct Tier1CfgEdge Tier1CfgEdge;
struct Tier1CfgEdge
{
    uint32_t* pending[BUSTER_TIER1_ALLOCATE_CLASSES];
    uint32_t pending_count[BUSTER_TIER1_ALLOCATE_CLASSES];
    bool memory_pair[BUSTER_TIER1_ALLOCATE_CLASSES];
    uint32_t* recompute;
    uint32_t recompute_count;
    uint32_t* carried_at;
    uint32_t* carried_stamps;
    uint32_t stamp;
};

// One value of an edge successor's entry state: `value` there, `carried` (the value itself, or a phi's operand) from
// the predecessor's exit. Adds its move, if it needs one, to its class's pending moves or to the recomputations.
// Returns false where either side has no location.
static bool tier1_cfg_edge_value(const Tier1CfgInput* input, const Tier1CfgAllocation* result, Tier1CfgEdge* edge, uint32_t value, uint32_t carried, uint32_t exit, uint32_t entry)
{
    uint32_t from = 0;
    uint32_t to = 0;
    uint8_t from_kind = 0;
    uint8_t to_kind = 0;
    const bool found = tier1_cfg_location(result, carried, exit, &from, &from_kind) && tier1_cfg_entry_location(result, value, entry, &to, &to_kind);
    if (found && to_kind != BUSTER_TIER1_ALLOCATE_REMATERIALIZE && from != to)
    {
        if (from_kind == BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
        {
            uint32_t* move = edge->recompute + (uint64_t)edge->recompute_count * 3;
            move[0] = BUSTER_TIER1_CFG_REMATERIALIZE;
            move[1] = to;
            move[2] = carried;
            edge->recompute_count += 1;
        }
        else
        {
            const uint32_t class = input->classes[value];
            uint32_t* move = edge->pending[class] + (uint64_t)edge->pending_count[class] * 3;
            move[0] = from;
            move[1] = to;
            move[2] = carried;
            edge->pending_count[class] += 1;
            edge->memory_pair[class] = edge->memory_pair[class] || (from >= BUSTER_TIER1_CFG_SPILL && to >= BUSTER_TIER1_CFG_SPILL);
            edge->carried_at[from] = carried;
            edge->carried_stamps[from] = edge->stamp;
        }
    }
    return found;
}

// Stages one edge's moves after `staged_count`: each class's pending moves in tier1_resolve_moves' order, a
// slot-to-slot one through the class's scratch register, then the recomputations. Returns false on a resolution
// failure or a move past `capacity`, which a valid allocation never reaches.
static bool tier1_cfg_edge_stage(const Tier1CfgInput* input, uint32_t spills, Tier1CfgEdge* edge, Arena* scratch, uint32_t* sources, uint32_t* destinations, uint32_t* staged, uint64_t capacity, uint32_t* staged_count, uint8_t* temp_slots)
{
    const uint32_t locations = BUSTER_TIER1_CFG_SPILL + spills + BUSTER_TIER1_ALLOCATE_CLASSES;
    bool valid = true;
    uint32_t count = *staged_count;
    for (uint32_t c = 0; valid && c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
    {
        const uint32_t pending = edge->pending_count[c];
        const uint32_t conduit = c * 32 + input->scratch_registers[c];
        const uint32_t temporary = BUSTER_TIER1_CFG_SPILL + spills + c;
        for (uint32_t m = 0; m < pending; m += 1)
        {
            sources[m] = edge->pending[c][m * 3];
            destinations[m] = edge->pending[c][m * 3 + 1];
        }
        const Tier1ParallelMoves parallel = {
            .sources = sources,
            .destinations = destinations,
            .count = pending,
            .scratch = edge->memory_pair[c] ? temporary : conduit,
        };
        const uint64_t mark = scratch->length;
        Tier1ResolvedMoves resolved = { 0 };
        valid = pending == 0 || tier1_resolve_moves(&parallel, scratch, &resolved) == TIER1_ALLOCATION_OK;
        for (uint32_t m = 0; valid && pending != 0 && m < resolved.count; m += 1)
        {
            const uint32_t from = resolved.sources[m];
            const uint32_t to = resolved.destinations[m];
            valid = from < locations && to < locations && edge->carried_stamps[from] == edge->stamp && (uint64_t)count + 2 <= capacity;
            if (valid)
            {
                const uint32_t carried = edge->carried_at[from];
                const bool memory = from >= BUSTER_TIER1_CFG_SPILL && to >= BUSTER_TIER1_CFG_SPILL;
                uint32_t* move = staged + (uint64_t)count * 3;
                move[0] = from;
                move[1] = memory ? conduit : to;
                move[2] = carried;
                move[3] = conduit;
                move[4] = to;
                move[5] = carried;
                count += memory ? 2 : 1;
                edge->carried_at[to] = carried;
                edge->carried_stamps[to] = edge->stamp;
                *temp_slots |= (uint8_t)((from == temporary || to == temporary) << c);
            }
        }
        arena_pop_to(scratch, mark);
    }
    for (uint32_t m = 0; valid && m < edge->recompute_count; m += 1)
    {
        valid = (uint64_t)count + 1 <= capacity;
        if (valid)
        {
            memcpy(staged + (uint64_t)count * 3, edge->recompute + (uint64_t)m * 3, 3 * sizeof(uint32_t));
            count += 1;
        }
    }
    *staged_count = count;
    return valid;
}

// The intervals being built: counted, or with `fill` filled, by value and position, with the splits among them.
typedef struct Tier1CfgPieces Tier1CfgPieces;
struct Tier1CfgPieces
{
    uint32_t* starts;
    uint32_t* ends;
    uint32_t* values;
    uint32_t* allowed;
    uint8_t* classes;
    uint8_t* remat;
    uint32_t* hints;
    uint32_t* split_positions;
    uint32_t* split_values;
    uint64_t count;
    uint64_t splits;
    bool fill;
};

// A new interval of value v over [start, end), hinted to `hint`'s register; a split where it continues v's range in
// its block.
static void tier1_cfg_piece(Tier1CfgPieces* pieces, const Tier1CfgInput* input, uint32_t v, uint32_t start, uint32_t end, uint32_t hint, bool split)
{
    if (pieces->fill)
    {
        const uint64_t i = pieces->count;
        pieces->starts[i] = start;
        pieces->ends[i] = end;
        pieces->values[i] = v;
        pieces->allowed[i] = input->allowed_registers[v];
        pieces->classes[i] = input->classes[v];
        pieces->remat[i] = input->rematerializable != 0 ? input->rematerializable[v] : 0;
        pieces->hints[i] = hint;
        if (split)
        {
            pieces->split_positions[pieces->splits] = start;
            pieces->split_values[pieces->splits] = v;
        }
    }
    pieces->count += 1;
    pieces->splits += split;
}

static Tier1CfgError tier1_cfg_allocate_impl(const Tier1CfgInput* input, Arena* output, Arena* scratch, bool vector, Tier1CfgAllocation* result)
{
    Tier1CfgError error = TIER1_CFG_INVALID;
    const uint64_t output_saved = output != 0 ? output->length : 0;
    const uint64_t scratch_saved = scratch != 0 ? scratch->length : 0;
    Tier1CfgAllocation candidate = { 0 };
    const bool arguments = input != 0 && output != 0 && scratch != 0 && output != scratch && result != 0;
    if (arguments && tier1_cfg_input_valid(input, scratch))
    {
        error = TIER1_CFG_CAPACITY;
        const uint32_t blocks = input->block_count;
        const uint32_t values = input->value_count;
        // Each value's definition block, and whether it is used outside it (dense ids, ascending value order).
        uint32_t* home = arena_push_array(scratch, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t));
        uint32_t* dense = arena_push_array(scratch, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t));
        uint32_t* dense_values = arena_push_array(scratch, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t));
        bool ready = home != 0 && dense != 0 && dense_values != 0;
        uint32_t dense_count = 0;
        for (uint32_t v = 0; ready && v < values; v += 1)
        {
            home[v] = tier1_cfg_block_of(input, input->definitions[v]);
            dense[v] = BUSTER_TIER1_CFG_NONE;
        }
        for (uint32_t o = 0; ready && o < input->phi_operand_offsets[input->phi_count]; o += 1)
        {
            dense[input->phi_operands[o]] = 0;
        }
        for (uint32_t v = 0; ready && v < values; v += 1)
        {
            for (uint32_t u = input->use_offsets[v]; dense[v] == BUSTER_TIER1_CFG_NONE && u < input->use_offsets[v + 1]; u += 1)
            {
                dense[v] = tier1_cfg_block_of(input, input->use_positions[u]) != home[v] ? 0 : BUSTER_TIER1_CFG_NONE;
            }
        }
        for (uint32_t v = 0; ready && v < values; v += 1)
        {
            if (dense[v] != BUSTER_TIER1_CFG_NONE)
            {
                dense[v] = dense_count;
                dense_values[dense_count] = v;
                dense_count += 1;
            }
        }
        // Each phi's hint: its operand from the predecessor nearest before it in layout, which the allocator has most
        // likely placed last, or else from its first predecessor.
        uint32_t* phi_hints = ready ? arena_push_array(scratch, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        ready = ready && phi_hints != 0;
        if (ready)
        {
            memset(phi_hints, 0xff, ((uint64_t)values + 1) * sizeof(uint32_t));
        }
        for (uint32_t b = 0; ready && b < blocks; b += 1)
        {
            uint32_t nearest = 0;
            for (uint32_t j = 0; j < input->predecessor_offsets[b + 1] - input->predecessor_offsets[b]; j += 1)
            {
                const uint32_t p = input->predecessors[input->predecessor_offsets[b] + j];
                const uint32_t best = input->predecessors[input->predecessor_offsets[b] + nearest];
                nearest = p < b && (best >= b || p > best) ? j : nearest;
            }
            for (uint32_t k = input->phi_offsets[b]; k < input->phi_offsets[b + 1]; k += 1)
            {
                phi_hints[input->phi_values[k]] = input->phi_operands[input->phi_operand_offsets[k] + nearest];
            }
        }
        Dataflow liveness = { 0 };
        ready = ready && dataflow_create(scratch, blocks, dense_count, input->edge_count, &liveness);
        const uint64_t words = liveness.row_words;
        const uint64_t matrix_words = (uint64_t)blocks * words;
        // P[b], the phi operands b passes on its edges, kept apart to add to OUT after the solve.
        uint64_t* passed = ready && words != 0 ? arena_push(scratch, matrix_words * sizeof(uint64_t), BUSTER_DATAFLOW_VECTOR_BYTES) : 0;
        ready = ready && (words == 0 || passed != 0);
        if (ready)
        {
            memcpy(liveness.successor_offsets, input->successor_offsets, ((uint64_t)blocks + 1) * sizeof(uint32_t));
            if (input->edge_count != 0)
            {
                memcpy(liveness.successors, input->successors, (uint64_t)input->edge_count * sizeof(uint32_t));
            }
            if (words != 0)
            {
                memset(passed, 0, matrix_words * sizeof(uint64_t));
            }
            for (uint32_t i = 0; i < dense_count; i += 1)
            {
                const uint32_t v = dense_values[i];
                tier1_cfg_set(liveness.def + (uint64_t)home[v] * words, i);
                for (uint32_t u = input->use_offsets[v]; u < input->use_offsets[v + 1]; u += 1)
                {
                    const uint32_t block = tier1_cfg_block_of(input, input->use_positions[u]);
                    if (block != home[v])
                    {
                        tier1_cfg_set(liveness.use + (uint64_t)block * words, i);
                    }
                }
            }
            for (uint32_t b = 0; b < blocks; b += 1)
            {
                for (uint32_t k = input->phi_offsets[b]; k < input->phi_offsets[b + 1]; k += 1)
                {
                    for (uint32_t j = 0; j < input->phi_operand_offsets[k + 1] - input->phi_operand_offsets[k]; j += 1)
                    {
                        const uint32_t predecessor = input->predecessors[input->predecessor_offsets[b] + j];
                        tier1_cfg_set(passed + (uint64_t)predecessor * words, dense[input->phi_operands[input->phi_operand_offsets[k] + j]]);
                    }
                }
            }
            for (uint64_t w = 0; w < matrix_words; w += 1)
            {
                liveness.use[w] |= passed[w] & ~liveness.def[w];
            }
            ready = dataflow_postorder(&liveness, scratch);
        }
        if (ready)
        {
            candidate.liveness_iterations = BUSTER_VECTOR_PATH(vector) ? dataflow_liveness_vector(&liveness) : dataflow_liveness_scalar(&liveness);
            for (uint64_t w = 0; w < matrix_words; w += 1)
            {
                liveness.out[w] |= passed[w];
            }
            candidate.cross_block_values = dense_count;
        }
        // Only a value's own definition can define it: one live into the entry, or into its own block, isn't SSA.
        for (uint32_t i = 0; ready && i < dense_count; i += 1)
        {
            if (tier1_cfg_bit(liveness.in, i) || tier1_cfg_bit(liveness.in + (uint64_t)home[dense_values[i]] * words, i))
            {
                ready = false;
                error = TIER1_CFG_INVALID;
            }
        }
        // Blocks whose entry state continues from their layout predecessor (see 2. above), and each block's entry
        // state's size, for the edges' moves.
        uint8_t* merges = ready ? arena_push(scratch, blocks, 1) : 0;
        uint32_t* entering = ready ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        ready = ready && merges != 0 && entering != 0;
        for (uint32_t b = 0; ready && b < blocks; b += 1)
        {
            const uint64_t* in = liveness.in + (uint64_t)b * words;
            bool merge = b != 0 && input->phi_offsets[b] == input->phi_offsets[b + 1];
            uint32_t count = input->phi_offsets[b + 1] - input->phi_offsets[b];
            for (uint64_t w = 0; w < words; w += 1)
            {
                merge = merge && (in[w] & ~liveness.out[((uint64_t)b - 1) * words + w]) == 0;
                count += (uint32_t)__builtin_popcountll(in[w]);
            }
            merges[b] = merge;
            entering[b] = count;
        }
        // Each value's blocks, ascending: its definition's and those it is live into. Counted per value, with the values
        // each block defines, then filled block by block.
        uint32_t* list_offsets = ready ? arena_push_array(scratch, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        uint32_t* defined_offsets = ready ? arena_push_array(scratch, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        uint32_t* defined = ready ? arena_push_array(scratch, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        uint32_t* cursors = ready ? arena_push_array(scratch, (uint64_t)(values > blocks ? values : blocks) + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        ready = ready && list_offsets != 0 && defined_offsets != 0 && defined != 0 && cursors != 0;
        if (ready)
        {
            memset(list_offsets, 0, ((uint64_t)values + 1) * sizeof(uint32_t));
            memset(defined_offsets, 0, ((uint64_t)blocks + 1) * sizeof(uint32_t));
            for (uint32_t v = 0; v < values; v += 1)
            {
                list_offsets[v + 1] += 1;
                defined_offsets[home[v] + 1] += 1;
            }
            for (uint64_t w = 0; w < matrix_words; w += 1)
            {
                uint64_t bits = liveness.in[w];
                while (bits != 0)
                {
                    const uint32_t i = (uint32_t)((w % words) * 64 + (uint64_t)__builtin_ctzll(bits));
                    bits &= bits - 1;
                    list_offsets[dense_values[i] + 1] += 1;
                }
            }
            for (uint32_t v = 0; v < values; v += 1)
            {
                list_offsets[v + 1] += list_offsets[v];
            }
            for (uint32_t b = 0; b < blocks; b += 1)
            {
                defined_offsets[b + 1] += defined_offsets[b];
                cursors[b] = defined_offsets[b];
            }
            for (uint32_t v = 0; v < values; v += 1)
            {
                defined[cursors[home[v]]] = v;
                cursors[home[v]] += 1;
            }
        }
        uint32_t* list = ready ? arena_push_array(scratch, (uint64_t)list_offsets[values] + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        ready = ready && list != 0;
        if (ready)
        {
            for (uint32_t v = 0; v < values; v += 1)
            {
                cursors[v] = list_offsets[v];
            }
            for (uint32_t b = 0; b < blocks; b += 1)
            {
                for (uint32_t x = defined_offsets[b]; x < defined_offsets[b + 1]; x += 1)
                {
                    list[cursors[defined[x]]] = b;
                    cursors[defined[x]] += 1;
                }
                for (uint64_t w = 0; w < words; w += 1)
                {
                    uint64_t bits = liveness.in[(uint64_t)b * words + w];
                    while (bits != 0)
                    {
                        const uint32_t v = dense_values[w * 64 + (uint64_t)__builtin_ctzll(bits)];
                        bits &= bits - 1;
                        list[cursors[v]] = b;
                        cursors[v] += 1;
                    }
                }
            }
        }
        // Fixed ranges by start, counted per position then filled: a value's range is cut around each one of its
        // class that blocks every register the value may take (2. above), and a block whose start one covers doesn't
        // continue its predecessor's ranges.
        const uint32_t fixed_count = input->fixed.count;
        uint32_t* fixed_offsets = ready ? arena_push_array(scratch, (uint64_t)input->position_count + 2, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        uint32_t* fixed_sorted = ready ? arena_push_array(scratch, (uint64_t)fixed_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        ready = ready && fixed_offsets != 0 && fixed_sorted != 0;
        uint32_t longest = 1;
        if (ready)
        {
            memset(fixed_offsets, 0, ((uint64_t)input->position_count + 2) * sizeof(uint32_t));
            for (uint32_t r = 0; r < fixed_count; r += 1)
            {
                const uint32_t length = input->fixed.ends[r] - input->fixed.starts[r];
                fixed_offsets[input->fixed.starts[r] + 1] += 1;
                longest = length > longest ? length : longest;
            }
            for (uint32_t q = 0; q <= input->position_count; q += 1)
            {
                fixed_offsets[q + 1] += fixed_offsets[q];
            }
            for (uint32_t r = 0; r < fixed_count; r += 1)
            {
                fixed_sorted[fixed_offsets[input->fixed.starts[r]]] = r;
                fixed_offsets[input->fixed.starts[r]] += 1;
            }
            // The fill advanced each start to the next's: shift them back.
            for (uint32_t q = input->position_count + 1; q > 0; q -= 1)
            {
                fixed_offsets[q] = fixed_offsets[q - 1];
            }
            fixed_offsets[0] = 0;
            for (uint32_t b = 1; b < blocks; b += 1)
            {
                const uint32_t entry = input->block_starts[b];
                const uint32_t low = entry >= longest ? entry - longest + 1 : 0;
                for (uint32_t k = fixed_offsets[low]; merges[b] && k < fixed_offsets[entry + 1]; k += 1)
                {
                    merges[b] = input->fixed.ends[fixed_sorted[k]] <= entry;
                }
            }
        }
        // Intervals, counted, then filled, by value and position; and the splits, each piece of a range after the
        // first in its block, by the position it starts at and its value.
        candidate.value_interval_offsets = ready ? arena_push_array(output, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        ready = ready && candidate.value_interval_offsets != 0;
        Tier1CfgPieces pieces = { 0 };
        uint64_t interval_count = 0;
        uint64_t split_count = 0;
        for (uint32_t pass = 0; ready && pass < 2; pass += 1)
        {
            uint32_t* use_offsets = 0;
            pieces = (Tier1CfgPieces){
                .fill = pass == 1,
            };
            if (pass == 1)
            {
                ready = interval_count <= BUSTER_TIER1_ALLOCATE_MAX_INTERVALS && split_count <= BUSTER_TIER1_ALLOCATE_MAX_INTERVALS;
                pieces.starts = ready ? arena_push_array(output, interval_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
                pieces.ends = ready ? arena_push_array(output, interval_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
                pieces.values = ready ? arena_push_array(output, interval_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
                pieces.allowed = ready ? arena_push_array(output, interval_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
                pieces.classes = ready ? arena_push(output, interval_count + 1, 1) : 0;
                pieces.remat = ready ? arena_push(output, interval_count + 1, 1) : 0;
                pieces.hints = ready ? arena_push_array(output, interval_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
                use_offsets = ready ? arena_push_array(output, interval_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
                pieces.split_positions = ready ? arena_push_array(scratch, split_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
                pieces.split_values = ready ? arena_push_array(scratch, split_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
                ready = ready && pieces.starts != 0 && pieces.ends != 0 && pieces.values != 0 && pieces.allowed != 0 && pieces.classes != 0 && pieces.remat != 0 && pieces.hints != 0 && use_offsets != 0 && pieces.split_positions != 0 && pieces.split_values != 0;
                pieces.fill = ready;
            }
            for (uint32_t v = 0; ready && v < values; v += 1)
            {
                if (pass == 1)
                {
                    candidate.value_interval_offsets[v] = (uint32_t)pieces.count;
                }
                const uint64_t first = pieces.count;
                uint32_t use = input->use_offsets[v];
                const uint32_t use_end = input->use_offsets[v + 1];
                bool stray = false;
                for (uint32_t x = list_offsets[v]; x < list_offsets[v + 1]; x += 1)
                {
                    const uint32_t b = list[x];
                    const bool defined_here = b == home[v];
                    const bool live_out = dense[v] != BUSTER_TIER1_CFG_NONE && tier1_cfg_bit(liveness.out + (uint64_t)b * words, dense[v]);
                    // A use before this block lies in no block the value is live in: not SSA.
                    while (use < use_end && input->use_positions[use] < input->block_starts[b])
                    {
                        stray = true;
                        use += 1;
                    }
                    uint32_t last_use = BUSTER_TIER1_CFG_NONE;
                    while (use < use_end && input->use_positions[use] < input->block_starts[b + 1])
                    {
                        last_use = input->use_positions[use];
                        use += 1;
                    }
                    const uint32_t start = defined_here ? input->definitions[v] : input->block_starts[b];
                    const uint32_t end = live_out ? input->block_starts[b + 1] : last_use != BUSTER_TIER1_CFG_NONE ? last_use + 1 : start + 1;
                    // The range's pieces: before each fixed range that blocks the value, and over it.
                    uint32_t cursor = start;
                    uint32_t piece = 0;
                    uint32_t k = fixed_offsets[start >= longest ? start - longest + 1 : 0];
                    while (cursor < end)
                    {
                        uint32_t blocked_start = end;
                        uint32_t blocked_end = end;
                        for (; blocked_start == end && k < fixed_offsets[end]; k += 1)
                        {
                            const uint32_t r = fixed_sorted[k];
                            if (input->fixed.classes[r] == input->classes[v] && (input->allowed_registers[v] & ~input->fixed.registers[r]) == 0 && input->fixed.ends[r] > cursor)
                            {
                                blocked_start = input->fixed.starts[r] > cursor ? input->fixed.starts[r] : cursor;
                                blocked_end = input->fixed.ends[r] < end ? input->fixed.ends[r] : end;
                            }
                        }
                        for (uint32_t part = 0; part < 2; part += 1)
                        {
                            const uint32_t from = part == 0 ? cursor : blocked_start;
                            const uint32_t to = part == 0 ? blocked_start : blocked_end;
                            if (from < to)
                            {
                                // A merged block continues the value's range from its layout predecessor, its previous
                                // block. A range the value enters a block with keeps, where it can, the register it had
                                // before; a phi takes its operand's; a piece after a split, the value's last.
                                if (piece == 0 && !defined_here && merges[b] && pieces.count > first)
                                {
                                    if (pieces.fill)
                                    {
                                        pieces.ends[pieces.count - 1] = to;
                                    }
                                }
                                else
                                {
                                    tier1_cfg_piece(&pieces, input, v, from, to, piece == 0 && defined_here ? phi_hints[v] : v, piece != 0);
                                }
                                piece += 1;
                            }
                        }
                        cursor = blocked_start < end ? blocked_end : end;
                    }
                }
                ready = !stray && use == use_end;
                error = ready ? error : TIER1_CFG_INVALID;
            }
            if (ready && pass == 0)
            {
                interval_count = pieces.count;
                split_count = pieces.splits;
            }
            if (ready && pass == 1)
            {
                const uint32_t count = (uint32_t)pieces.count;
                candidate.value_interval_offsets[values] = count;
                // Uses are sorted per value and intervals are by value and position: each slice is contiguous.
                uint32_t use = 0;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    const uint32_t v = pieces.values[i];
                    use = use < input->use_offsets[v] ? input->use_offsets[v] : use;
                    use_offsets[i] = use;
                    while (use < input->use_offsets[v + 1] && input->use_positions[use] < pieces.ends[i])
                    {
                        use += 1;
                    }
                }
                use_offsets[count] = input->use_count;
                candidate.intervals = (Tier1AllocationInput){
                    .starts = pieces.starts,
                    .ends = pieces.ends,
                    .values = pieces.values,
                    .allowed_registers = pieces.allowed,
                    .classes = pieces.classes,
                    .hint_values = pieces.hints,
                    .rematerializable = input->rematerializable != 0 ? pieces.remat : 0,
                    .use_offsets = use_offsets,
                    .use_positions = input->use_positions,
                    .use_required = input->use_required,
                    .use_count = input->use_count,
                    .count = count,
                    .value_count = values,
                    .position_count = input->position_count,
                    .registers = {
                        input->registers[0],
                        input->registers[1],
                    },
                    .fixed = input->fixed,
                };
            }
        }
        if (ready)
        {
            const Tier1AllocationError allocated = BUSTER_VECTOR_PATH(vector) ? tier1_allocate(&candidate.intervals, output, &candidate.allocation) : tier1_allocate_scalar(&candidate.intervals, output, &candidate.allocation);
            ready = allocated == TIER1_ALLOCATION_OK;
            error = allocated == TIER1_ALLOCATION_OK ? error : allocated == TIER1_ALLOCATION_CONSTRAINT ? TIER1_CFG_CONSTRAINT : allocated == TIER1_ALLOCATION_INVALID ? TIER1_CFG_INVALID : TIER1_CFG_CAPACITY;
        }
        // Splits by position, counted then filled: the values whose ranges are cut there, and the distinct positions.
        const uint32_t splits = ready ? (uint32_t)pieces.splits : 0;
        uint32_t* split_offsets = ready ? arena_push_array(scratch, (uint64_t)input->position_count + 2, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        uint32_t* split_values = ready ? arena_push_array(scratch, (uint64_t)splits + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        ready = ready && split_offsets != 0 && split_values != 0;
        uint32_t split_positions = 0;
        uint32_t split_widest = 0;
        if (ready)
        {
            memset(split_offsets, 0, ((uint64_t)input->position_count + 2) * sizeof(uint32_t));
            for (uint32_t k = 0; k < splits; k += 1)
            {
                split_offsets[pieces.split_positions[k] + 1] += 1;
            }
            for (uint32_t q = 0; q <= input->position_count; q += 1)
            {
                const uint32_t here = split_offsets[q + 1];
                split_positions += here != 0;
                split_widest = here > split_widest ? here : split_widest;
                split_offsets[q + 1] += split_offsets[q];
            }
            for (uint32_t k = 0; k < splits; k += 1)
            {
                split_values[split_offsets[pieces.split_positions[k]]] = pieces.split_values[k];
                split_offsets[pieces.split_positions[k]] += 1;
            }
            // The fill advanced each start to the next's: shift them back.
            for (uint32_t q = input->position_count + 1; q > 0; q -= 1)
            {
                split_offsets[q] = split_offsets[q - 1];
            }
            split_offsets[0] = 0;
        }
        // Edge and split moves: each edge's, then each split's, resolved into a staging area, then all published at
        // their exact extent. An edge has at most four moves per value of its successor's entry state
        // (tier1_resolve_moves' bound, two per move, and a slot-to-slot move takes two), and a split as many per value
        // it cuts.
        const uint32_t spills = ready ? candidate.allocation.spill_count : 0;
        const uint32_t locations = BUSTER_TIER1_CFG_SPILL + spills + BUSTER_TIER1_ALLOCATE_CLASSES;
        uint64_t bound = 4 * (uint64_t)splits;
        uint32_t widest = split_widest;
        for (uint32_t e = 0; ready && e < input->edge_count; e += 1)
        {
            const uint32_t target = input->successors[e];
            bound += 4 * (uint64_t)entering[target];
            widest = entering[target] > widest ? entering[target] : widest;
        }
        ready = ready && bound <= UINT32_MAX;
        Tier1CfgEdge edge = { 0 };
        uint32_t* staged = ready ? arena_push_array(scratch, (bound + 2) * 3, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        uint32_t* sources = ready ? arena_push_array(scratch, (uint64_t)widest + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        uint32_t* destinations = ready ? arena_push_array(scratch, (uint64_t)widest + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        edge.recompute = ready ? arena_push_array(scratch, ((uint64_t)widest + 1) * 3, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        edge.carried_at = ready ? arena_push_array(scratch, locations, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        edge.carried_stamps = ready ? arena_push_array(scratch, locations, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        for (uint32_t c = 0; c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
        {
            edge.pending[c] = ready ? arena_push_array(scratch, ((uint64_t)widest + 1) * 3, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
            ready = ready && edge.pending[c] != 0;
        }
        candidate.edge_placements = ready ? arena_push(output, (uint64_t)input->edge_count + 1, 1) : 0;
        candidate.edge_move_offsets = ready ? arena_push_array(output, (uint64_t)input->edge_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        candidate.split_positions = ready ? arena_push_array(output, (uint64_t)split_positions + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        candidate.split_move_offsets = ready ? arena_push_array(output, (uint64_t)split_positions + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        ready = ready && staged != 0 && sources != 0 && destinations != 0 && edge.recompute != 0 && edge.carried_at != 0 && edge.carried_stamps != 0 && candidate.edge_placements != 0 && candidate.edge_move_offsets != 0 && candidate.split_positions != 0 && candidate.split_move_offsets != 0;
        if (ready)
        {
            memset(edge.carried_stamps, 0, (uint64_t)locations * sizeof(uint32_t));
        }
        uint32_t staged_count = 0;
        for (uint32_t b = 0; ready && b < blocks; b += 1)
        {
            const uint32_t exit = input->block_starts[b + 1] - 1;
            for (uint32_t e = input->successor_offsets[b]; ready && e < input->successor_offsets[b + 1]; e += 1)
            {
                const uint32_t target = input->successors[e];
                const uint32_t entry = input->block_starts[target];
                candidate.edge_move_offsets[e] = staged_count;
                edge.stamp += 1;
                edge.recompute_count = 0;
                for (uint32_t c = 0; c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
                {
                    edge.pending_count[c] = 0;
                    edge.memory_pair[c] = false;
                }
                for (uint64_t w = 0; ready && w < words; w += 1)
                {
                    uint64_t bits = liveness.in[(uint64_t)target * words + w];
                    while (ready && bits != 0)
                    {
                        const uint32_t v = dense_values[w * 64 + (uint64_t)__builtin_ctzll(bits)];
                        bits &= bits - 1;
                        ready = tier1_cfg_edge_value(input, &candidate, &edge, v, v, exit, entry);
                    }
                }
                uint32_t j = 0;
                while (input->predecessors[input->predecessor_offsets[target] + j] != b)
                {
                    j += 1;
                }
                for (uint32_t k = input->phi_offsets[target]; ready && k < input->phi_offsets[target + 1]; k += 1)
                {
                    ready = tier1_cfg_edge_value(input, &candidate, &edge, input->phi_values[k], input->phi_operands[input->phi_operand_offsets[k] + j], exit, entry);
                }
                ready = ready && tier1_cfg_edge_stage(input, spills, &edge, scratch, sources, destinations, staged, bound + 2, &staged_count, &candidate.temp_slots);
                error = ready ? error : TIER1_CFG_INVALID;
                if (ready)
                {
                    const bool moves = staged_count != candidate.edge_move_offsets[e];
                    const bool one_successor = input->successor_offsets[b + 1] - input->successor_offsets[b] == 1;
                    const bool one_predecessor = input->predecessor_offsets[target + 1] - input->predecessor_offsets[target] == 1;
                    candidate.edge_placements[e] = !moves ? TIER1_CFG_EDGE_EMPTY : one_successor ? TIER1_CFG_EDGE_PREDECESSOR : one_predecessor ? TIER1_CFG_EDGE_SUCCESSOR : TIER1_CFG_EDGE_SPLIT;
                    candidate.split_edges += candidate.edge_placements[e] == TIER1_CFG_EDGE_SPLIT;
                }
            }
        }
        if (ready)
        {
            candidate.edge_move_offsets[input->edge_count] = staged_count;
        }
        // Each split's moves: from the value's location just before the position to its new piece's.
        for (uint32_t q = 0; ready && q < input->position_count; q += 1)
        {
            if (split_offsets[q] != split_offsets[q + 1])
            {
                candidate.split_positions[candidate.split_count] = q;
                candidate.split_move_offsets[candidate.split_count] = staged_count;
                candidate.split_count += 1;
                edge.stamp += 1;
                edge.recompute_count = 0;
                for (uint32_t c = 0; c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
                {
                    edge.pending_count[c] = 0;
                    edge.memory_pair[c] = false;
                }
                for (uint32_t k = split_offsets[q]; ready && k < split_offsets[q + 1]; k += 1)
                {
                    ready = q != 0 && tier1_cfg_edge_value(input, &candidate, &edge, split_values[k], split_values[k], q - 1, q);
                }
                ready = ready && tier1_cfg_edge_stage(input, spills, &edge, scratch, sources, destinations, staged, bound + 2, &staged_count, &candidate.temp_slots);
                error = ready ? error : TIER1_CFG_INVALID;
            }
        }
        if (ready)
        {
            candidate.split_move_offsets[candidate.split_count] = staged_count;
            candidate.move_count = staged_count;
            candidate.move_sources = arena_push_array(output, (uint64_t)staged_count + 1, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.move_destinations = arena_push_array(output, (uint64_t)staged_count + 1, sizeof(uint32_t), _Alignof(uint32_t));
            candidate.move_values = arena_push_array(output, (uint64_t)staged_count + 1, sizeof(uint32_t), _Alignof(uint32_t));
            ready = candidate.move_sources != 0 && candidate.move_destinations != 0 && candidate.move_values != 0;
            error = ready ? TIER1_CFG_OK : TIER1_CFG_CAPACITY;
        }
        for (uint32_t m = 0; ready && m < staged_count; m += 1)
        {
            candidate.move_sources[m] = staged[(uint64_t)m * 3];
            candidate.move_destinations[m] = staged[(uint64_t)m * 3 + 1];
            candidate.move_values[m] = staged[(uint64_t)m * 3 + 2];
        }
    }
    if (error == TIER1_CFG_OK)
    {
        candidate.retained_bytes = output->length - output_saved;
        candidate.scratch_bytes = scratch->length - scratch_saved;
        arena_pop_to(scratch, scratch_saved);
        *result = candidate;
    }
    else
    {
        if (output != 0)
        {
            arena_pop_to(output, output_saved);
        }
        if (scratch != 0)
        {
            arena_pop_to(scratch, scratch_saved);
        }
    }
    return error;
}

static __attribute__((unused)) Tier1CfgError tier1_cfg_allocate(const Tier1CfgInput* input, Arena* output, Arena* scratch, Tier1CfgAllocation* result)
{
    return tier1_cfg_allocate_impl(input, output, scratch, BUSTER_HOST_VECTOR, result);
}

static __attribute__((unused)) Tier1CfgError tier1_cfg_allocate_scalar(const Tier1CfgInput* input, Arena* output, Arena* scratch, Tier1CfgAllocation* result)
{
    return tier1_cfg_allocate_impl(input, output, scratch, false, result);
}

typedef struct Tier1CfgCheck Tier1CfgCheck;
struct Tier1CfgCheck
{
    // "ok", or the first failure, with its block, value and position where they apply.
    const char* reason;
    uint32_t block;
    uint32_t value;
    uint32_t position;
    uint64_t checks;
};

// The checker's locations: registers, slots, then the temporary slots. A stamp other than the current one means
// "unknown".
typedef struct Tier1CfgState Tier1CfgState;
struct Tier1CfgState
{
    uint32_t* values;
    uint32_t* stamps;
    uint32_t stamp;
    uint32_t count;
};

static uint32_t tier1_cfg_state_get(const Tier1CfgState* state, uint32_t location)
{
    return location < state->count && state->stamps[location] == state->stamp ? state->values[location] : BUSTER_TIER1_CFG_NONE;
}

static void tier1_cfg_state_set(Tier1CfgState* state, uint32_t location, uint32_t value)
{
    state->values[location] = value;
    state->stamps[location] = state->stamp;
}

static bool tier1_cfg_fail(Tier1CfgCheck* check, const char* reason, uint32_t block, uint32_t value, uint32_t position)
{
    if (check->reason == 0)
    {
        check->reason = reason;
        check->block = block;
        check->value = value;
        check->position = position;
    }
    return false;
}

// Whether a write of `location` at `position` would overwrite a value other than `value` that the allocation still
// keeps there: two live values in one register.
static bool tier1_cfg_clobbers(const Tier1CfgAllocation* result, const Tier1CfgState* state, uint32_t location, uint32_t value, uint32_t position)
{
    const uint32_t held = tier1_cfg_state_get(state, location);
    uint32_t where = 0;
    uint8_t kind = 0;
    return held != BUSTER_TIER1_CFG_NONE && held != value && held < result->intervals.value_count && tier1_cfg_location(result, held, position, &where, &kind) && where == location;
}

// One move of an edge or a split, applied to `state`: it must write a location of its value's class, a register only
// if the class may hold it there or it is the class's scratch, and read the value it says it carries, or recompute a
// value with a recipe. At a split (`position` inside a block) it mustn't land on a value the allocation still keeps
// in that location.
static bool tier1_cfg_apply(const Tier1CfgInput* input, const Tier1CfgAllocation* result, Tier1CfgState* state, uint32_t m, uint32_t block, uint32_t position, bool split, Tier1CfgCheck* check)
{
    const uint32_t from = result->move_sources[m];
    const uint32_t to = result->move_destinations[m];
    const uint32_t carried = result->move_values[m];
    const uint32_t class = carried < input->value_count ? input->classes[carried] : BUSTER_TIER1_CFG_NONE;
    const bool reg = to < BUSTER_TIER1_CFG_SPILL;
    const bool writable = !reg || (class < BUSTER_TIER1_ALLOCATE_CLASSES && (((input->registers[class] | UINT32_C(1) << input->scratch_registers[class]) >> (to % 32)) & 1) != 0);
    bool valid = (to < state->count && class != BUSTER_TIER1_CFG_NONE && tier1_cfg_location_class(input, result, to) == class && writable) || tier1_cfg_fail(check, "a move to no location of its value's class", block, carried, position);
    valid = valid && (!split || !tier1_cfg_clobbers(result, state, to, carried, position) || tier1_cfg_fail(check, "a split's move over another live value", block, carried, position));
    if (valid && from == BUSTER_TIER1_CFG_REMATERIALIZE)
    {
        valid = (input->rematerializable != 0 && input->rematerializable[carried] != 0) || tier1_cfg_fail(check, "a rematerialization without a recipe", block, carried, position);
        tier1_cfg_state_set(state, to, carried);
    }
    else if (valid)
    {
        const uint32_t moved = tier1_cfg_state_get(state, from);
        valid = (tier1_cfg_location_class(input, result, from) == class) || tier1_cfg_fail(check, "a move from no location of its value's class", block, carried, position);
        valid = valid && ((moved != BUSTER_TIER1_CFG_NONE && moved == carried) || tier1_cfg_fail(check, "a move that reads another value than it says it carries", block, carried, position));
        tier1_cfg_state_set(state, to, moved);
    }
    check->checks += 1;
    return valid;
}

static __attribute__((unused)) bool tier1_cfg_verify(const Tier1CfgInput* input, const Tier1CfgAllocation* result, Arena* scratch, Tier1CfgCheck* check)
{
    const uint64_t saved = scratch->length;
    *check = (Tier1CfgCheck){ 0 };
    bool valid = tier1_cfg_input_valid(input, scratch) || tier1_cfg_fail(check, "invalid input", 0, 0, 0);
    const uint32_t blocks = input->block_count;
    const uint32_t values = input->value_count;
    const Tier1Allocation* allocation = &result->allocation;
    const Tier1AllocationInput* intervals = &result->intervals;
    // Every value's intervals: its own, in position order, without overlap, each with fragments from start to end.
    valid = valid && ((result->value_interval_offsets[0] == 0 && result->value_interval_offsets[values] == intervals->count && allocation->interval_count == intervals->count) || tier1_cfg_fail(check, "interval map", 0, 0, 0));
    for (uint32_t v = 0; valid && v < values; v += 1)
    {
        for (uint32_t i = result->value_interval_offsets[v]; valid && i < result->value_interval_offsets[v + 1]; i += 1)
        {
            valid = (intervals->values[i] == v && intervals->classes[i] == input->classes[v] && intervals->starts[i] < intervals->ends[i] && intervals->ends[i] <= input->position_count && (i == result->value_interval_offsets[v] || intervals->ends[i - 1] <= intervals->starts[i])) || tier1_cfg_fail(check, "an interval out of its value's order", 0, v, intervals->starts[i]);
            uint32_t position = intervals->starts[i];
            for (uint32_t f = allocation->fragment_offsets[i]; valid && f < allocation->fragment_offsets[i + 1]; f += 1)
            {
                const bool reg = allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER;
                valid = (allocation->starts[f] == position && allocation->ends[f] > position && allocation->kinds[f] <= BUSTER_TIER1_ALLOCATE_REMATERIALIZE) || tier1_cfg_fail(check, "fragments don't tile their interval", 0, v, position);
                valid = valid && ((!reg || (allocation->registers[f] < 32 && ((input->allowed_registers[v] >> allocation->registers[f]) & 1) != 0)) || tier1_cfg_fail(check, "a register outside the value's mask", 0, v, position));
                valid = valid && ((allocation->kinds[f] != BUSTER_TIER1_ALLOCATE_SPILL || (allocation->spill_ids[v] < allocation->spill_count && allocation->spill_values[allocation->spill_ids[v]] == v)) || tier1_cfg_fail(check, "a spill without a slot", 0, v, position));
                valid = valid && ((allocation->kinds[f] != BUSTER_TIER1_ALLOCATE_REMATERIALIZE || (input->rematerializable != 0 && input->rematerializable[v] != 0)) || tier1_cfg_fail(check, "a rematerialization of a value without a recipe", 0, v, position));
                position = allocation->ends[f];
            }
            valid = valid && ((position == intervals->ends[i]) || tier1_cfg_fail(check, "fragments don't reach their interval's end", 0, v, position));
        }
    }
    // The moves: every edge's, then every split's, each split at a position strictly inside a block, in order.
    valid = valid && ((result->edge_move_offsets[0] == 0 && result->edge_move_offsets[input->edge_count] == result->split_move_offsets[0] && result->split_move_offsets[result->split_count] == result->move_count) || tier1_cfg_fail(check, "the moves' extents", 0, 0, 0));
    for (uint32_t e = 0; valid && e < input->edge_count; e += 1)
    {
        valid = result->edge_move_offsets[e] <= result->edge_move_offsets[e + 1] || tier1_cfg_fail(check, "an edge's moves out of order", 0, 0, 0);
    }
    for (uint32_t k = 0; valid && k < result->split_count; k += 1)
    {
        const uint32_t q = result->split_positions[k];
        valid = (q < input->position_count && (k == 0 || q > result->split_positions[k - 1]) && input->block_starts[tier1_cfg_block_of(input, q)] != q && result->split_move_offsets[k] <= result->split_move_offsets[k + 1]) || tier1_cfg_fail(check, "a split out of order or at a block's start", 0, 0, q);
    }
    // Every phi operand's predecessor, by value (counted, then filled), and which values are phis.
    uint32_t* pass_offsets = valid ? arena_push_array(scratch, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* pass_blocks = valid ? arena_push_array(scratch, (uint64_t)input->phi_operand_offsets[input->phi_count] + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint8_t* is_phi = valid ? arena_push(scratch, (uint64_t)values + 1, 1) : 0;
    valid = valid && ((pass_offsets != 0 && pass_blocks != 0 && is_phi != 0) || tier1_cfg_fail(check, "checker memory", 0, 0, 0));
    if (valid)
    {
        memset(pass_offsets, 0, ((uint64_t)values + 1) * sizeof(uint32_t));
        memset(is_phi, 0, (uint64_t)values + 1);
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            for (uint32_t b = 0; b < blocks; b += 1)
            {
                for (uint32_t k = input->phi_offsets[b]; k < input->phi_offsets[b + 1]; k += 1)
                {
                    is_phi[input->phi_values[k]] = 1;
                    for (uint32_t j = 0; j < input->phi_operand_offsets[k + 1] - input->phi_operand_offsets[k]; j += 1)
                    {
                        const uint32_t operand = input->phi_operands[input->phi_operand_offsets[k] + j];
                        if (pass == 0)
                        {
                            pass_offsets[operand + 1] += 1;
                        }
                        else
                        {
                            pass_blocks[pass_offsets[operand]] = input->predecessors[input->predecessor_offsets[b] + j];
                            pass_offsets[operand] += 1;
                        }
                    }
                }
            }
            if (pass == 0)
            {
                for (uint32_t v = 0; v < values; v += 1)
                {
                    pass_offsets[v + 1] += pass_offsets[v];
                }
            }
            else
            {
                // The fill advanced each start to its end: shift them back.
                for (uint32_t v = values; v > 0; v -= 1)
                {
                    pass_offsets[v] = pass_offsets[v - 1];
                }
                pass_offsets[0] = 0;
            }
        }
    }
    // Liveness by walking every use back to its definition: live-in pairs, counted, then filled.
    uint32_t* home = valid ? arena_push_array(scratch, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* marks = valid ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* stack = valid ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* live_offsets = valid ? arena_push_array(scratch, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    valid = valid && ((home != 0 && marks != 0 && stack != 0 && live_offsets != 0) || tier1_cfg_fail(check, "checker memory", 0, 0, 0));
    uint32_t* live = 0;
    for (uint32_t pass = 0; valid && pass < 2; pass += 1)
    {
        uint32_t* fill = 0;
        if (pass == 0)
        {
            memset(live_offsets, 0, ((uint64_t)blocks + 1) * sizeof(uint32_t));
            for (uint32_t v = 0; v < values; v += 1)
            {
                home[v] = tier1_cfg_block_of(input, input->definitions[v]);
            }
        }
        else
        {
            for (uint32_t b = 0; b < blocks; b += 1)
            {
                live_offsets[b + 1] += live_offsets[b];
            }
            live = arena_push_array(scratch, (uint64_t)live_offsets[blocks] + 1, sizeof(uint32_t), _Alignof(uint32_t));
            fill = arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t));
            valid = (live != 0 && fill != 0) || tier1_cfg_fail(check, "checker memory", 0, 0, 0);
            for (uint32_t b = 0; valid && b < blocks; b += 1)
            {
                fill[b] = live_offsets[b];
            }
        }
        memset(marks, 0xff, (uint64_t)blocks * sizeof(uint32_t));
        for (uint32_t v = 0; valid && v < values; v += 1)
        {
            uint32_t depth = 0;
            // Seeds: ordinary uses outside the home block, and every predecessor that passes v to a phi.
            for (uint32_t u = input->use_offsets[v]; u < input->use_offsets[v + 1]; u += 1)
            {
                const uint32_t b = tier1_cfg_block_of(input, input->use_positions[u]);
                if (b != home[v] && marks[b] != v)
                {
                    marks[b] = v;
                    stack[depth] = b;
                    depth += 1;
                }
            }
            for (uint32_t i = pass_offsets[v]; i < pass_offsets[v + 1]; i += 1)
            {
                const uint32_t p = pass_blocks[i];
                if (p != home[v] && marks[p] != v)
                {
                    marks[p] = v;
                    stack[depth] = p;
                    depth += 1;
                }
            }
            // Each marked block is live-in; walk its predecessors until the home block.
            while (valid && depth != 0)
            {
                depth -= 1;
                const uint32_t b = stack[depth];
                valid = b != 0 || tier1_cfg_fail(check, "a value live into the entry", 0, v, 0);
                if (pass == 0)
                {
                    live_offsets[b + 1] += 1;
                }
                else
                {
                    live[fill[b]] = v;
                    fill[b] += 1;
                }
                for (uint32_t e = input->predecessor_offsets[b]; valid && e < input->predecessor_offsets[b + 1]; e += 1)
                {
                    const uint32_t q = input->predecessors[e];
                    if (q != home[v] && marks[q] != v)
                    {
                        marks[q] = v;
                        stack[depth] = q;
                        depth += 1;
                    }
                }
                check->checks += 1;
            }
        }
    }
    // Events by position: fragment transitions, uses, clobbers and definitions (counted, then filled).
    uint32_t* event_offsets = valid ? arena_push_array(scratch, (uint64_t)input->position_count + 2, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    valid = valid && ((event_offsets != 0) || tier1_cfg_fail(check, "checker memory", 0, 0, 0));
    uint32_t* events = 0;
    uint32_t* event_fill = 0;
    for (uint32_t pass = 0; valid && pass < 2; pass += 1)
    {
        if (pass == 0)
        {
            memset(event_offsets, 0, ((uint64_t)input->position_count + 2) * sizeof(uint32_t));
        }
        else
        {
            for (uint32_t q = 0; q <= input->position_count; q += 1)
            {
                event_offsets[q + 1] += event_offsets[q];
            }
            events = arena_push_array(scratch, (uint64_t)event_offsets[input->position_count] * 2 + 2, sizeof(uint32_t), _Alignof(uint32_t));
            event_fill = arena_push_array(scratch, (uint64_t)input->position_count + 1, sizeof(uint32_t), _Alignof(uint32_t));
            valid = (events != 0 && event_fill != 0) || tier1_cfg_fail(check, "checker memory", 0, 0, 0);
            for (uint32_t q = 0; valid && q < input->position_count; q += 1)
            {
                event_fill[q] = event_offsets[q];
            }
        }
        // Kinds: 0 transition (fragment f), 1 use (use u), 2 clobber (fixed range r), 3 definition (value v).
        for (uint32_t i = 0; valid && i < intervals->count; i += 1)
        {
            for (uint32_t f = allocation->fragment_offsets[i] + 1; f < allocation->fragment_offsets[i + 1]; f += 1)
            {
                const uint32_t q = allocation->starts[f];
                if (pass == 0)
                {
                    event_offsets[q + 1] += 1;
                }
                else
                {
                    events[(uint64_t)event_fill[q] * 2] = 0;
                    events[(uint64_t)event_fill[q] * 2 + 1] = f;
                    event_fill[q] += 1;
                }
            }
        }
        for (uint32_t v = 0; valid && v < values; v += 1)
        {
            for (uint32_t u = input->use_offsets[v]; u < input->use_offsets[v + 1]; u += 1)
            {
                const uint32_t q = input->use_positions[u];
                if (pass == 0)
                {
                    event_offsets[q + 1] += 1;
                }
                else
                {
                    events[(uint64_t)event_fill[q] * 2] = 1;
                    events[(uint64_t)event_fill[q] * 2 + 1] = u;
                    event_fill[q] += 1;
                }
            }
        }
        for (uint32_t r = 0; valid && r < input->fixed.count; r += 1)
        {
            const uint32_t q = input->fixed.starts[r];
            if (pass == 0)
            {
                event_offsets[q + 1] += 1;
            }
            else
            {
                events[(uint64_t)event_fill[q] * 2] = 2;
                events[(uint64_t)event_fill[q] * 2 + 1] = r;
                event_fill[q] += 1;
            }
        }
        for (uint32_t v = 0; valid && v < values; v += 1)
        {
            const uint32_t q = input->definitions[v];
            if (is_phi[v] == 0 && pass == 0)
            {
                event_offsets[q + 1] += 1;
            }
            else if (is_phi[v] == 0)
            {
                events[(uint64_t)event_fill[q] * 2] = 3;
                events[(uint64_t)event_fill[q] * 2 + 1] = v;
                event_fill[q] += 1;
            }
        }
    }
    // The value of each use, and the interval of each fragment.
    uint32_t* use_values = valid ? arena_push_array(scratch, (uint64_t)input->use_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* fragment_intervals = valid ? arena_push_array(scratch, (uint64_t)allocation->fragment_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    valid = valid && ((use_values != 0 && fragment_intervals != 0) || tier1_cfg_fail(check, "checker memory", 0, 0, 0));
    for (uint32_t v = 0; valid && v < values; v += 1)
    {
        for (uint32_t u = input->use_offsets[v]; u < input->use_offsets[v + 1]; u += 1)
        {
            use_values[u] = v;
        }
    }
    for (uint32_t i = 0; valid && i < intervals->count; i += 1)
    {
        for (uint32_t f = allocation->fragment_offsets[i]; f < allocation->fragment_offsets[i + 1]; f += 1)
        {
            fragment_intervals[f] = i;
        }
    }
    const uint32_t locations = BUSTER_TIER1_CFG_SPILL + allocation->spill_count + BUSTER_TIER1_ALLOCATE_CLASSES;
    Tier1CfgState state = {
        .count = locations,
    };
    Tier1CfgState edge = {
        .count = locations,
    };
    state.values = valid ? arena_push_array(scratch, locations, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    state.stamps = valid ? arena_push_array(scratch, locations, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    edge.values = valid ? arena_push_array(scratch, locations, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    edge.stamps = valid ? arena_push_array(scratch, locations, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    valid = valid && ((state.values != 0 && state.stamps != 0 && edge.values != 0 && edge.stamps != 0) || tier1_cfg_fail(check, "checker memory", 0, 0, 0));
    if (valid)
    {
        memset(state.stamps, 0, (uint64_t)locations * sizeof(uint32_t));
        memset(edge.stamps, 0, (uint64_t)locations * sizeof(uint32_t));
    }
    uint32_t split = 0;
    for (uint32_t b = 0; valid && b < blocks; b += 1)
    {
        const uint32_t entry = input->block_starts[b];
        state.stamp += 1;
        // The entry state: the live-in values and the phis where the allocation keeps them at the block's start.
        const uint32_t phi_first = input->phi_offsets[b];
        const uint32_t phi_end = input->phi_offsets[b + 1];
        for (uint32_t i = live_offsets[b]; valid && i < live_offsets[b + 1] + (phi_end - phi_first); i += 1)
        {
            const uint32_t v = i < live_offsets[b + 1] ? live[i] : input->phi_values[phi_first + i - live_offsets[b + 1]];
            uint32_t location = 0;
            uint8_t kind = 0;
            valid = tier1_cfg_entry_location(result, v, entry, &location, &kind) || tier1_cfg_fail(check, "a value live at a block's start without a location", b, v, entry);
            if (valid && kind != BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
            {
                valid = tier1_cfg_state_get(&state, location) == BUSTER_TIER1_CFG_NONE || tier1_cfg_fail(check, "two values live in one location at a block's start", b, v, entry);
                tier1_cfg_state_set(&state, location, v);
            }
            check->checks += 1;
        }
        for (uint32_t q = entry; valid && q < input->block_starts[b + 1]; q += 1)
        {
            // Stores, then a split's moves, then loads and rematerializations, uses, clobbers and definitions.
            for (uint32_t step = 0; valid && step < 5; step += 1)
            {
                if (step == 1 && split < result->split_count && result->split_positions[split] == q)
                {
                    for (uint32_t m = result->split_move_offsets[split]; valid && m < result->split_move_offsets[split + 1]; m += 1)
                    {
                        valid = tier1_cfg_apply(input, result, &state, m, b, q, true, check);
                    }
                    split += 1;
                }
                for (uint32_t e = event_offsets[q]; valid && e < event_offsets[q + 1]; e += 1)
                {
                    const uint32_t kind = events[(uint64_t)e * 2];
                    const uint32_t item = events[(uint64_t)e * 2 + 1];
                    if (kind == 0 && (step == 0 || step == 1))
                    {
                        const uint32_t f = item;
                        const uint32_t interval = fragment_intervals[f];
                        const uint32_t v = intervals->values[interval];
                        const uint32_t class = intervals->classes[interval];
                        const uint8_t previous = allocation->kinds[f - 1];
                        const uint8_t current = allocation->kinds[f];
                        const uint32_t slot = BUSTER_TIER1_CFG_SPILL + allocation->spill_ids[v];
                        if (step == 0 && current == BUSTER_TIER1_ALLOCATE_SPILL)
                        {
                            valid = (previous == BUSTER_TIER1_ALLOCATE_REGISTER && tier1_cfg_state_get(&state, class * 32 + allocation->registers[f - 1]) == v) || tier1_cfg_fail(check, "a store of a register that doesn't hold its value", b, v, q);
                            tier1_cfg_state_set(&state, slot, v);
                        }
                        else if (step == 1 && current == BUSTER_TIER1_ALLOCATE_REGISTER)
                        {
                            const uint32_t target = class * 32 + allocation->registers[f];
                            const uint32_t from = previous == BUSTER_TIER1_ALLOCATE_SPILL ? slot : previous == BUSTER_TIER1_ALLOCATE_REGISTER ? class * 32 + allocation->registers[f - 1] : BUSTER_TIER1_CFG_REMATERIALIZE;
                            valid = (from == BUSTER_TIER1_CFG_REMATERIALIZE || tier1_cfg_state_get(&state, from) == v) || tier1_cfg_fail(check, "a reload from a location that doesn't hold its value", b, v, q);
                            valid = valid && (!tier1_cfg_clobbers(result, &state, target, v, q) || tier1_cfg_fail(check, "a reload over another live value", b, v, q));
                            tier1_cfg_state_set(&state, target, v);
                        }
                        check->checks += 1;
                    }
                    else if (kind == 1 && step == 4 && q == input->definitions[use_values[item]] && is_phi[use_values[item]] == 0)
                    {
                        // The definition's own use: where the definition puts the value, a register if it's required.
                        const uint32_t v = use_values[item];
                        uint32_t location = 0;
                        uint8_t where = 0;
                        valid = tier1_cfg_location(result, v, q, &location, &where) || tier1_cfg_fail(check, "a definition outside its value's intervals", b, v, q);
                        valid = valid && ((where == BUSTER_TIER1_ALLOCATE_REGISTER || input->use_required == 0 || input->use_required[item] == 0) || tier1_cfg_fail(check, "a required definition without a register", b, v, q));
                        check->checks += 1;
                    }
                    else if (kind == 1 && step == 2 && (q != input->definitions[use_values[item]] || is_phi[use_values[item]] != 0))
                    {
                        const uint32_t v = use_values[item];
                        uint32_t location = 0;
                        uint8_t where = 0;
                        valid = tier1_cfg_location(result, v, q, &location, &where) || tier1_cfg_fail(check, "a use outside its value's intervals", b, v, q);
                        if (valid && where == BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
                        {
                            valid = (input->use_required == 0 || input->use_required[item] == 0) || tier1_cfg_fail(check, "a required use without a register", b, v, q);
                        }
                        else if (valid)
                        {
                            valid = tier1_cfg_state_get(&state, location) == v || tier1_cfg_fail(check, "a use that finds another value", b, v, q);
                            valid = valid && ((where == BUSTER_TIER1_ALLOCATE_REGISTER || input->use_required == 0 || input->use_required[item] == 0) || tier1_cfg_fail(check, "a required use without a register", b, v, q));
                        }
                        check->checks += 1;
                    }
                    else if (kind == 2 && step == 3)
                    {
                        const uint32_t class = input->fixed.classes[item];
                        for (uint32_t r = 0; r < 32; r += 1)
                        {
                            if (((input->fixed.registers[item] >> r) & 1) != 0)
                            {
                                valid = valid && (!tier1_cfg_clobbers(result, &state, class * 32 + r, BUSTER_TIER1_CFG_NONE, q) || tier1_cfg_fail(check, "a fixed range over a live value", b, BUSTER_TIER1_CFG_NONE, q));
                                tier1_cfg_state_set(&state, class * 32 + r, BUSTER_TIER1_CFG_NONE - 1);
                            }
                        }
                        check->checks += 1;
                    }
                    else if (kind == 3 && step == 4)
                    {
                        const uint32_t v = item;
                        uint32_t location = 0;
                        uint8_t where = 0;
                        valid = tier1_cfg_location(result, v, q, &location, &where) || tier1_cfg_fail(check, "a definition outside its value's intervals", b, v, q);
                        if (valid && where != BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
                        {
                            valid = !tier1_cfg_clobbers(result, &state, location, v, q) || tier1_cfg_fail(check, "a definition over another live value", b, v, q);
                            tier1_cfg_state_set(&state, location, v);
                        }
                        check->checks += 1;
                    }
                }
            }
        }
        // Each edge: the predecessor's final state, its moves in order, then the successor's entry expectations.
        for (uint32_t e = input->successor_offsets[b]; valid && e < input->successor_offsets[b + 1]; e += 1)
        {
            const uint32_t s = input->successors[e];
            const uint32_t s_entry = input->block_starts[s];
            const uint32_t moves = result->edge_move_offsets[e + 1] - result->edge_move_offsets[e];
            const uint32_t out_degree = input->successor_offsets[b + 1] - input->successor_offsets[b];
            const uint32_t in_degree = input->predecessor_offsets[s + 1] - input->predecessor_offsets[s];
            const uint8_t placement = result->edge_placements[e];
            valid = ((moves == 0) == (placement == TIER1_CFG_EDGE_EMPTY)) || tier1_cfg_fail(check, "an edge's placement disagrees with its moves", b, BUSTER_TIER1_CFG_NONE, s_entry);
            valid = valid && ((placement != TIER1_CFG_EDGE_PREDECESSOR || out_degree == 1) || tier1_cfg_fail(check, "moves at the end of a block with several successors", b, BUSTER_TIER1_CFG_NONE, s_entry));
            valid = valid && ((placement != TIER1_CFG_EDGE_SUCCESSOR || in_degree == 1) || tier1_cfg_fail(check, "moves at the start of a block with several predecessors", b, BUSTER_TIER1_CFG_NONE, s_entry));
            edge.stamp = state.stamp;
            memcpy(edge.values, state.values, (uint64_t)locations * sizeof(uint32_t));
            memcpy(edge.stamps, state.stamps, (uint64_t)locations * sizeof(uint32_t));
            for (uint32_t m = result->edge_move_offsets[e]; valid && m < result->edge_move_offsets[e + 1]; m += 1)
            {
                valid = tier1_cfg_apply(input, result, &edge, m, b, s_entry, false, check);
            }
            uint32_t j = 0;
            while (valid && input->predecessors[input->predecessor_offsets[s] + j] != b)
            {
                j += 1;
            }
            const uint32_t s_phis = input->phi_offsets[s + 1] - input->phi_offsets[s];
            for (uint32_t i = live_offsets[s]; valid && i < live_offsets[s + 1] + s_phis; i += 1)
            {
                const bool phi = i >= live_offsets[s + 1];
                const uint32_t k = phi ? input->phi_offsets[s] + i - live_offsets[s + 1] : 0;
                const uint32_t v = phi ? input->phi_values[k] : live[i];
                const uint32_t expected = phi ? input->phi_operands[input->phi_operand_offsets[k] + j] : v;
                uint32_t location = 0;
                uint8_t kind = 0;
                valid = tier1_cfg_entry_location(result, v, s_entry, &location, &kind) || tier1_cfg_fail(check, "a value live at a block's start without a location", s, v, s_entry);
                if (valid && kind == BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
                {
                    valid = (!phi && input->rematerializable != 0 && input->rematerializable[v] != 0) || tier1_cfg_fail(check, "a phi without a location", s, v, s_entry);
                }
                else if (valid)
                {
                    valid = tier1_cfg_state_get(&edge, location) == expected || tier1_cfg_fail(check, phi ? "an edge that doesn't bring a phi its operand" : "an edge that doesn't bring a live value", s, v, s_entry);
                }
                check->checks += 1;
            }
        }
    }
    valid = valid && (split == result->split_count || tier1_cfg_fail(check, "a split the blocks never reach", 0, 0, 0));
    if (valid)
    {
        check->reason = "ok";
    }
    arena_pop_to(scratch, saved);
    return valid;
}
