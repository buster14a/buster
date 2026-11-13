// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's register allocation over a CFG (#140). Include after lane.c, dataflow.c, dominators.c and tier1_allocate.c.
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
//    values times blocks. Each range a value enters a block with is hinted to the register the value has at the exit of
//    the block's anchor (#2553, tier1_cfg_anchors): its immediate dominator, or a latch laid out before it, of which
//    every such value is live out at once; each phi to its operand's at the exit of a chosen predecessor; and a value
//    that a phi takes, to the first register of the phi, or of the next phi along, that has one before it. Each value
//    then stays where control from its definition brings it, and an edge moves what its own source block changed.
// 3. tier1_allocate, or its scalar twin, assigns the intervals.
// 4. Each edge moves its successor's live-in values and phis from their locations at the predecessor's last position
//    to their entry locations (tier1_cfg_entry_location). A store of the same value to its own slot is omitted where
//    the predecessor block already establishes that slot (tier1_cfg_slot_current, #1216). A changed phi operand still
//    writes it. Those boundary locations are one forward sweep of each value's intervals (#1399), not a binary search
//    per live value per edge. Emission resolves a position from the first interval that overlaps its block, when that
//    column fits. tier1_resolve_moves orders each class's moves, breaking a
//    cycle with the class's scratch register or, where one of the class's moves goes slot to slot, with the class's
//    temporary slot; a slot-to-slot move then goes through the scratch register, which holds nothing from one move to
//    the next. Recomputing a rematerializable value comes last. The moves go at the predecessor's end when it has one
//    successor (after its last instruction: a jump there must read no allocated value), at the successor's start when
//    it has one predecessor, and otherwise on the split edge.
// 5. Each split, a position inside a block where pieces of values' ranges meet, moves each of those values from its
//    location just before the position to its new piece's, resolved as an edge's moves are (a value keeps one slot,
//    so none goes slot to slot). They run at the position after its stores and before its reloads.
//
// With slot_widths, a phi web's spilled values share a slot where no two of them are live at once
// (tier1_cfg_share_slots, #1216), after the allocation and before the moves: an edge then moves nothing for an
// operand already in its phi's slot.
//
// Location ids: class * 32 + register for registers, BUSTER_TIER1_CFG_SPILL + the value's spill id for its slot, and
// BUSTER_TIER1_CFG_SPILL + spill_count + class for the class's temporary slot, which temp_slots marks where a move
// uses it. A move whose source is BUSTER_TIER1_CFG_REMATERIALIZE recomputes move_values[m] into its destination.
//
// The classes are tier1_allocate.c's: general, vector and opmask registers (#179). A mask value takes one of k1 to k7
// (BUSTER_TIER1_ALLOCATE_MASK_REGISTERS: k0 can't be a write mask, and validation refuses a class or a value that
// allows it), and the class's scratch register is a k register outside its `registers`: k0 for the producers here,
// which leaves the seven others to the values. A consumer moves, spills and reloads a mask at its width; calls clobber
// every opmask register (the System V convention passes none and preserves none), which the producer's fixed ranges say.
//
// tier1_cfg_verify is an independent oracle for tests and qualification, not the production path. It computes
// liveness its own way (each use walked back to its definition), simulates every block's locations as the emitter
// would run the fragments and splits (stores, then a split's moves, then loads and rematerializations, at each
// position; fixed ranges clobber their registers), checks that every use finds its value, that each piece of a range
// cut inside a block finds its value where the piece keeps it once the split's moves have run (whether or not a use
// reads it there), and that no write lands on another value the allocation still keeps there, and applies each edge's
// moves in order to the predecessor's final state, which must then hold every live-in value and each phi's operand
// where the successor expects them.
//
// Interval construction, move resolution and the verifier's search stay scalar, around the vector liveness solver and
// the vector allocator. The boundary sweep is scalar too: each value's intervals are a variable-length run, and one
// forward pass removes the dependent searches that were the measured cost. A vectorized search would still do every
// lookup. The position-to-block column is a sequential fill. Measure a vector path for these on the 9700X before adding one.
// Inputs are borrowed and immutable. Output and scratch are distinct arenas; failure restores both lengths and leaves
// the result unchanged.

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BUSTER_TIER1_CFG_SPILL (32u * BUSTER_TIER1_ALLOCATE_CLASSES)
#define BUSTER_TIER1_CFG_REMATERIALIZE 0x80000000u
#define BUSTER_TIER1_CFG_NONE UINT32_MAX
// Retained position-to-block column, four bytes each, at or below this many positions.
#define BUSTER_TIER1_CFG_POSITION_BLOCKS (1u << 20)
// First interval of a value overlapping each block (#1399). Sixteen mebibytes of entries at most.
#define BUSTER_TIER1_CFG_BLOCK_INTERVALS (1u << 22)
// Below this many intervals a binary search is at most three steps, so a partial column spends its budget on wider values.
#define BUSTER_TIER1_CFG_LOOKUP_WIDE 8u
// At most this many values share one slot (tier1_cfg_share_slots): each merge checks every pair of the two slots' values.
#define BUSTER_TIER1_CFG_SLOT_WEB 32u

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
    // Optional: the registers each value's definition must be in (a parameter's incoming one), a subset of its allowed
    // ones, on the interval that holds the definition alone; allowed_registers holds on the others, and on every interval
    // of a value whose entry is 0.
    const uint32_t* definition_registers;
    // Optional: a value that may be recomputed rather than spilled.
    const uint8_t* rematerializable;
    // Ordinary uses, sorted per value: not phi operands. use_required is optional, as tier1_allocate takes it.
    const uint32_t* use_offsets;
    const uint32_t* use_positions;
    const uint8_t* use_required;
    // Block b's phis are phi_values[phi_offsets[b] .. phi_offsets[b + 1]); phi k's operand from its block's j-th
    // predecessor is phi_operands[phi_operand_offsets[k] + j], or BUSTER_TIER1_CFG_NONE for an undefined one (the IR's
    // undef): any value is the phi's along that edge, so the edge moves nothing for it and nothing is live for it there.
    const uint32_t* phi_offsets;
    const uint32_t* phi_values;
    const uint32_t* phi_operand_offsets;
    const uint32_t* phi_operands;
    // Optional, per edge: whether its consumer copies something along it that the allocation doesn't see (Tier 1's long
    // doubles' phis, #141): it is placed as an edge with moves, at its predecessor's end, its successor's start or a
    // split's, though it has none of the allocation's.
    const uint8_t* edge_copies;
    // Optional, per value: its slot's width. A phi and an operand of its class and width then share a slot where
    // neither is live at the other's definition (tier1_cfg_share_slots, #1216); without it each value has its own.
    const uint8_t* slot_widths;
    // Optional (#2574): the allocator's clobbers (Tier1AllocationInput.clobbers), nondecreasing positions where
    // clobber_registers lose their values, as at a call: a recomputable value there leaves its register, to be recomputed
    // where a required use next needs it. The consumer saves every other value around them.
    const uint32_t* clobbers;
    uint32_t clobber_count;
    uint32_t clobber_registers[BUSTER_TIER1_ALLOCATE_CLASSES];
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
    // Position q's block, length position_count, or null when the column is not retained. The verifier does not read it.
    uint32_t* position_blocks;
    // Value v's first interval overlapping block b is value_block_intervals[value_block_base[v] + b], or
    // BUSTER_TIER1_CFG_NONE when the value misses the block. A base of BUSTER_TIER1_CFG_NONE keeps the binary search.
    // Null columns keep it for every value. Emission uses these; the verifier and the certificate do not.
    uint32_t* value_block_base;
    uint32_t* value_block_intervals;
    // The input's blocks, borrowed as `intervals` borrows its uses (tier1_cfg_entry_unread), or null for none.
    const uint32_t* block_starts;
    uint32_t block_count;
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
    valid = valid && (input->registers[BUSTER_TIER1_ALLOCATE_GPR] & 0xffff0000u) == 0 && input->scratch_registers[BUSTER_TIER1_ALLOCATE_GPR] < 16;
    valid = valid && (input->registers[BUSTER_TIER1_ALLOCATE_MASK] & ~BUSTER_TIER1_ALLOCATE_MASK_REGISTERS) == 0 && input->scratch_registers[BUSTER_TIER1_ALLOCATE_MASK] < 8;
    valid = valid && input->block_starts[0] == 0 && input->block_starts[input->block_count] == input->position_count;
    valid = valid && input->fixed.count <= BUSTER_TIER1_ALLOCATE_MAX_INTERVALS && (input->fixed.count == 0 || (input->fixed.starts != 0 && input->fixed.ends != 0 && input->fixed.classes != 0 && input->fixed.registers != 0));
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
    // Transpose the successor view into the predecessor ranges, then consume each source once. Neither CSR view
    // needs sorted rows; the caller's predecessor order (and therefore its phi operands) stays unchanged. Check all
    // offsets before indexing either edge array, including intermediate offsets beyond the declared edge count.
    const uint32_t blocks = input->block_count;
    const uint32_t edges = input->edge_count;
    const uint32_t* const successor_offsets = input->successor_offsets;
    const uint32_t* const predecessor_offsets = input->predecessor_offsets;
    const uint32_t* const successors = input->successors;
    const uint32_t* const predecessor_sources = input->predecessors;
    for (uint32_t b = 0; valid && b < blocks; b += 1)
    {
        valid = successor_offsets[b] <= successor_offsets[b + 1] && successor_offsets[b + 1] <= edges && predecessor_offsets[b] <= predecessor_offsets[b + 1] && predecessor_offsets[b + 1] <= edges;
    }
    uint32_t* cursors = valid ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* marks = valid ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* incoming = valid ? arena_push_array(scratch, (uint64_t)edges + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    valid = valid && cursors != 0 && marks != 0 && incoming != 0;
    if (valid)
    {
        memcpy(cursors, predecessor_offsets, (uint64_t)blocks * sizeof(uint32_t));
        memset(marks, 0, (uint64_t)blocks * sizeof(uint32_t));
    }
    for (uint32_t b = 0; valid && b < blocks; b += 1)
    {
        const uint32_t end = successor_offsets[b + 1];
        for (uint32_t e = successor_offsets[b]; valid && e < end; e += 1)
        {
            const uint32_t target = successors[e];
            valid = target < blocks && cursors[target] < predecessor_offsets[target + 1];
            if (valid)
            {
                incoming[cursors[target]] = b;
                cursors[target] += 1;
            }
        }
    }
    for (uint32_t b = 0; valid && b < blocks; b += 1)
    {
        const uint32_t first = predecessor_offsets[b];
        const uint32_t end = predecessor_offsets[b + 1];
        const uint32_t stamp = b + 1;
        valid = cursors[b] == end;
        for (uint32_t e = first; valid && e < end; e += 1)
        {
            const uint32_t source = incoming[e];
            valid = marks[source] != stamp;
            marks[source] = stamp;
        }
        for (uint32_t e = first; valid && e < end; e += 1)
        {
            const uint32_t source = predecessor_sources[e];
            valid = source < blocks && marks[source] == stamp;
            if (valid)
            {
                marks[source] = 0;
            }
        }
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
                valid = operand == BUSTER_TIER1_CFG_NONE || (operand < input->value_count && input->classes[operand] == input->classes[v]);
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
        valid = input->classes[v] < BUSTER_TIER1_ALLOCATE_CLASSES && (input->allowed_registers[v] & ~input->registers[input->classes[v]]) == 0 && input->definitions[v] < input->position_count &&
                (input->definition_registers == 0 || (input->definition_registers[v] & ~input->allowed_registers[v]) == 0);
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

// For a validated input, publish the sole incoming edge's successor-CSR index per block, or NONE. The emitter owns
// `edges`, a disjoint block_count-entry scratch column. One edge sweep replaces a search per single-predecessor block.
static void tier1_cfg_single_predecessor_edges(const Tier1CfgInput* input, uint32_t* edges)
{
    const uint32_t blocks = input->block_count;
    const uint32_t count = input->edge_count;
    const uint32_t* const successors = input->successors;
    const uint32_t* const offsets = input->predecessor_offsets;
    memset(edges, 0xff, (uint64_t)blocks * sizeof(uint32_t));
    for (uint32_t e = 0; e < count; e += 1)
    {
        const uint32_t target = successors[e];
        if (offsets[target + 1] - offsets[target] == 1)
        {
            edges[target] = e;
        }
    }
}

// Value v's interval that covers `position`, by binary search, or BUSTER_TIER1_CFG_NONE. The verifier and the
// certificate use this. Emission uses tier1_cfg_interval_at, which reads the block column when it is present.
static uint32_t tier1_cfg_interval_oracle(const Tier1CfgAllocation* result, uint32_t value, uint32_t position)
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

// The same answer from the block column: the stored interval is the first that overlaps the position's block, and the
// value's later pieces in that block are the only ones that can still cover the position.
static uint32_t tier1_cfg_interval_at(const Tier1CfgAllocation* result, uint32_t value, uint32_t position)
{
    const uint32_t* bases = result->value_block_base;
    const uint32_t* blocks = result->position_blocks;
    const uint32_t* slots = result->value_block_intervals;
    const bool mapped = bases != 0 && blocks != 0 && slots != 0 && position < result->intervals.position_count && bases[value] != BUSTER_TIER1_CFG_NONE;
    uint32_t found = BUSTER_TIER1_CFG_NONE;
    if (mapped)
    {
        const uint32_t last = result->value_interval_offsets[value + 1];
        uint32_t interval = slots[bases[value] + blocks[position]];
        while (interval != BUSTER_TIER1_CFG_NONE && interval < last && result->intervals.ends[interval] <= position)
        {
            interval += 1;
        }
        if (interval != BUSTER_TIER1_CFG_NONE && interval < last && result->intervals.starts[interval] <= position)
        {
            found = interval;
        }
    }
    else
    {
        found = tier1_cfg_interval_oracle(result, value, position);
    }
    return found;
}

// The fragment of `interval` that covers `position`, encoded as a location id.
static bool tier1_cfg_interval_location(const Tier1CfgAllocation* result, uint32_t value, uint32_t interval, uint32_t position, uint32_t* location, uint8_t* kind)
{
    const Tier1Allocation* allocation = &result->allocation;
    const uint32_t last = allocation->fragment_offsets[interval + 1];
    uint32_t low = allocation->fragment_offsets[interval];
    uint32_t high = last;
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
    const bool found = low < last && allocation->starts[low] <= position;
    if (found)
    {
        const uint8_t fragment_kind = allocation->kinds[low];
        *kind = fragment_kind;
        *location = fragment_kind == BUSTER_TIER1_ALLOCATE_REGISTER ? (uint32_t)result->intervals.classes[interval] * 32 + allocation->registers[low] : fragment_kind == BUSTER_TIER1_ALLOCATE_SPILL ? BUSTER_TIER1_CFG_SPILL + allocation->spill_ids[value] : BUSTER_TIER1_CFG_REMATERIALIZE;
    }
    return found;
}

// Whether a recomputed value's (#178) register fragment `fragment` of `interval` reads nothing from block entry `entry`
// on: it serves no use from there to its end, where the interval's next fragment is the recipe's, before the block's
// last position, so no later fragment takes the register's contents (a transition into a recipe moves nothing; the next
// register fragment recomputes) and no edge leaving the block reads it (an interval may continue into the layout
// successor, and edges' reads aren't uses). An edge into the block then needn't recompute the value into that register:
// control enters with the value's recipe (#1217), as tier1_emit_cfg_dead_definition leaves out a definition's. Without
// it every edge into a loop's dispatch block recomputed a table's or a frame object's address that the block overwrote
// before any use. Without use or block columns nothing is proven. Uses are sorted per interval's slice. The caller has
// checked the cheap part (tier1_cfg_interval_entry): this is the rare proof, out of the hot path, as
// tier1_cfg_slot_current is (#1217's compile-time measurement: inlined whole, every entry paid for it).
static __attribute__((noinline)) bool tier1_cfg_entry_unread(const Tier1CfgAllocation* result, uint32_t interval, uint32_t fragment, uint32_t entry)
{
    const Tier1Allocation* allocation = &result->allocation;
    const Tier1AllocationInput* intervals = &result->intervals;
    bool unread = intervals->use_offsets != 0 && intervals->use_positions != 0 && result->block_starts != 0 && result->block_count != 0;
    if (unread)
    {
        // The block that holds `entry`: the last whose start is at or before it.
        uint32_t low = 0;
        uint32_t high = result->block_count;
        while (low + 1 < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            if (result->block_starts[middle] <= entry)
            {
                low = middle;
            }
            else
            {
                high = middle;
            }
        }
        // Only where control enters a block: a split's piece starts inside one (tier1_cfg_verify refuses one at a block's
        // start), and the verifier checks each piece holds its value whether or not a use reads it there.
        unread = result->block_starts[low] == entry && allocation->ends[fragment] < result->block_starts[low + 1];
    }
    if (unread)
    {
        const uint32_t last = intervals->use_offsets[interval + 1];
        uint32_t low = intervals->use_offsets[interval];
        uint32_t high = last;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            if (intervals->use_positions[middle] < entry)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        unread = low == last || intervals->use_positions[low] >= allocation->ends[fragment];
    }
    return unread;
}

// The fragment of `interval` that covers `position`, BUSTER_TIER1_CFG_NONE for none.
static uint32_t tier1_cfg_interval_fragment(const Tier1CfgAllocation* result, uint32_t interval, uint32_t position)
{
    const Tier1Allocation* allocation = &result->allocation;
    const uint32_t last = allocation->fragment_offsets[interval + 1];
    uint32_t low = allocation->fragment_offsets[interval];
    uint32_t high = last;
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
    return low < last && allocation->starts[low] <= position ? low : BUSTER_TIER1_CFG_NONE;
}

// A value's location where control enters its block at `entry`, from `interval`, which covers it: its fragment at
// `position` (entry, or entry - 1 for an interval that continues from the layout predecessor), found once and encoded
// as tier1_cfg_interval_location encodes it, or the recipe where that register fragment reads nothing from the entry on
// (tier1_cfg_entry_unread). Only a register fragment of a recomputed value with a recipe's fragment after it can be one.
static inline __attribute__((always_inline)) bool tier1_cfg_interval_entry(const Tier1CfgAllocation* result, uint32_t value, uint32_t interval, uint32_t position, uint32_t entry, uint32_t* location, uint8_t* kind)
{
    const Tier1Allocation* allocation = &result->allocation;
    const uint32_t fragment = tier1_cfg_interval_fragment(result, interval, position);
    const bool found = fragment != BUSTER_TIER1_CFG_NONE;
    if (found)
    {
        const uint8_t fragment_kind = allocation->kinds[fragment];
        const bool candidate = fragment_kind == BUSTER_TIER1_ALLOCATE_REGISTER && fragment + 1 < allocation->fragment_offsets[interval + 1] &&
                               allocation->kinds[fragment + 1] == BUSTER_TIER1_ALLOCATE_REMATERIALIZE && result->intervals.rematerializable != 0 &&
                               result->intervals.rematerializable[interval] != 0;
        const bool recipe = candidate && tier1_cfg_entry_unread(result, interval, fragment, entry);
        *kind = recipe ? (uint8_t)BUSTER_TIER1_ALLOCATE_REMATERIALIZE : fragment_kind;
        *location = recipe || fragment_kind == BUSTER_TIER1_ALLOCATE_REMATERIALIZE ? BUSTER_TIER1_CFG_REMATERIALIZE :
                    fragment_kind == BUSTER_TIER1_ALLOCATE_REGISTER                  ? (uint32_t)result->intervals.classes[interval] * 32 + allocation->registers[fragment] :
                                                                                       BUSTER_TIER1_CFG_SPILL + allocation->spill_ids[value];
    }
    return found;
}

static bool tier1_cfg_location_mode(const Tier1CfgAllocation* result, uint32_t value, uint32_t position, uint32_t* location, uint8_t* kind, bool oracle)
{
    const uint32_t interval = oracle ? tier1_cfg_interval_oracle(result, value, position) : tier1_cfg_interval_at(result, value, position);
    return interval != BUSTER_TIER1_CFG_NONE && tier1_cfg_interval_location(result, value, interval, position, location, kind);
}

// The location of value v at position q: its interval's fragment there. Returns false if no interval of v covers q.
static bool tier1_cfg_location(const Tier1CfgAllocation* result, uint32_t value, uint32_t position, uint32_t* location, uint8_t* kind)
{
    return tier1_cfg_location_mode(result, value, position, location, kind, false);
}

static bool tier1_cfg_location_oracle(const Tier1CfgAllocation* result, uint32_t value, uint32_t position, uint32_t* location, uint8_t* kind)
{
    return tier1_cfg_location_mode(result, value, position, location, kind, true);
}

// Where a value must be when control enters its block at `entry`: for an interval that continues from the block's
// layout predecessor, where it was there, since a fragment transition at `entry` runs inside the block; otherwise its
// location at `entry`. The interval that covers `entry` also covers `entry - 1` when it continues. A recomputed value
// whose register there reads nothing from the entry on enters as its recipe (tier1_cfg_entry_unread).
static bool tier1_cfg_entry_location_mode(const Tier1CfgAllocation* result, uint32_t value, uint32_t entry, uint32_t* location, uint8_t* kind, bool oracle)
{
    const uint32_t interval = oracle ? tier1_cfg_interval_oracle(result, value, entry) : tier1_cfg_interval_at(result, value, entry);
    const uint32_t position = interval != BUSTER_TIER1_CFG_NONE && result->intervals.starts[interval] < entry ? entry - 1 : entry;
    return interval != BUSTER_TIER1_CFG_NONE && tier1_cfg_interval_entry(result, value, interval, position, entry, location, kind);
}

static bool tier1_cfg_entry_location(const Tier1CfgAllocation* result, uint32_t value, uint32_t entry, uint32_t* location, uint8_t* kind)
{
    return tier1_cfg_entry_location_mode(result, value, entry, location, kind, false);
}

static bool tier1_cfg_entry_location_oracle(const Tier1CfgAllocation* result, uint32_t value, uint32_t entry, uint32_t* location, uint8_t* kind)
{
    return tier1_cfg_entry_location_mode(result, value, entry, location, kind, true);
}

// Last spill end at each fragment, by value and layout position. The prefix includes earlier pieces of the
// value's range, so a reload after a fixed-register split keeps the proof. Zero means no spill. This is a linear
// pass over spilled values and their fragments; edge queries must not rescan fragments for each successor of a
// dispatch block. A queried unchanged value has a spill destination, so it has a spill id, which values sharing a slot
// (tier1_cfg_share_slots) each have. Register-only values need no prefix. Column pointers stay local under
// -fno-strict-aliasing.
static void tier1_cfg_spill_prefix(const Tier1CfgAllocation* result, uint32_t* spill_ends)
{
    const uint32_t values = result->intervals.value_count;
    const uint32_t* spill_ids = result->allocation.spill_ids;
    const uint32_t* value_offsets = result->value_interval_offsets;
    const uint32_t* fragment_offsets = result->allocation.fragment_offsets;
    const uint32_t* ends = result->allocation.ends;
    const uint8_t* kinds = result->allocation.kinds;
    for (uint32_t v = 0; v < values; v += 1)
    {
        uint32_t end = 0;
        const uint32_t first = spill_ids[v] != UINT32_MAX ? fragment_offsets[value_offsets[v]] : 0;
        const uint32_t last = spill_ids[v] != UINT32_MAX ? fragment_offsets[value_offsets[v + 1]] : 0;
        for (uint32_t f = first; f < last; f += 1)
        {
            end = kinds[f] == BUSTER_TIER1_ALLOCATE_SPILL ? ends[f] : end;
            spill_ends[f] = end;
        }
    }
}

// Whether this block already establishes value's own slot before position. A spill in the block, including its
// incoming slot state, remains current after reloads: an SSA value cannot change inside its block. A spill from an
// earlier layout block proves nothing. The equality case is a reload at the block's first position in a continuing
// interval: tier1_cfg_entry_location requires the preceding spill location on every incoming edge. A new interval
// at that same position has no such contract. The prefix query is logarithmic, like tier1_cfg_location's.
// Keep this rare proof out of the hot edge helper so the common edge path can be inlined.
static __attribute__((noinline)) bool tier1_cfg_slot_current(const Tier1CfgAllocation* result, const uint32_t* spill_ends, uint32_t value, uint32_t position, uint32_t block_start)
{
    const uint32_t interval = tier1_cfg_interval_at(result, value, position);
    bool current = false;
    if (interval != BUSTER_TIER1_CFG_NONE)
    {
        const uint32_t* offsets = result->allocation.fragment_offsets;
        const uint32_t* ends = result->allocation.ends;
        uint32_t low = offsets[interval];
        uint32_t high = offsets[interval + 1];
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            if (ends[middle] <= position)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        if (low < offsets[interval + 1])
        {
            const uint32_t end = spill_ends[low];
            current = end > block_start || (end == block_start && result->intervals.starts[interval] < block_start);
        }
    }
    return current;
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
    // Ordinary CFG edges only: each fragment's last spill end, and the predecessor's first position.
    const uint32_t* spill_ends;
    uint32_t block_start;
};

static uint8_t tier1_cfg_kind_of_location(uint32_t location)
{
    uint8_t kind = BUSTER_TIER1_ALLOCATE_REGISTER;
    if (location == BUSTER_TIER1_CFG_REMATERIALIZE)
    {
        kind = BUSTER_TIER1_ALLOCATE_REMATERIALIZE;
    }
    else if (location >= BUSTER_TIER1_CFG_SPILL)
    {
        kind = BUSTER_TIER1_ALLOCATE_SPILL;
    }
    return kind;
}

// Adds the move `carried` needs to take `value`'s place, when the two locations differ and the destination is not a
// recomputation. A matching pair, a rematerialized destination, or a slot the predecessor already holds, adds nothing.
static void tier1_cfg_edge_place(const Tier1CfgInput* input, const Tier1CfgAllocation* result, Tier1CfgEdge* edge, uint32_t value, uint32_t carried, uint32_t from, uint8_t from_kind, uint32_t to, uint8_t to_kind, uint32_t exit)
{
    // A self-phi or an unchanged live-through value need not store a register back to its current slot. A different
    // phi operand must still write it, even when the phi's old value is present there.
    const bool current_slot = from_kind == BUSTER_TIER1_ALLOCATE_REGISTER && to_kind == BUSTER_TIER1_ALLOCATE_SPILL && value == carried &&
                              edge->spill_ends != 0 && tier1_cfg_slot_current(result, edge->spill_ends, value, exit, edge->block_start);
    if (to_kind != BUSTER_TIER1_ALLOCATE_REMATERIALIZE && from != to && !current_slot)
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
}

// One value of an edge successor's entry state whose locations were already resolved. Returns false where either side
// has no location.
static bool tier1_cfg_edge_known(const Tier1CfgInput* input, const Tier1CfgAllocation* result, Tier1CfgEdge* edge, uint32_t value, uint32_t carried, uint32_t from, uint32_t to, uint32_t exit)
{
    const bool found = from != BUSTER_TIER1_CFG_NONE && to != BUSTER_TIER1_CFG_NONE;
    if (found)
    {
        tier1_cfg_edge_place(input, result, edge, value, carried, from, tier1_cfg_kind_of_location(from), to, tier1_cfg_kind_of_location(to), exit);
    }
    return found;
}

// One value of an edge successor's entry state: `value` there, `carried` (the value itself, or a phi's operand) from
// the predecessor's exit. Adds its move, if it needs one, to its class's pending moves or to the recomputations.
// Returns false where either side has no location. Splits use this; edges use the swept locations.
static bool tier1_cfg_edge_value(const Tier1CfgInput* input, const Tier1CfgAllocation* result, Tier1CfgEdge* edge, uint32_t value, uint32_t carried, uint32_t exit, uint32_t entry)
{
    uint32_t from = 0;
    uint32_t to = 0;
    uint8_t from_kind = 0;
    uint8_t to_kind = 0;
    const bool found = tier1_cfg_location(result, carried, exit, &from, &from_kind) && tier1_cfg_entry_location(result, value, entry, &to, &to_kind);
    if (found)
    {
        tier1_cfg_edge_place(input, result, edge, value, carried, from, from_kind, to, to_kind, exit);
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
    uint32_t* hint_intervals;
    uint32_t* split_positions;
    uint32_t* split_values;
    uint64_t count;
    uint64_t splits;
    bool fill;
};

// A new interval of value v over [start, end), hinted to `hint`'s register, or first to the one interval `earlier`
// started in; a split where it continues v's range in its block.
static void tier1_cfg_piece(Tier1CfgPieces* pieces, const Tier1CfgInput* input, uint32_t v, uint32_t start, uint32_t end, uint32_t hint, uint32_t earlier, bool split)
{
    if (pieces->fill)
    {
        const uint64_t i = pieces->count;
        pieces->starts[i] = start;
        pieces->ends[i] = end;
        pieces->values[i] = v;
        const uint32_t defined = input->definition_registers != 0 ? input->definition_registers[v] : 0;
        pieces->allowed[i] = defined != 0 && start <= input->definitions[v] && input->definitions[v] < end ? defined : input->allowed_registers[v];
        pieces->classes[i] = input->classes[v];
        pieces->remat[i] = input->rematerializable != 0 ? input->rematerializable[v] : 0;
        pieces->hints[i] = hint;
        pieces->hint_intervals[i] = earlier;
        if (split)
        {
            pieces->split_positions[pieces->splits] = start;
            pieces->split_values[pieces->splits] = v;
        }
    }
    pieces->count += 1;
    pieces->splits += split;
}

// Each block's anchor and chosen predecessor (#2553), from dominators.c's tree, which `scratch` holds until they are read,
// with its temporaries on `output`; both arenas end as they began, but for the caller's two columns. False where the
// analysis fails.
// - The chosen predecessor, by its index among the block's, is the one whose exit its phis take their operands' places
//   from: one laid out before the block that the block dominates (a loop's latch, whose back edge runs every
//   iteration: the nearest in layout of them), else the immediate dominator where it is a predecessor laid out before
//   the block (its edge then moves no value the block takes from before it), else the nearest predecessor before the
//   block in layout, which the allocator has most likely placed last; none where every predecessor follows the block.
// - The anchor is the block whose exit the block takes its other values' places from: such a latch, as for the phis,
//   since the loop's body, laid out before its header, has placed them; else the immediate dominator where that is laid
//   out before the block; else none: the entry, an unreachable block, or one whose dominator follows it, as a dispatch
//   loop's handlers' join does, whose own conventions (`siblings`, `firsts`) take them. A value live into a block is live
//   out of each of its predecessors, and out of its immediate dominator, unless the block is its definition's: the values
//   a block takes from its anchor's exit are there at once, each in its own place.
static bool tier1_cfg_anchors(const Tier1CfgInput* input, Arena* output, Arena* scratch, uint32_t* anchors, uint32_t* chosen)
{
    const uint64_t mark = scratch->length;
    const DominatorCfg graph = {
        .block_count = input->block_count,
        .edge_count = input->edge_count,
        .entry = 0,
        .successor_offsets = input->successor_offsets,
        .successors = input->successors,
        .predecessor_offsets = input->predecessor_offsets,
        .predecessors = input->predecessors,
    };
    DominatorOptions options = dominators_options();
    options.outputs = 0;
    Dominators tree = { 0 };
    const bool analyzed = dominators_analyze(scratch, output, &graph, options, &tree) == DOMINATOR_OK;
    for (uint32_t b = 0; analyzed && b < input->block_count; b += 1)
    {
        const uint32_t dominator = b != 0 ? tree.idom[b] : BUSTER_TIER1_CFG_NONE;
        anchors[b] = dominator < b ? dominator : BUSTER_TIER1_CFG_NONE;
        uint32_t latch = BUSTER_TIER1_CFG_NONE;
        uint32_t anchored = BUSTER_TIER1_CFG_NONE;
        uint32_t nearest = BUSTER_TIER1_CFG_NONE;
        for (uint32_t j = 0; j < input->predecessor_offsets[b + 1] - input->predecessor_offsets[b]; j += 1)
        {
            const uint32_t p = input->predecessors[input->predecessor_offsets[b] + j];
            const bool earlier = p < b;
            const bool closer = nearest == BUSTER_TIER1_CFG_NONE || p > input->predecessors[input->predecessor_offsets[b] + nearest];
            const bool back = earlier && dominators_dominates(&tree, b, p);
            latch = back && (latch == BUSTER_TIER1_CFG_NONE || p > input->predecessors[input->predecessor_offsets[b] + latch]) ? j : latch;
            anchored = earlier && p == anchors[b] ? j : anchored;
            nearest = earlier && closer ? j : nearest;
        }
        chosen[b] = latch != BUSTER_TIER1_CFG_NONE ? latch : anchored != BUSTER_TIER1_CFG_NONE ? anchored : nearest;
        anchors[b] = latch != BUSTER_TIER1_CFG_NONE ? input->predecessors[input->predecessor_offsets[b] + latch] : anchors[b];
    }
    arena_pop_to(scratch, mark);
    return analyzed;
}

// The interval of value v's run [first, end) that holds `position`, or BUSTER_TIER1_CFG_NONE: the runs are in position
// order and don't overlap.
static uint32_t tier1_cfg_piece_at(const Tier1CfgPieces* pieces, uint32_t first, uint32_t end, uint32_t position)
{
    uint32_t low = first;
    uint32_t high = end;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (pieces->starts[middle] <= position)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low > first && pieces->ends[low - 1] > position ? low - 1 : BUSTER_TIER1_CFG_NONE;
}

// A phi's interval that no sibling convention models (#1216) takes its operand's register at the exit of its block's
// chosen predecessor (#2553, tier1_cfg_anchors). Every value's intervals must be filled: an operand's may follow its
// phi's.
static void tier1_cfg_phi_exits(const Tier1CfgInput* input, const uint32_t* chosen, const uint32_t* offsets, Tier1CfgPieces* pieces)
{
    for (uint32_t b = 0; b < input->block_count; b += 1)
    {
        const uint32_t j = chosen[b];
        const uint32_t predecessor = j != BUSTER_TIER1_CFG_NONE ? input->predecessors[input->predecessor_offsets[b] + j] : BUSTER_TIER1_CFG_NONE;
        const uint32_t exit = predecessor != BUSTER_TIER1_CFG_NONE ? input->block_starts[predecessor + 1] - 1 : 0;
        for (uint32_t k = input->phi_offsets[b]; predecessor != BUSTER_TIER1_CFG_NONE && k < input->phi_offsets[b + 1]; k += 1)
        {
            const uint32_t v = input->phi_values[k];
            const uint32_t operand = input->phi_operands[input->phi_operand_offsets[k] + j];
            const uint32_t own = tier1_cfg_piece_at(pieces, offsets[v], offsets[v + 1], input->block_starts[b]);
            const uint32_t taken = operand != BUSTER_TIER1_CFG_NONE ? tier1_cfg_piece_at(pieces, offsets[operand], offsets[operand + 1], exit) : BUSTER_TIER1_CFG_NONE;
            if (own != BUSTER_TIER1_CFG_NONE && taken != BUSTER_TIER1_CFG_NONE && pieces->hint_intervals[own] == BUSTER_TIER1_CFG_NONE && pieces->classes[taken] == pieces->classes[own] &&
                pieces->ends[taken] <= pieces->starts[own])
            {
                pieces->hint_intervals[own] = taken | BUSTER_TIER1_ALLOCATE_HINT_EXIT;
            }
        }
    }
}

// The furthest a phi operand's definition looks along its phis for a register (tier1_cfg_operand_targets).
#define BUSTER_TIER1_CFG_TARGET_STEPS 8u

// A value that a phi takes along an edge, and that nothing else hints, is hinted to that phi (`targets`, from the phi
// hints). Where the phi has no interval before the value's definition, no register of it is known there: the hint goes on
// along the phis that take the phi in turn (`ups`, each value's first such phi), to the first with an interval before
// the definition (#2553), whose first interval the definition's interval takes its opening register from: a phi's own,
// or a dispatch loop's join's phi's in its first handler, the register every handler takes it in. A handler passes a
// value on through phis of its own joins to the loop's join: the value is defined in that register, so that none of
// those edges move it, though the phi may have left it in the handler since.
static void tier1_cfg_operand_targets(const Tier1CfgInput* input, const uint32_t* targets, const uint32_t* ups, const uint8_t* phis, const uint32_t* offsets, Tier1CfgPieces* pieces)
{
    for (uint32_t v = 0; v < input->value_count; v += 1)
    {
        uint32_t target = phis[v] == 0 ? targets[v] : BUSTER_TIER1_CFG_NONE;
        const uint32_t definition = input->definitions[v];
        bool unknown = target != BUSTER_TIER1_CFG_NONE && (offsets[target] == offsets[target + 1] || pieces->starts[offsets[target]] > definition);
        for (uint32_t step = 0; unknown && step < BUSTER_TIER1_CFG_TARGET_STEPS && ups[target] != BUSTER_TIER1_CFG_NONE && ups[target] != v && input->classes[ups[target]] == input->classes[v]; step += 1)
        {
            target = ups[target];
            unknown = offsets[target] == offsets[target + 1] || pieces->starts[offsets[target]] > definition;
        }
        const uint32_t own = target != BUSTER_TIER1_CFG_NONE && !unknown ? tier1_cfg_piece_at(pieces, offsets[v], offsets[v + 1], definition) : BUSTER_TIER1_CFG_NONE;
        if (own != BUSTER_TIER1_CFG_NONE)
        {
            const uint32_t first = offsets[target];
            pieces->hints[own] = target;
            pieces->hint_intervals[own] = pieces->hint_intervals[own] == BUSTER_TIER1_CFG_NONE && pieces->starts[first] < pieces->starts[own] ? first : pieces->hint_intervals[own];
        }
    }
}

// Wider values first, so a partial block column keeps the ones whose binary search is deepest (#1399). Descending:
// narrow values' zero keys sort last, where the install loop's `keys[i] != 0` stops.
static int tier1_cfg_lookup_order(const void* left, const void* right)
{
    const uint64_t a = *(const uint64_t*)left;
    const uint64_t b = *(const uint64_t*)right;
    return (a > b) - (a < b);
}

static uint32_t tier1_cfg_interval_count(const Tier1CfgAllocation* result, uint32_t value)
{
    return result->value_interval_offsets[value + 1] - result->value_interval_offsets[value];
}

// The first interval of each value that overlaps a block, for emission's position lookups. A function whose values
// and blocks fit gets every value; a larger one keeps the widest values that fit in the budget. Failure leaves the
// columns null and the binary search in place, and restores the output length.
static void tier1_cfg_install_lookup(const Tier1CfgInput* input, Arena* output, Arena* scratch, Tier1CfgAllocation* result)
{
    const uint32_t values = input->value_count;
    const uint32_t blocks = input->block_count;
    const uint64_t mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const bool mapped = result->position_blocks != 0 && values != 0 && blocks != 0 && blocks <= BUSTER_TIER1_CFG_BLOCK_INTERVALS;
    const uint32_t room = mapped ? (uint32_t)(BUSTER_TIER1_CFG_BLOCK_INTERVALS / blocks) : 0;
    uint32_t* base = mapped && room != 0 ? arena_push_array(output, values, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    bool placed = false;
    if (base != 0)
    {
        memset(base, 0xff, (uint64_t)values * sizeof(uint32_t));
        uint32_t chosen = 0;
        if (values <= room)
        {
            for (uint32_t v = 0; v < values; v += 1)
            {
                base[v] = chosen;
                chosen += blocks;
            }
            placed = true;
        }
        else
        {
            uint64_t* keys = arena_push_array(scratch, values, sizeof(uint64_t), _Alignof(uint64_t));
            if (keys != 0)
            {
                for (uint32_t v = 0; v < values; v += 1)
                {
                    const uint32_t n = tier1_cfg_interval_count(result, v);
                    keys[v] = n >= BUSTER_TIER1_CFG_LOOKUP_WIDE ? ((uint64_t)n << 32) | (uint64_t)(UINT32_MAX - v) : 0;
                }
                qsort(keys, values, sizeof(uint64_t), tier1_cfg_lookup_order);
                for (uint32_t i = 0; i < values && chosen / blocks < room && keys[i] != 0; i += 1)
                {
                    const uint32_t v = UINT32_MAX - (uint32_t)keys[i];
                    base[v] = chosen;
                    chosen += blocks;
                }
                placed = chosen != 0;
            }
        }
        uint32_t* slots = placed ? arena_push_array(output, (uint64_t)(chosen / blocks) * blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        if (placed && slots != 0)
        {
            memset(slots, 0xff, (uint64_t)(chosen / blocks) * blocks * sizeof(uint32_t));
            const uint32_t* blocks_of = result->position_blocks;
            for (uint32_t v = 0; v < values; v += 1)
            {
                if (base[v] != BUSTER_TIER1_CFG_NONE)
                {
                    for (uint32_t i = result->value_interval_offsets[v]; i < result->value_interval_offsets[v + 1]; i += 1)
                    {
                        const uint32_t end = result->intervals.ends[i];
                        for (uint32_t b = blocks_of[result->intervals.starts[i]]; b < blocks && input->block_starts[b] < end; b += 1)
                        {
                            uint32_t* slot = slots + base[v] + b;
                            if (*slot == BUSTER_TIER1_CFG_NONE)
                            {
                                *slot = i;
                            }
                        }
                    }
                }
            }
            result->value_block_base = base;
            result->value_block_intervals = slots;
        }
        else
        {
            arena_pop_to(output, mark);
        }
    }
    arena_pop_to(scratch, scratch_mark);
}

static uint32_t tier1_cfg_slot_root(uint32_t* roots, uint32_t slot)
{
    while (roots[slot] != slot)
    {
        roots[slot] = roots[roots[slot]];
        slot = roots[slot];
    }
    return slot;
}

// Phi webs' slots (#1216): with slot_widths, a phi and each operand of its class and width, both spilled somewhere, take
// one slot where no value already sharing either one's is live at another's definition, up to
// BUSTER_TIER1_CFG_SLOT_WEB values a slot. In SSA two values' ranges overlap exactly where one is live at the other's
// definition, so those pairs are a shared slot's whole interference. A phi's slot is written by its edges' moves, which
// read its operands in the same parallel move; a value live out of the predecessor and not into the phi's block is dead
// along that edge, whose moves then run on it alone. A dispatch loop's values that rest in memory then keep one slot
// around the loop, where each edge copied them from one value's slot to the next's. Operands that are phis themselves join
// first (#2553), then the others: a web of phis that pass a value on around a loop keeps one slot, which an entering
// operand, joined first, could have split by being live at one of their definitions. Slots are renumbered in order, each
// keeping its first value in spill_values. False where scratch is short.
static bool tier1_cfg_share_slots(const Tier1CfgInput* input, Tier1CfgAllocation* result, Arena* scratch)
{
    Tier1Allocation* allocation = &result->allocation;
    const uint32_t spills = allocation->spill_count;
    uint32_t* ids = allocation->spill_ids;
    const uint64_t mark = scratch->length;
    uint32_t* roots = arena_push_array(scratch, (uint64_t)spills + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* sizes = arena_push_array(scratch, (uint64_t)spills + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* heads = arena_push_array(scratch, (uint64_t)spills + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* tails = arena_push_array(scratch, (uint64_t)spills + 1, sizeof(uint32_t), _Alignof(uint32_t));
    // Each value's next in its slot's list, and whether it is a phi.
    uint32_t* nexts = arena_push_array(scratch, (uint64_t)input->value_count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint8_t* phis = arena_push(scratch, (uint64_t)input->value_count + 1, 1);
    const bool ready = roots != 0 && sizes != 0 && heads != 0 && tails != 0 && nexts != 0 && phis != 0;
    if (ready)
    {
        memset(phis, 0, (uint64_t)input->value_count + 1);
        for (uint32_t k = 0; k < input->phi_count; k += 1)
        {
            phis[input->phi_values[k]] = 1;
        }
    }
    for (uint32_t s = 0; ready && s < spills; s += 1)
    {
        const uint32_t v = allocation->spill_values[s];
        roots[s] = s;
        sizes[s] = 1;
        heads[s] = v;
        tails[s] = v;
        nexts[v] = BUSTER_TIER1_CFG_NONE;
    }
    for (uint32_t pass = 0; pass < 2; pass += 1)
    {
        for (uint32_t k = 0; ready && k < input->phi_count; k += 1)
        {
            const uint32_t phi = input->phi_values[k];
            for (uint32_t o = input->phi_operand_offsets[k]; ids[phi] != UINT32_MAX && o < input->phi_operand_offsets[k + 1]; o += 1)
            {
                const uint32_t operand = input->phi_operands[o];
                const bool paired = operand != BUSTER_TIER1_CFG_NONE && ids[operand] != UINT32_MAX && input->classes[operand] == input->classes[phi] && input->slot_widths[operand] == input->slot_widths[phi] &&
                                    (phis[operand] != 0) == (pass == 0);
                const uint32_t into = paired ? tier1_cfg_slot_root(roots, ids[phi]) : 0;
                const uint32_t from = paired ? tier1_cfg_slot_root(roots, ids[operand]) : 0;
                bool apart = paired && into != from && sizes[into] + sizes[from] <= BUSTER_TIER1_CFG_SLOT_WEB;
                for (uint32_t m = apart ? heads[into] : BUSTER_TIER1_CFG_NONE; apart && m != BUSTER_TIER1_CFG_NONE; m = nexts[m])
                {
                    for (uint32_t n = heads[from]; apart && n != BUSTER_TIER1_CFG_NONE; n = nexts[n])
                    {
                        apart = tier1_cfg_interval_at(result, m, input->definitions[n]) == BUSTER_TIER1_CFG_NONE && tier1_cfg_interval_at(result, n, input->definitions[m]) == BUSTER_TIER1_CFG_NONE;
                    }
                }
                if (apart)
                {
                    roots[from] = into;
                    sizes[into] += sizes[from];
                    nexts[tails[into]] = heads[from];
                    tails[into] = tails[from];
                }
            }
        }
    }
    // A root's new id in \`sizes\`, which it no longer needs: earlier roots take lower ids, so each write lands at or
    // before the slot being read.
    uint32_t count = 0;
    for (uint32_t s = 0; ready && s < spills; s += 1)
    {
        if (roots[s] == s)
        {
            allocation->spill_values[count] = allocation->spill_values[s];
            sizes[s] = count;
            count += 1;
        }
    }
    for (uint32_t v = 0; ready && v < input->value_count; v += 1)
    {
        ids[v] = ids[v] != UINT32_MAX ? sizes[tier1_cfg_slot_root(roots, ids[v])] : UINT32_MAX;
    }
    allocation->spill_count = ready ? count : spills;
    arena_pop_to(scratch, mark);
    return ready;
}

// Locations at block boundaries, in the order each block's live-in and live-out bits are walked. The edge pass reads
// them. A cursor per value moves only forward: blocks are visited in layout order, and within a block the entry is
// at or before the exit. `ready` is false when the columns do not fit, and the edge pass then searches.
typedef struct Tier1CfgBoundaries Tier1CfgBoundaries;
struct Tier1CfgBoundaries
{
    uint32_t* in_offsets;
    uint32_t* out_offsets;
    uint32_t* entry_locations;
    uint32_t* exit_locations;
    uint32_t* phi_locations;
    uint32_t* exit_at;
    uint32_t* exit_stamp;
    bool ready;
};

static uint32_t tier1_cfg_cursor_cover(const Tier1CfgAllocation* result, uint32_t* cursors, uint32_t* lasts, uint32_t value, uint32_t position)
{
    const uint32_t begin = result->value_interval_offsets[value];
    const uint32_t end = result->value_interval_offsets[value + 1];
    uint32_t interval = cursors[value];
    if (lasts[value] != BUSTER_TIER1_CFG_NONE && position < lasts[value])
    {
        interval = begin;
    }
    while (interval < end && result->intervals.ends[interval] <= position)
    {
        interval += 1;
    }
    cursors[value] = interval;
    lasts[value] = position;
    return interval < end && result->intervals.starts[interval] <= position ? interval : BUSTER_TIER1_CFG_NONE;
}

static uint32_t tier1_cfg_cursor_where(const Tier1CfgAllocation* result, uint32_t* cursors, uint32_t* lasts, uint32_t value, uint32_t position)
{
    const uint32_t interval = tier1_cfg_cursor_cover(result, cursors, lasts, value, position);
    uint32_t location = BUSTER_TIER1_CFG_NONE;
    uint8_t kind = 0;
    if (interval != BUSTER_TIER1_CFG_NONE && !tier1_cfg_interval_location(result, value, interval, position, &location, &kind))
    {
        location = BUSTER_TIER1_CFG_NONE;
    }
    (void)kind;
    return location;
}

static uint32_t tier1_cfg_cursor_entry(const Tier1CfgAllocation* result, uint32_t* cursors, uint32_t* lasts, uint32_t value, uint32_t entry)
{
    const uint32_t interval = tier1_cfg_cursor_cover(result, cursors, lasts, value, entry);
    uint32_t location = BUSTER_TIER1_CFG_NONE;
    if (interval != BUSTER_TIER1_CFG_NONE)
    {
        const uint32_t position = result->intervals.starts[interval] < entry ? entry - 1 : entry;
        uint8_t kind = 0;
        if (!tier1_cfg_interval_entry(result, value, interval, position, entry, &location, &kind))
        {
            location = BUSTER_TIER1_CFG_NONE;
        }
        (void)kind;
    }
    return location;
}

static Tier1CfgBoundaries tier1_cfg_boundaries(const Tier1CfgInput* input, const Tier1CfgAllocation* result, const Dataflow* liveness, const uint32_t* dense_values, uint32_t dense_count, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint32_t blocks = input->block_count;
    const uint32_t values = input->value_count;
    const uint64_t words = liveness->row_words;
    Tier1CfgBoundaries bounds = { 0 };
    uint32_t* in_offsets = arena_push_array(scratch, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* out_offsets = arena_push_array(scratch, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* cursors = arena_push_array(scratch, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* lasts = arena_push_array(scratch, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t));
    bool ok = in_offsets != 0 && out_offsets != 0 && cursors != 0 && lasts != 0;
    uint64_t in_run = 0;
    uint64_t out_run = 0;
    for (uint32_t b = 0; ok && b < blocks; b += 1)
    {
        in_offsets[b] = (uint32_t)in_run;
        out_offsets[b] = (uint32_t)out_run;
        for (uint64_t w = 0; w < words; w += 1)
        {
            in_run += (uint32_t)__builtin_popcountll(liveness->in[(uint64_t)b * words + w]);
            out_run += (uint32_t)__builtin_popcountll(liveness->out[(uint64_t)b * words + w]);
        }
        ok = in_run <= UINT32_MAX && out_run <= UINT32_MAX;
    }
    if (ok)
    {
        in_offsets[blocks] = (uint32_t)in_run;
        out_offsets[blocks] = (uint32_t)out_run;
    }
    uint32_t* entry_locations = ok ? arena_push_array(scratch, in_run + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* exit_locations = ok ? arena_push_array(scratch, out_run + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* phi_locations = ok ? arena_push_array(scratch, (uint64_t)input->phi_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* exit_at = ok ? arena_push_array(scratch, (uint64_t)dense_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* exit_stamp = ok ? arena_push_array(scratch, (uint64_t)dense_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    ok = ok && entry_locations != 0 && exit_locations != 0 && phi_locations != 0 && exit_at != 0 && exit_stamp != 0;
    if (ok)
    {
        memset(exit_stamp, 0, ((uint64_t)dense_count + 1) * sizeof(uint32_t));
        for (uint32_t v = 0; v < values; v += 1)
        {
            cursors[v] = result->value_interval_offsets[v];
            lasts[v] = BUSTER_TIER1_CFG_NONE;
        }
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            const uint32_t entry = input->block_starts[b];
            const uint32_t exit = input->block_starts[b + 1] - 1;
            uint32_t rank = 0;
            for (uint64_t w = 0; w < words; w += 1)
            {
                uint64_t bits = liveness->in[(uint64_t)b * words + w];
                while (bits != 0)
                {
                    const uint32_t v = dense_values[(uint32_t)(w * 64 + (uint64_t)__builtin_ctzll(bits))];
                    bits &= bits - 1;
                    entry_locations[in_offsets[b] + rank] = tier1_cfg_cursor_entry(result, cursors, lasts, v, entry);
                    rank += 1;
                }
            }
            for (uint32_t k = input->phi_offsets[b]; k < input->phi_offsets[b + 1]; k += 1)
            {
                phi_locations[k] = tier1_cfg_cursor_entry(result, cursors, lasts, input->phi_values[k], entry);
            }
            rank = 0;
            for (uint64_t w = 0; w < words; w += 1)
            {
                uint64_t bits = liveness->out[(uint64_t)b * words + w];
                while (bits != 0)
                {
                    const uint32_t v = dense_values[(uint32_t)(w * 64 + (uint64_t)__builtin_ctzll(bits))];
                    bits &= bits - 1;
                    exit_locations[out_offsets[b] + rank] = tier1_cfg_cursor_where(result, cursors, lasts, v, exit);
                    rank += 1;
                }
            }
        }
        bounds.in_offsets = in_offsets;
        bounds.out_offsets = out_offsets;
        bounds.entry_locations = entry_locations;
        bounds.exit_locations = exit_locations;
        bounds.phi_locations = phi_locations;
        bounds.exit_at = exit_at;
        bounds.exit_stamp = exit_stamp;
        bounds.ready = true;
    }
    else
    {
        arena_pop_to(scratch, mark);
        bounds = (Tier1CfgBoundaries){ 0 };
    }
    return bounds;
}

// What tier1_cfg_prepare leaves for tier1_cfg_assign and tier1_cfg_finish (#2538): the intervals, in the output arena
// as a result's are, and on the scratch arena the working state the two read, which tier1_cfg_release gives back. The
// intervals don't depend on the registers a value may take where the input has no fixed range, so one preparation serves
// every problem over the same values that tier1_cfg_column describes. The arenas' lengths at the preparation's start,
// which a failure of the three restores.
typedef struct Tier1CfgPrepared Tier1CfgPrepared;
struct Tier1CfgPrepared
{
    // The intervals and each value's run of them, the position column and liveness's counts; intervals.allowed_registers
    // is the input's own problem's column, the one the interval builder wrote.
    Tier1CfgAllocation candidate;
    Dataflow liveness;
    // The values used outside their definition's block, by dense id, and the inverse.
    uint32_t* dense;
    uint32_t* dense_values;
    // Each block's entry state's size, for the edges' moves.
    uint32_t* entering;
    // The splits: each piece of a range after the first in its block, by the position it starts at and its value.
    uint32_t* split_positions;
    uint32_t* split_values;
    uint32_t dense_count;
    uint32_t splits;
    uint64_t output_saved;
    uint64_t scratch_saved;
    bool vector;
};

// Validation, homes and dense values, hints, liveness, block lists and the intervals (1. and 2. above), for `input`'s
// problem. Failure restores both arenas.
static Tier1CfgError tier1_cfg_prepare(const Tier1CfgInput* input, Arena* output, Arena* scratch, bool vector, Tier1CfgPrepared* prepared)
{
    Tier1CfgError error = TIER1_CFG_INVALID;
    const uint64_t output_saved = output != 0 ? output->length : 0;
    const uint64_t scratch_saved = scratch != 0 ? scratch->length : 0;
    Tier1CfgAllocation candidate = { 0 };
    const bool arguments = input != 0 && output != 0 && scratch != 0 && output != scratch && prepared != 0;
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
        const uint32_t* blocks_of = 0;
        if (ready && input->position_count <= BUSTER_TIER1_CFG_POSITION_BLOCKS)
        {
            uint32_t* filled = arena_push_array(output, input->position_count, sizeof(uint32_t), _Alignof(uint32_t));
            if (filled != 0)
            {
                for (uint32_t b = 0; b < blocks; b += 1)
                {
                    const uint32_t from = input->block_starts[b];
                    const uint32_t to = input->block_starts[b + 1];
                    for (uint32_t q = from; q < to; q += 1)
                    {
                        filled[q] = b;
                    }
                }
                candidate.position_blocks = filled;
                blocks_of = filled;
            }
        }
        uint32_t dense_count = 0;
        for (uint32_t v = 0; ready && v < values; v += 1)
        {
            home[v] = blocks_of != 0 ? blocks_of[input->definitions[v]] : tier1_cfg_block_of(input, input->definitions[v]);
            dense[v] = BUSTER_TIER1_CFG_NONE;
        }
        for (uint32_t o = 0; ready && o < input->phi_operand_offsets[input->phi_count]; o += 1)
        {
            const uint32_t operand = input->phi_operands[o];
            if (operand != BUSTER_TIER1_CFG_NONE)
            {
                dense[operand] = 0;
            }
        }
        for (uint32_t v = 0; ready && v < values; v += 1)
        {
            for (uint32_t u = input->use_offsets[v]; dense[v] == BUSTER_TIER1_CFG_NONE && u < input->use_offsets[v + 1]; u += 1)
            {
                const uint32_t block = blocks_of != 0 ? blocks_of[input->use_positions[u]] : tier1_cfg_block_of(input, input->use_positions[u]);
                dense[v] = block != home[v] ? 0 : BUSTER_TIER1_CFG_NONE;
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
        // Each block's anchor and chosen predecessor (#2553, tier1_cfg_anchors).
        uint32_t* anchors = ready ? arena_push_array(scratch, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        uint32_t* chosen = ready ? arena_push_array(scratch, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        ready = ready && anchors != 0 && chosen != 0 && tier1_cfg_anchors(input, output, scratch, anchors, chosen);
        // Each phi's hint: its operand from its block's chosen predecessor, or else from its first predecessor.
        uint32_t* phi_hints = ready ? arena_push_array(scratch, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        ready = ready && phi_hints != 0;
        if (ready)
        {
            memset(phi_hints, 0xff, ((uint64_t)values + 1) * sizeof(uint32_t));
        }
        for (uint32_t b = 0; ready && b < blocks; b += 1)
        {
            const uint32_t nearest = chosen[b] != BUSTER_TIER1_CFG_NONE ? chosen[b] : 0;
            const uint32_t predecessors = input->predecessor_offsets[b + 1] - input->predecessor_offsets[b];
            for (uint32_t k = input->phi_offsets[b]; k < input->phi_offsets[b + 1]; k += 1)
            {
                // An undefined operand hints nothing: then the first defined one, if any.
                uint32_t hint = input->phi_operands[input->phi_operand_offsets[k] + nearest];
                for (uint32_t j = 0; hint == BUSTER_TIER1_CFG_NONE && j < predecessors; j += 1)
                {
                    hint = input->phi_operands[input->phi_operand_offsets[k] + j];
                }
                phi_hints[input->phi_values[k]] = hint;
            }
        }
        // And a value a phi takes along an edge, hinted to the phi where nothing else hints it: along a back edge, the
        // phi has its register by then, from its definition, or (a loop's header laid out after its body, as a
        // dispatch loop's join is) from its ranges in the body's blocks before. Where the operand takes it, the edge
        // moves nothing for it; where the phi has no register yet, the hint goes on along the phis
        // (tier1_cfg_operand_targets, #2553), for which this pass notes each value's first phi that takes it (`ups`)
        // and whether it is a phi.
        uint32_t* ups = ready ? arena_push_array(scratch, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        uint8_t* phis = ready ? arena_push(scratch, (uint64_t)values + 1, 1) : 0;
        ready = ready && ups != 0 && phis != 0;
        if (ready)
        {
            memset(ups, 0xff, ((uint64_t)values + 1) * sizeof(uint32_t));
            memset(phis, 0, (uint64_t)values + 1);
        }
        for (uint32_t b = 0; ready && b < blocks; b += 1)
        {
            for (uint32_t k = input->phi_offsets[b]; k < input->phi_offsets[b + 1]; k += 1)
            {
                const uint32_t phi = input->phi_values[k];
                phis[phi] = 1;
                for (uint32_t o = input->phi_operand_offsets[k]; o < input->phi_operand_offsets[k + 1]; o += 1)
                {
                    const uint32_t operand = input->phi_operands[o];
                    if (operand != BUSTER_TIER1_CFG_NONE)
                    {
                        phi_hints[operand] = phi_hints[operand] == BUSTER_TIER1_CFG_NONE ? phi : phi_hints[operand];
                        ups[operand] = ups[operand] == BUSTER_TIER1_CFG_NONE && operand != phi ? phi : ups[operand];
                    }
                }
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
                    const uint32_t block = blocks_of != 0 ? blocks_of[input->use_positions[u]] : tier1_cfg_block_of(input, input->use_positions[u]);
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
                        const uint32_t operand = input->phi_operands[input->phi_operand_offsets[k] + j];
                        if (operand != BUSTER_TIER1_CFG_NONE)
                        {
                            tier1_cfg_set(passed + (uint64_t)predecessor * words, dense[operand]);
                        }
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
        // Each block's first successor in layout; and, for a block no earlier block reaches (a dispatch loop's handler,
        // its join laid out after them), the first of its predecessors' successors, a sibling whose entry it takes.
        // The join, in turn, takes for its phis their places at its first successor's entry, which comes before it.
        uint32_t* firsts = ready ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        uint32_t* siblings = ready ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        // Per block, the interval the value being built has at its entry, where entry_stamps says it is that value's; and the
        // one it has at its exit, where it is live out of the block (#2553).
        uint32_t* entry_pieces = ready ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        uint32_t* entry_stamps = ready ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        uint32_t* exit_pieces = ready ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        uint32_t* exit_stamps = ready ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
        ready = ready && firsts != 0 && siblings != 0 && entry_pieces != 0 && entry_stamps != 0 && exit_pieces != 0 && exit_stamps != 0;
        for (uint32_t b = 0; ready && b < blocks; b += 1)
        {
            uint32_t first = BUSTER_TIER1_CFG_NONE;
            for (uint32_t s = input->successor_offsets[b]; s < input->successor_offsets[b + 1]; s += 1)
            {
                first = input->successors[s] < first ? input->successors[s] : first;
            }
            firsts[b] = first;
        }
        for (uint32_t b = 0; ready && b < blocks; b += 1)
        {
            bool reached = false;
            uint32_t sibling = BUSTER_TIER1_CFG_NONE;
            for (uint32_t p = input->predecessor_offsets[b]; p < input->predecessor_offsets[b + 1]; p += 1)
            {
                const uint32_t predecessor = input->predecessors[p];
                reached |= predecessor < b;
                sibling = firsts[predecessor] < b && firsts[predecessor] < sibling ? firsts[predecessor] : sibling;
            }
            siblings[b] = reached ? BUSTER_TIER1_CFG_NONE : sibling;
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
                pieces.hint_intervals = ready ? arena_push_array(output, interval_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
                use_offsets = ready ? arena_push_array(output, interval_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
                pieces.split_positions = ready ? arena_push_array(scratch, split_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
                pieces.split_values = ready ? arena_push_array(scratch, split_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
                ready = ready && pieces.starts != 0 && pieces.ends != 0 && pieces.values != 0 && pieces.allowed != 0 && pieces.classes != 0 && pieces.remat != 0 && pieces.hints != 0 && pieces.hint_intervals != 0 && use_offsets != 0 && pieces.split_positions != 0 && pieces.split_values != 0;
                pieces.fill = ready;
            }
            if (ready)
            {
                memset(entry_stamps, 0, (uint64_t)blocks * sizeof(uint32_t));
                memset(exit_stamps, 0, (uint64_t)blocks * sizeof(uint32_t));
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
                                const bool entry = piece == 0 && from == input->block_starts[b];
                                if (piece == 0 && !defined_here && merges[b] && pieces.count > first)
                                {
                                    if (pieces.fill)
                                    {
                                        pieces.ends[pieces.count - 1] = to;
                                    }
                                }
                                else
                                {
                                    // A block laid out after its first successor (a dispatch loop's join) takes that
                                    // successor's convention for the values the two share, as its phis do (#2553).
                                    const uint32_t joined = firsts[b] < b ? firsts[b] : BUSTER_TIER1_CFG_NONE;
                                    const uint32_t model = !entry ? BUSTER_TIER1_CFG_NONE : !defined_here && siblings[b] != BUSTER_TIER1_CFG_NONE ? siblings[b] : joined;
                                    const bool modeled = model != BUSTER_TIER1_CFG_NONE && entry_stamps[model] == v + 1 && (!pieces.fill || pieces.starts[entry_pieces[model]] < from);
                                    // Else a value the block takes from before it, its place at the anchor's exit (#2553): a
                                    // phi's, its operand's at a predecessor's, comes once every value has its intervals.
                                    const uint32_t anchor = entry && !defined_here ? anchors[b] : BUSTER_TIER1_CFG_NONE;
                                    const bool anchored = !modeled && anchor != BUSTER_TIER1_CFG_NONE && exit_stamps[anchor] == v + 1;
                                    const uint32_t earlier = modeled ? entry_pieces[model] : anchored ? exit_pieces[anchor] | BUSTER_TIER1_ALLOCATE_HINT_EXIT : BUSTER_TIER1_CFG_NONE;
                                    tier1_cfg_piece(&pieces, input, v, from, to, piece == 0 && defined_here ? phi_hints[v] : v, earlier, piece != 0);
                                }
                                if (entry)
                                {
                                    entry_stamps[b] = v + 1;
                                    entry_pieces[b] = (uint32_t)pieces.count - 1;
                                }
                                piece += 1;
                            }
                        }
                        cursor = blocked_start < end ? blocked_end : end;
                    }
                    if (live_out && pieces.count > first)
                    {
                        exit_stamps[b] = v + 1;
                        exit_pieces[b] = (uint32_t)pieces.count - 1;
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
                tier1_cfg_phi_exits(input, chosen, candidate.value_interval_offsets, &pieces);
                tier1_cfg_operand_targets(input, phi_hints, ups, phis, candidate.value_interval_offsets, &pieces);
                candidate.block_starts = input->block_starts;
                candidate.block_count = input->block_count;
                candidate.intervals = (Tier1AllocationInput){
                    .starts = pieces.starts,
                    .ends = pieces.ends,
                    .values = pieces.values,
                    .allowed_registers = pieces.allowed,
                    .classes = pieces.classes,
                    .hint_values = pieces.hints,
                    .hint_intervals = pieces.hint_intervals,
                    .rematerializable = input->rematerializable != 0 ? pieces.remat : 0,
                    .use_offsets = use_offsets,
                    .use_positions = input->use_positions,
                    .use_required = input->use_required,
                    .clobbers = input->clobbers,
                    .clobber_count = input->clobber_count,
                    .clobber_registers = {
                        input->clobber_registers[BUSTER_TIER1_ALLOCATE_GPR],
                        input->clobber_registers[BUSTER_TIER1_ALLOCATE_VECTOR],
                        input->clobber_registers[BUSTER_TIER1_ALLOCATE_MASK],
                    },
                    .use_count = input->use_count,
                    .count = count,
                    .value_count = values,
                    .position_count = input->position_count,
                    .registers = {
                        input->registers[BUSTER_TIER1_ALLOCATE_GPR],
                        input->registers[BUSTER_TIER1_ALLOCATE_VECTOR],
                        input->registers[BUSTER_TIER1_ALLOCATE_MASK],
                    },
                    .fixed = input->fixed,
                };
            }
        }
        if (ready)
        {
            *prepared = (Tier1CfgPrepared){
                .candidate = candidate,
                .liveness = liveness,
                .dense = dense,
                .dense_values = dense_values,
                .entering = entering,
                .split_positions = pieces.split_positions,
                .split_values = pieces.split_values,
                .dense_count = dense_count,
                .splits = (uint32_t)pieces.splits,
                .output_saved = output_saved,
                .scratch_saved = scratch_saved,
                .vector = vector,
            };
            error = TIER1_CFG_OK;
        }
    }
    if (error != TIER1_CFG_OK)
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

// A problem's registers in the prepared intervals' allocator input: its classes' registers and its clobbers, which
// differ between problems over the same intervals (the CFG consumer's preferred problem has clobbers, #2574).
static void tier1_cfg_problem_registers(const Tier1CfgInput* input, Tier1AllocationInput* intervals)
{
    for (uint32_t c = 0; c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
    {
        intervals->registers[c] = input->registers[c];
        intervals->clobber_registers[c] = input->clobber_registers[c];
    }
    intervals->clobbers = input->clobbers;
    intervals->clobber_count = input->clobber_count;
}

// One assignment (3. above) over the prepared intervals, for `input`'s registers and `column`, the registers each
// interval may take: null for the prepared column, the input the preparation was given; else tier1_cfg_column's for
// another problem over the same values. Failure leaves the result unchanged; tier1_allocate restores the output arena.
static Tier1CfgError tier1_cfg_assign(const Tier1CfgInput* input, const Tier1CfgPrepared* prepared, const uint32_t* column, Arena* output, Tier1CfgAllocation* result)
{
    Tier1CfgAllocation candidate = prepared->candidate;
    candidate.intervals.allowed_registers = column != 0 ? column : candidate.intervals.allowed_registers;
    tier1_cfg_problem_registers(input, &candidate.intervals);
    const Tier1AllocationError allocated = BUSTER_VECTOR_PATH(prepared->vector) ? tier1_allocate(&candidate.intervals, output, &candidate.allocation) : tier1_allocate_scalar(&candidate.intervals, output, &candidate.allocation);
    const Tier1CfgError error = allocated == TIER1_ALLOCATION_OK ? TIER1_CFG_OK : allocated == TIER1_ALLOCATION_CONSTRAINT ? TIER1_CFG_CONSTRAINT : allocated == TIER1_ALLOCATION_INVALID ? TIER1_CFG_INVALID : TIER1_CFG_CAPACITY;
    if (error == TIER1_CFG_OK)
    {
        *result = candidate;
    }
    return error;
}

// Lookup columns, slot sharing, the splits' and the edges' moves (4. and 5. above) for an assignment of the prepared
// intervals, `result` as tier1_cfg_assign left it, with `input` its problem. The scratch arena goes back to where this
// found it, the preparation's state kept; failure restores the output arena too and leaves the result unchanged.
static Tier1CfgError tier1_cfg_finish(const Tier1CfgInput* input, const Tier1CfgPrepared* prepared, Arena* output, Arena* scratch, Tier1CfgAllocation* result)
{
    Tier1CfgError error = TIER1_CFG_CAPACITY;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    Tier1CfgAllocation candidate = *result;
    const uint32_t blocks = input->block_count;
    const Dataflow liveness = prepared->liveness;
    const uint64_t words = liveness.row_words;
    const uint32_t* dense = prepared->dense;
    const uint32_t* dense_values = prepared->dense_values;
    const uint32_t dense_count = prepared->dense_count;
    const uint32_t* entering = prepared->entering;
    tier1_cfg_install_lookup(input, output, scratch, &candidate);
    bool ready = input->slot_widths == 0 || candidate.allocation.spill_count < 2 || tier1_cfg_share_slots(input, &candidate, scratch);
    // Splits by position, counted then filled: the values whose ranges are cut there, and the distinct positions.
    const uint32_t splits = ready ? prepared->splits : 0;
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
            split_offsets[prepared->split_positions[k] + 1] += 1;
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
            split_values[split_offsets[prepared->split_positions[k]]] = prepared->split_values[k];
            split_offsets[prepared->split_positions[k]] += 1;
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
    uint32_t* spill_ends = ready && spills != 0 ? arena_push_array(scratch, (uint64_t)candidate.allocation.fragment_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    ready = ready && (spills == 0 || spill_ends != 0);
    if (ready && spills != 0)
    {
        tier1_cfg_spill_prefix(&candidate, spill_ends);
    }
    Tier1CfgEdge edge = { .spill_ends = spill_ends };
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
    Tier1CfgBoundaries bounds = { 0 };
    if (ready)
    {
        bounds = tier1_cfg_boundaries(input, &candidate, &liveness, dense_values, dense_count, scratch);
    }
    uint32_t staged_count = 0;
    for (uint32_t b = 0; ready && b < blocks; b += 1)
    {
        const uint32_t exit = input->block_starts[b + 1] - 1;
        edge.block_start = input->block_starts[b];
        if (bounds.ready)
        {
            uint32_t rank = 0;
            for (uint64_t w = 0; w < words; w += 1)
            {
                uint64_t bits = liveness.out[(uint64_t)b * words + w];
                while (bits != 0)
                {
                    const uint32_t i = (uint32_t)(w * 64 + (uint64_t)__builtin_ctzll(bits));
                    bits &= bits - 1;
                    bounds.exit_at[i] = bounds.exit_locations[bounds.out_offsets[b] + rank];
                    bounds.exit_stamp[i] = b + 1;
                    rank += 1;
                }
            }
        }
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
            uint32_t rank = 0;
            for (uint64_t w = 0; ready && w < words; w += 1)
            {
                uint64_t bits = liveness.in[(uint64_t)target * words + w];
                while (ready && bits != 0)
                {
                    const uint32_t i = (uint32_t)(w * 64 + (uint64_t)__builtin_ctzll(bits));
                    const uint32_t v = dense_values[i];
                    bits &= bits - 1;
                    if (bounds.ready)
                    {
                        const uint32_t from = bounds.exit_stamp[i] == b + 1 ? bounds.exit_at[i] : BUSTER_TIER1_CFG_NONE;
                        const uint32_t to = bounds.entry_locations[bounds.in_offsets[target] + rank];
                        ready = tier1_cfg_edge_known(input, &candidate, &edge, v, v, from, to, exit);
                    }
                    else
                    {
                        ready = tier1_cfg_edge_value(input, &candidate, &edge, v, v, exit, entry);
                    }
                    rank += 1;
                }
            }
            uint32_t j = 0;
            while (input->predecessors[input->predecessor_offsets[target] + j] != b)
            {
                j += 1;
            }
            for (uint32_t k = input->phi_offsets[target]; ready && k < input->phi_offsets[target + 1]; k += 1)
            {
                const uint32_t operand = input->phi_operands[input->phi_operand_offsets[k] + j];
                if (operand == BUSTER_TIER1_CFG_NONE)
                {
                    ready = true;
                }
                else if (bounds.ready)
                {
                    const uint32_t id = dense[operand];
                    const uint32_t from = id != BUSTER_TIER1_CFG_NONE && bounds.exit_stamp[id] == b + 1 ? bounds.exit_at[id] : BUSTER_TIER1_CFG_NONE;
                    ready = tier1_cfg_edge_known(input, &candidate, &edge, input->phi_values[k], operand, from, bounds.phi_locations[k], exit);
                }
                else
                {
                    ready = tier1_cfg_edge_value(input, &candidate, &edge, input->phi_values[k], operand, exit, entry);
                }
            }
            ready = ready && tier1_cfg_edge_stage(input, spills, &edge, scratch, sources, destinations, staged, bound + 2, &staged_count, &candidate.temp_slots);
            error = ready ? error : TIER1_CFG_INVALID;
            if (ready)
            {
                const bool moves = staged_count != candidate.edge_move_offsets[e] || (input->edge_copies != 0 && input->edge_copies[e] != 0);
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
    // Slot-current elision is for CFG edges only; keep the existing split-move contract unchanged.
    edge.spill_ends = 0;
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
    if (error == TIER1_CFG_OK)
    {
        candidate.retained_bytes = output->length - prepared->output_saved;
        candidate.scratch_bytes = scratch->length - prepared->scratch_saved;
        arena_pop_to(scratch, scratch_mark);
        *result = candidate;
    }
    else
    {
        arena_pop_to(output, output_mark);
        arena_pop_to(scratch, scratch_mark);
    }
    return error;
}

// The scratch arena back to where tier1_cfg_prepare found it, once nothing reads the preparation's state.
static void tier1_cfg_release(const Tier1CfgPrepared* prepared, Arena* scratch)
{
    arena_pop_to(scratch, prepared->scratch_saved);
}

// The three in a row, for the input's own problem.
static Tier1CfgError tier1_cfg_allocate_impl(const Tier1CfgInput* input, Arena* output, Arena* scratch, bool vector, Tier1CfgAllocation* result)
{
    Tier1CfgPrepared prepared = { 0 };
    Tier1CfgAllocation candidate = { 0 };
    Tier1CfgError error = result != 0 ? tier1_cfg_prepare(input, output, scratch, vector, &prepared) : TIER1_CFG_INVALID;
    const bool held = error == TIER1_CFG_OK;
    error = held ? tier1_cfg_assign(input, &prepared, 0, output, &candidate) : error;
    error = error == TIER1_CFG_OK ? tier1_cfg_finish(input, &prepared, output, scratch, &candidate) : error;
    if (held && error == TIER1_CFG_OK)
    {
        tier1_cfg_release(&prepared, scratch);
        *result = candidate;
    }
    else if (held)
    {
        arena_pop_to(output, prepared.output_saved);
        arena_pop_to(scratch, prepared.scratch_saved);
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

// Whether each value's registers lie in its class's, and its definition's in its allowed ones: tier1_cfg_input_valid's
// two conditions on a value's registers, 16 values a step, the class's registers by an in-register lookup of the three
// and each condition an andnot. The classes were validated with the prepared input.
static bool tier1_cfg_sets_valid_vector(const Tier1CfgInput* input)
{
    const __m512i table = _mm512_maskz_loadu_epi32((__mmask16)((1u << BUSTER_TIER1_ALLOCATE_CLASSES) - 1), input->registers);
    const uint32_t values = input->value_count;
    __mmask16 bad = 0;
    for (uint32_t v = 0; v < values; v += 16)
    {
        const uint32_t left = values - v;
        const __mmask16 lanes = left >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
        const __m512i sets = _mm512_permutexvar_epi32(_mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, input->classes + v)), table);
        const __m512i allowed = _mm512_maskz_loadu_epi32(lanes, input->allowed_registers + v);
        const __m512i outside = _mm512_andnot_si512(sets, allowed);
        const __m512i defined = input->definition_registers != 0 ? _mm512_maskz_loadu_epi32(lanes, input->definition_registers + v) : _mm512_setzero_si512();
        const __m512i unallowed = _mm512_andnot_si512(allowed, defined);
        bad |= _mm512_mask_test_epi32_mask(lanes, outside, outside) | _mm512_mask_test_epi32_mask(lanes, unallowed, unallowed);
    }
    return bad == 0;
}

// The scalar twin of tier1_cfg_sets_valid_vector: tier1_cfg_input_valid's two compares per value.
static bool tier1_cfg_sets_valid_scalar(const Tier1CfgInput* input)
{
    bool valid = true;
    for (uint32_t v = 0; valid && v < input->value_count; v += 1)
    {
        valid = (input->allowed_registers[v] & ~input->registers[input->classes[v]]) == 0 && (input->definition_registers == 0 || (input->definition_registers[v] & ~input->allowed_registers[v]) == 0);
    }
    return valid;
}

// The registers each prepared interval may take in another problem over the same values (#2538): `input` is the
// prepared one but for its registers, allowed_registers and definition_registers. Each value's run of intervals takes its
// allowed set, and the interval that holds its definition its definition's registers where some are bound, as
// tier1_cfg_piece gives them; the column is on `output`. TIER1_CFG_INVALID where the problem breaks what
// tier1_cfg_input_valid checks of those three, or where the input has a fixed range, around which a value's intervals
// depend on its registers (2. above), so that no column of the prepared ones fits another problem.
static __attribute__((unused)) Tier1CfgError tier1_cfg_column(const Tier1CfgInput* input, const Tier1CfgPrepared* prepared, Arena* output, uint32_t** column)
{
    const Tier1CfgAllocation* intervals = &prepared->candidate;
    bool valid = input->fixed.count == 0 && (input->value_count == 0 || input->allowed_registers != 0);
    for (uint32_t c = 0; valid && c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
    {
        valid = input->scratch_registers[c] < 32 && ((input->registers[c] >> input->scratch_registers[c]) & 1) == 0;
    }
    valid = valid && (input->registers[BUSTER_TIER1_ALLOCATE_GPR] & 0xffff0000u) == 0 && input->scratch_registers[BUSTER_TIER1_ALLOCATE_GPR] < 16;
    valid = valid && (input->registers[BUSTER_TIER1_ALLOCATE_MASK] & ~BUSTER_TIER1_ALLOCATE_MASK_REGISTERS) == 0 && input->scratch_registers[BUSTER_TIER1_ALLOCATE_MASK] < 8;
    valid = valid && (BUSTER_VECTOR_PATH(prepared->vector) ? tier1_cfg_sets_valid_vector(input) : tier1_cfg_sets_valid_scalar(input));
    uint32_t* written = valid ? arena_push_array(output, (uint64_t)intervals->intervals.count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    for (uint32_t v = 0; written != 0 && v < input->value_count; v += 1)
    {
        const uint32_t allowed = input->allowed_registers[v];
        const uint32_t defined = input->definition_registers != 0 ? input->definition_registers[v] : 0;
        const uint32_t definition = input->definitions[v];
        for (uint32_t i = intervals->value_interval_offsets[v]; i < intervals->value_interval_offsets[v + 1]; i += 1)
        {
            written[i] = defined != 0 && intervals->intervals.starts[i] <= definition && definition < intervals->intervals.ends[i] ? defined : allowed;
        }
    }
    *column = written;
    return !valid ? TIER1_CFG_INVALID : written == 0 ? TIER1_CFG_CAPACITY : TIER1_CFG_OK;
}

// Whether a preferred problem's allocation replaces the plain one (tier1_emit_cfg.c's callee-saved preference, #1116):
// allocated, without a split, each class's temporary slot a frame row of its own.
static __attribute__((unused)) bool tier1_cfg_taken(Tier1CfgError error, const Tier1CfgAllocation* allocation)
{
    return error == TIER1_CFG_OK && allocation->split_count == 0 && (allocation->temp_slots & ~((1u << BUSTER_TIER1_ALLOCATE_CLASSES) - 1)) == 0;
}

// tier1_cfg_assign, and another problem's verdict inside the same call (#2538's step 3, tier1_allocate_verdict): `other`
// is the prepared input but for its registers, allowed and definition registers, `other_column` its column (null for the
// prepared one). `*taken` says whether the verdict was taken, `*constraint` what it is.
static Tier1CfgError tier1_cfg_assign_verdict(const Tier1CfgInput* input, const Tier1CfgPrepared* prepared, const uint32_t* column, const Tier1CfgInput* other, const uint32_t* other_column, Arena* output, Tier1CfgAllocation* result, bool* taken, bool* constraint)
{
    Tier1CfgAllocation candidate = prepared->candidate;
    Tier1AllocationVerdict verdict = {
        .allowed_registers = other_column != 0 ? other_column : candidate.intervals.allowed_registers,
    };
    candidate.intervals.allowed_registers = column != 0 ? column : candidate.intervals.allowed_registers;
    tier1_cfg_problem_registers(input, &candidate.intervals);
    for (uint32_t c = 0; c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
    {
        verdict.registers[c] = other->registers[c];
    }
    const Tier1AllocationError allocated = tier1_allocate_verdict(&candidate.intervals, output, prepared->vector, &verdict, &candidate.allocation);
    const Tier1CfgError error = allocated == TIER1_ALLOCATION_OK ? TIER1_CFG_OK : allocated == TIER1_ALLOCATION_CONSTRAINT ? TIER1_CFG_CONSTRAINT : allocated == TIER1_ALLOCATION_INVALID ? TIER1_CFG_INVALID : TIER1_CFG_CAPACITY;
    if (error == TIER1_CFG_OK)
    {
        *result = candidate;
    }
    *taken = verdict.taken;
    *constraint = verdict.constraint;
    return error;
}

// A preferred problem for the one-call order (#2538): its classes' registers, and each value's registers but those of its
// definition, which the order adds while the definition is bound (tier1_cfg_preferred). `verdict` takes the plain
// problem's verdict inside the preferred problem's assignment (step 3); without it the plain problem is assigned whole
// first (step 2), the form the tests compare it with.
typedef struct Tier1CfgPreference Tier1CfgPreference;
struct Tier1CfgPreference
{
    const uint32_t* sets;
    uint32_t registers[BUSTER_TIER1_ALLOCATE_CLASSES];
    // The preferred problem's clobbers (Tier1CfgInput.clobbers, #2574), or none.
    const uint32_t* clobbers;
    uint32_t clobber_count;
    uint32_t clobber_registers[BUSTER_TIER1_ALLOCATE_CLASSES];
    bool verdict;
};

// The preferred problem over `input`'s values: the preference's registers, and each value's set with its definition's
// registers, input's while bound, on `output`. False where the arena is short.
static __attribute__((unused)) bool tier1_cfg_preferred(const Tier1CfgInput* input, const Tier1CfgPreference* preference, Arena* output, Tier1CfgInput* preferred)
{
    uint32_t* allowed = arena_push_array(output, (uint64_t)input->value_count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    for (uint32_t v = 0; allowed != 0 && v < input->value_count; v += 1)
    {
        allowed[v] = preference->sets[v] | (input->definition_registers != 0 ? input->definition_registers[v] : 0);
    }
    *preferred = *input;
    preferred->allowed_registers = allowed;
    preferred->clobbers = preference->clobbers;
    preferred->clobber_count = preference->clobber_count;
    for (uint32_t c = 0; c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
    {
        preferred->registers[c] = preference->registers[c];
        preferred->clobber_registers[c] = preference->clobber_registers[c];
    }
    return allowed != 0;
}

// The one-call order's state between its halves (#2538): the intervals, prepared once, and the plain problem's
// assignment where one was made.
typedef struct Tier1CfgOrder Tier1CfgOrder;
struct Tier1CfgOrder
{
    Tier1CfgPrepared prepared;
    Tier1CfgAllocation plain;
};

// The one-call order's first half: the intervals (tier1_cfg_prepare), whose columns the preference reads in
// `order->prepared.candidate`. tier1_cfg_order_run or tier1_cfg_release ends what this holds.
static __attribute__((unused)) Tier1CfgError tier1_cfg_order_prepare(const Tier1CfgInput* input, Arena* output, Arena* scratch, bool vector, Tier1CfgOrder* order)
{
    *order = (Tier1CfgOrder){ 0 };
    return tier1_cfg_prepare(input, output, scratch, vector, &order->prepared);
}

// The plain problem assigned whole over the prepared intervals: with `freed`, its definitions already free, over the free
// problem's column. Where a definition bound to its registers (a parameter's incoming one) leaves a use no register,
// every definition is freed (`input->definition_registers` made null) and the problem assigned again.
static Tier1CfgError tier1_cfg_order_plain(Tier1CfgInput* input, Arena* output, Tier1CfgOrder* order, bool freed)
{
    uint32_t* column = 0;
    Tier1CfgError error = freed ? tier1_cfg_column(input, &order->prepared, output, &column) : TIER1_CFG_OK;
    error = error == TIER1_CFG_OK ? tier1_cfg_assign(input, &order->prepared, column, output, &order->plain) : error;
    if (error == TIER1_CFG_CONSTRAINT && !freed)
    {
        input->definition_registers = 0;
        error = tier1_cfg_column(input, &order->prepared, output, &column);
        error = error == TIER1_CFG_OK ? tier1_cfg_assign(input, &order->prepared, column, output, &order->plain) : error;
    }
    return error;
}

// The preferred problem assigned, with the plain problem's verdict inside its call (step 3): its column, and with
// `freed` the free plain problem's. Returns the preferred assignment's outcome; `*verdict` says whether the plain
// problem's verdict was taken, `*constraint` what it is.
static Tier1CfgError tier1_cfg_order_verdict(const Tier1CfgInput* input, const Tier1CfgPreference* preference, Arena* output, Tier1CfgOrder* order, bool freed, Tier1CfgInput* preferred, Tier1CfgAllocation* second, bool* verdict, bool* constraint)
{
    uint32_t* column = 0;
    uint32_t* plain_column = 0;
    *verdict = false;
    *constraint = false;
    Tier1CfgError error = tier1_cfg_preferred(input, preference, output, preferred) ? TIER1_CFG_OK : TIER1_CFG_CAPACITY;
    error = error == TIER1_CFG_OK ? tier1_cfg_column(preferred, &order->prepared, output, &column) : error;
    error = error == TIER1_CFG_OK && freed ? tier1_cfg_column(input, &order->prepared, output, &plain_column) : error;
    error = error == TIER1_CFG_OK ? tier1_cfg_assign_verdict(preferred, &order->prepared, column, input, plain_column, output, second, verdict, constraint) : error;
    return error;
}

// The one-call order's second half. With a preference that takes the verdict (step 3): the preferred problem assigned
// with the plain one's verdict inside its call; where that is a constraint, every definition freed and the preferred
// problem assigned again with the free plain one's verdict, a second constraint the plain problem's outcome. Without, or
// where no verdict was taken (step 2): the plain problem assigned whole first (tier1_cfg_order_plain), then the preferred
// one over the definitions the plain one left. The preferred allocation is finished and stands where tier1_cfg_taken takes
// it, with `*taken` set and its allowed sets in `*preferred_allowed`; else the plain problem, assigned whole now if only
// its verdict was taken, is finished. `*preferred_error` is the preferred problem's outcome. Returns the outcome of the
// allocation that stands, and gives back the scratch arena the order held; failure restores both arenas.
static __attribute__((unused)) Tier1CfgError tier1_cfg_order_run(Tier1CfgInput* input, const Tier1CfgPreference* preference, Arena* output, Arena* scratch, Tier1CfgOrder* order, Tier1CfgAllocation* result, const uint32_t** preferred_allowed, Tier1CfgError* preferred_error, bool* taken)
{
    Tier1CfgError error = TIER1_CFG_OK;
    Tier1CfgError second_error = TIER1_CFG_OK;
    Tier1CfgAllocation second = { 0 };
    Tier1CfgInput preferred = *input;
    // Whether the plain problem's verdict is known, its definitions were freed, and it was assigned whole.
    bool known = false;
    bool freed = false;
    bool assigned = false;
    if (preference != 0 && preference->verdict)
    {
        bool verdict = false;
        bool constraint = false;
        second_error = tier1_cfg_order_verdict(input, preference, output, order, false, &preferred, &second, &verdict, &constraint);
        known = verdict && !constraint;
        if (verdict && constraint)
        {
            input->definition_registers = 0;
            freed = true;
            second_error = tier1_cfg_order_verdict(input, preference, output, order, true, &preferred, &second, &verdict, &constraint);
            known = verdict;
            error = verdict && constraint ? TIER1_CFG_CONSTRAINT : TIER1_CFG_OK;
        }
    }
    if (!known)
    {
        error = tier1_cfg_order_plain(input, output, order, freed);
        assigned = true;
        uint32_t* column = 0;
        const bool prefer = error == TIER1_CFG_OK && preference != 0;
        second_error = prefer ? (tier1_cfg_preferred(input, preference, output, &preferred) ? TIER1_CFG_OK : TIER1_CFG_CAPACITY) : TIER1_CFG_OK;
        second_error = prefer && second_error == TIER1_CFG_OK ? tier1_cfg_column(&preferred, &order->prepared, output, &column) : second_error;
        second_error = prefer && second_error == TIER1_CFG_OK ? tier1_cfg_assign(&preferred, &order->prepared, column, output, &second) : second_error;
    }
    const bool prefer = error == TIER1_CFG_OK && preference != 0;
    second_error = prefer && second_error == TIER1_CFG_OK ? tier1_cfg_finish(&preferred, &order->prepared, output, scratch, &second) : second_error;
    const bool replaced = prefer && tier1_cfg_taken(second_error, &second);
    if (replaced)
    {
        *result = second;
        *preferred_allowed = preferred.allowed_registers;
    }
    else if (error == TIER1_CFG_OK)
    {
        // The plain problem stands: assigned whole now where only its verdict was taken.
        error = assigned ? TIER1_CFG_OK : tier1_cfg_order_plain(input, output, order, freed);
        error = error == TIER1_CFG_OK ? tier1_cfg_finish(input, &order->prepared, output, scratch, &order->plain) : error;
        if (error == TIER1_CFG_OK)
        {
            *result = order->plain;
        }
    }
    if (error != TIER1_CFG_OK)
    {
        arena_pop_to(output, order->prepared.output_saved);
    }
    tier1_cfg_release(&order->prepared, scratch);
    *preferred_error = preference != 0 ? second_error : TIER1_CFG_OK;
    *taken = replaced;
    return error;
}

static bool tier1_cfg_same_column(const void* left, const void* right, uint64_t bytes)
{
    return (left == 0) == (right == 0) && (left == 0 || bytes == 0 || memcmp(left, right, bytes) == 0);
}

// 0 where two allocations of `input`'s problem are the same, field by field, else the first group of fields that
// differs, from 1: the allocator's input and its counts; its interval columns; the hints and recomputations; the use
// slices; the assignment's counts; its fragments; its per-value and slot columns; the values' runs of intervals; the
// edges' placements; the moves; the splits; the liveness counts and temporary slots; and the lookup columns. Memory
// counts aren't compared: they depend on where each allocation's arenas stood.
static __attribute__((unused)) uint32_t tier1_cfg_differs(const Tier1CfgInput* input, const Tier1CfgAllocation* left, const Tier1CfgAllocation* right)
{
    const Tier1AllocationInput* a = &left->intervals;
    const Tier1AllocationInput* b = &right->intervals;
    const Tier1Allocation* x = &left->allocation;
    const Tier1Allocation* y = &right->allocation;
    const uint64_t count = a->count;
    const uint64_t fragments = x->fragment_count;
    const uint64_t values = input->value_count;
    const uint64_t edges = input->edge_count;
    uint32_t differs = 0;
    bool same = a->count == b->count && a->value_count == b->value_count && a->position_count == b->position_count && a->use_count == b->use_count && a->fixed.count == b->fixed.count &&
                a->clobber_count == b->clobber_count && tier1_cfg_same_column(a->clobbers, b->clobbers, (uint64_t)a->clobber_count * 4);
    for (uint32_t c = 0; same && c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
    {
        same = a->registers[c] == b->registers[c] && a->clobber_registers[c] == b->clobber_registers[c];
    }
    differs = same ? differs : 1;
    same = same && tier1_cfg_same_column(a->starts, b->starts, count * 4) && tier1_cfg_same_column(a->ends, b->ends, count * 4) && tier1_cfg_same_column(a->values, b->values, count * 4) &&
           tier1_cfg_same_column(a->allowed_registers, b->allowed_registers, count * 4) && tier1_cfg_same_column(a->classes, b->classes, count);
    differs = same || differs != 0 ? differs : 2;
    same = same && tier1_cfg_same_column(a->hint_values, b->hint_values, count * 4) && tier1_cfg_same_column(a->hint_intervals, b->hint_intervals, count * 4) &&
           tier1_cfg_same_column(a->rematerializable, b->rematerializable, count) && tier1_cfg_same_column(a->flags, b->flags, count) && tier1_cfg_same_column(a->hints, b->hints, count);
    differs = same || differs != 0 ? differs : 3;
    same = same && tier1_cfg_same_column(a->use_offsets, b->use_offsets, (count + 1) * 4) && a->use_positions == b->use_positions && a->use_required == b->use_required;
    differs = same || differs != 0 ? differs : 4;
    same = same && x->fragment_count == y->fragment_count && x->interval_count == y->interval_count && x->spill_count == y->spill_count && x->vector_intervals == y->vector_intervals &&
           x->scalar_intervals == y->scalar_intervals && x->splits == y->splits && memcmp(x->peak_pressure, y->peak_pressure, sizeof(x->peak_pressure)) == 0;
    differs = same || differs != 0 ? differs : 5;
    same = same && tier1_cfg_same_column(x->fragment_offsets, y->fragment_offsets, ((uint64_t)x->interval_count + 1) * 4) && tier1_cfg_same_column(x->starts, y->starts, fragments * 4) &&
           tier1_cfg_same_column(x->ends, y->ends, fragments * 4) && tier1_cfg_same_column(x->kinds, y->kinds, fragments) && tier1_cfg_same_column(x->registers, y->registers, fragments);
    differs = same || differs != 0 ? differs : 6;
    same = same && tier1_cfg_same_column(x->value_registers, y->value_registers, values) && tier1_cfg_same_column(x->spill_ids, y->spill_ids, values * 4) &&
           tier1_cfg_same_column(x->spill_values, y->spill_values, (uint64_t)x->spill_count * 4);
    differs = same || differs != 0 ? differs : 7;
    same = same && tier1_cfg_same_column(left->value_interval_offsets, right->value_interval_offsets, (values + 1) * 4);
    differs = same || differs != 0 ? differs : 8;
    same = same && tier1_cfg_same_column(left->edge_placements, right->edge_placements, edges) && tier1_cfg_same_column(left->edge_move_offsets, right->edge_move_offsets, (edges + 1) * 4);
    differs = same || differs != 0 ? differs : 9;
    same = same && left->move_count == right->move_count && tier1_cfg_same_column(left->move_sources, right->move_sources, (uint64_t)left->move_count * 4) &&
           tier1_cfg_same_column(left->move_destinations, right->move_destinations, (uint64_t)left->move_count * 4) && tier1_cfg_same_column(left->move_values, right->move_values, (uint64_t)left->move_count * 4);
    differs = same || differs != 0 ? differs : 10;
    same = same && left->split_count == right->split_count && left->split_edges == right->split_edges && tier1_cfg_same_column(left->split_positions, right->split_positions, (uint64_t)left->split_count * 4) &&
           tier1_cfg_same_column(left->split_move_offsets, right->split_move_offsets, ((uint64_t)left->split_count + 1) * 4);
    differs = same || differs != 0 ? differs : 11;
    same = same && left->cross_block_values == right->cross_block_values && left->temp_slots == right->temp_slots && left->liveness_iterations == right->liveness_iterations;
    differs = same || differs != 0 ? differs : 12;
    // The block column's rows: one per value with a base.
    uint64_t rows = 0;
    for (uint32_t v = 0; same && left->value_block_base != 0 && v < values; v += 1)
    {
        rows += left->value_block_base[v] != BUSTER_TIER1_CFG_NONE;
    }
    same = same && tier1_cfg_same_column(left->position_blocks, right->position_blocks, (uint64_t)input->position_count * 4) && tier1_cfg_same_column(left->value_block_base, right->value_block_base, values * 4) &&
           tier1_cfg_same_column(left->value_block_intervals, right->value_block_intervals, rows * input->block_count * 4);
    differs = same || differs != 0 ? differs : 13;
    return differs;
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
    return held != BUSTER_TIER1_CFG_NONE && held != value && held < result->intervals.value_count && tier1_cfg_location_oracle(result, held, position, &where, &kind) && where == location;
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

// The allocation's columns, as the certificate's shape requires them (tier1_certificate_cfg.c), before anything reads
// through them: each present where its count needs it, the interval and fragment maps' ends, the slots' count within
// the values and each slot's value in range. What the columns' entries mean is left to the checks that read them.
static bool tier1_cfg_verify_columns(const Tier1CfgInput* input, const Tier1CfgAllocation* result)
{
    const Tier1AllocationInput* intervals = &result->intervals;
    const Tier1Allocation* allocation = &result->allocation;
    const uint32_t values = input->value_count;
    bool valid = result->value_interval_offsets != 0 && (intervals->count == 0 || (intervals->values != 0 && intervals->classes != 0 && intervals->starts != 0 && intervals->ends != 0));
    valid = valid && allocation->interval_count == intervals->count && allocation->fragment_offsets != 0 && allocation->fragment_offsets[0] == 0 && allocation->fragment_offsets[intervals->count] == allocation->fragment_count;
    valid = valid && (allocation->fragment_count == 0 || (allocation->starts != 0 && allocation->ends != 0 && allocation->kinds != 0 && allocation->registers != 0));
    valid = valid && allocation->spill_count <= values && (allocation->spill_count == 0 || (allocation->spill_values != 0 && allocation->spill_ids != 0));
    for (uint32_t s = 0; valid && s < allocation->spill_count; s += 1)
    {
        valid = allocation->spill_values[s] < values;
    }
    valid = valid && result->edge_move_offsets != 0 && result->split_move_offsets != 0 && result->edge_placements != 0;
    valid = valid && (result->move_count == 0 || (result->move_sources != 0 && result->move_destinations != 0 && result->move_values != 0));
    valid = valid && (result->split_count == 0 || result->split_positions != 0);
    return valid;
}

static __attribute__((unused)) bool tier1_cfg_verify(const Tier1CfgInput* input, const Tier1CfgAllocation* result, Arena* scratch, Tier1CfgCheck* check)
{
    const uint64_t saved = scratch->length;
    *check = (Tier1CfgCheck){ 0 };
    bool valid = tier1_cfg_input_valid(input, scratch) || tier1_cfg_fail(check, "invalid input", 0, 0, 0);
    valid = valid && (tier1_cfg_verify_columns(input, result) || tier1_cfg_fail(check, "the allocation's columns", 0, 0, 0));
    const uint32_t blocks = input->block_count;
    const uint32_t values = input->value_count;
    const Tier1Allocation* allocation = &result->allocation;
    const Tier1AllocationInput* intervals = &result->intervals;
    // Every value's intervals: its own, in position order, without overlap, each with fragments from start to end.
    valid = valid && ((result->value_interval_offsets[0] == 0 && result->value_interval_offsets[values] == intervals->count && allocation->interval_count == intervals->count) || tier1_cfg_fail(check, "interval map", 0, 0, 0));
    for (uint32_t v = 0; valid && v < values; v += 1)
    {
        // Each offset bounded before it bounds a read: a malformed allocation is refused, never read past its columns.
        valid = (result->value_interval_offsets[v] <= result->value_interval_offsets[v + 1] && result->value_interval_offsets[v + 1] <= intervals->count) || tier1_cfg_fail(check, "interval map", 0, v, 0);
        for (uint32_t i = result->value_interval_offsets[v]; valid && i < result->value_interval_offsets[v + 1]; i += 1)
        {
            valid = (intervals->values[i] == v && intervals->classes[i] == input->classes[v] && intervals->starts[i] < intervals->ends[i] && intervals->ends[i] <= input->position_count && (i == result->value_interval_offsets[v] || intervals->ends[i - 1] <= intervals->starts[i])) || tier1_cfg_fail(check, "an interval out of its value's order", 0, v, intervals->starts[i]);
            valid = valid && ((allocation->fragment_offsets[i] <= allocation->fragment_offsets[i + 1] && allocation->fragment_offsets[i + 1] <= allocation->fragment_count) || tier1_cfg_fail(check, "an interval's fragments past the allocation's", 0, v, intervals->starts[i]));
            uint32_t position = intervals->starts[i];
            for (uint32_t f = valid ? allocation->fragment_offsets[i] : 0; valid && f < allocation->fragment_offsets[i + 1]; f += 1)
            {
                const bool reg = allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER;
                valid = (allocation->starts[f] == position && allocation->ends[f] > position && allocation->kinds[f] <= BUSTER_TIER1_ALLOCATE_REMATERIALIZE) || tier1_cfg_fail(check, "fragments don't tile their interval", 0, v, position);
                valid = valid && ((!reg || (allocation->registers[f] < 32 && ((input->allowed_registers[v] >> allocation->registers[f]) & 1) != 0)) || tier1_cfg_fail(check, "a register outside the value's mask", 0, v, position));
                valid = valid && (reg || allocation->registers[f] == UINT8_MAX || tier1_cfg_fail(check, "a register on a fragment without one", 0, v, position));
                // The definition's fragment in one of its registers.
                const uint32_t defined = input->definition_registers != 0 ? input->definition_registers[v] : 0;
                const bool defines = allocation->starts[f] <= input->definitions[v] && input->definitions[v] < allocation->ends[f];
                valid = valid && ((defined == 0 || !defines || (reg && ((defined >> allocation->registers[f]) & 1) != 0)) || tier1_cfg_fail(check, "a definition outside its registers", 0, v, position));
                // A slot of the value's class: values of one class may share it (tier1_cfg_share_slots).
                valid = valid && ((allocation->kinds[f] != BUSTER_TIER1_ALLOCATE_SPILL || (allocation->spill_count != 0 && allocation->spill_ids[v] < allocation->spill_count && input->classes[allocation->spill_values[allocation->spill_ids[v]]] == input->classes[v])) || tier1_cfg_fail(check, "a spill without a slot", 0, v, position));
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
                        if (operand == BUSTER_TIER1_CFG_NONE)
                        {
                        }
                        else if (pass == 0)
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
        // Kinds: 0 transition (fragment f), 1 use (use u), 2 clobber (fixed range r), 3 definition (value v), 4 the
        // start of a piece of a range cut inside its block (interval i).
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
        // An interval that starts neither at its value's definition nor at its block's start: a piece of a range cut
        // around a fixed range, which a split's moves bring its value to.
        for (uint32_t i = 0; valid && i < intervals->count; i += 1)
        {
            const uint32_t q = intervals->starts[i];
            if (q != input->definitions[intervals->values[i]] && input->block_starts[tier1_cfg_block_of(input, q)] != q)
            {
                if (pass == 0)
                {
                    event_offsets[q + 1] += 1;
                }
                else
                {
                    events[(uint64_t)event_fill[q] * 2] = 4;
                    events[(uint64_t)event_fill[q] * 2 + 1] = i;
                    event_fill[q] += 1;
                }
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
            valid = tier1_cfg_entry_location_oracle(result, v, entry, &location, &kind) || tier1_cfg_fail(check, "a value live at a block's start without a location", b, v, entry);
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
                            valid = valid && (!tier1_cfg_clobbers(result, &state, slot, v, q) || tier1_cfg_fail(check, "a store over another live value", b, v, q));
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
                        valid = tier1_cfg_location_oracle(result, v, q, &location, &where) || tier1_cfg_fail(check, "a definition outside its value's intervals", b, v, q);
                        valid = valid && ((where == BUSTER_TIER1_ALLOCATE_REGISTER || input->use_required == 0 || input->use_required[item] == 0) || tier1_cfg_fail(check, "a required definition without a register", b, v, q));
                        check->checks += 1;
                    }
                    else if (kind == 1 && step == 2 && (q != input->definitions[use_values[item]] || is_phi[use_values[item]] != 0))
                    {
                        const uint32_t v = use_values[item];
                        uint32_t location = 0;
                        uint8_t where = 0;
                        valid = tier1_cfg_location_oracle(result, v, q, &location, &where) || tier1_cfg_fail(check, "a use outside its value's intervals", b, v, q);
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
                    else if (kind == 4 && step == 1)
                    {
                        // A cut piece's value where the piece's first fragment keeps it, now that the split's moves
                        // have run: no use may read that piece, which would leave a move to the wrong location unseen.
                        const uint32_t v = intervals->values[item];
                        uint32_t location = 0;
                        uint8_t where = 0;
                        valid = tier1_cfg_location_oracle(result, v, q, &location, &where) || tier1_cfg_fail(check, "a piece outside its value's intervals", b, v, q);
                        valid = valid && (where == BUSTER_TIER1_ALLOCATE_REMATERIALIZE || tier1_cfg_state_get(&state, location) == v || tier1_cfg_fail(check, "a split that doesn't bring a value to its piece", b, v, q));
                        check->checks += 1;
                    }
                    else if (kind == 3 && step == 4)
                    {
                        const uint32_t v = item;
                        uint32_t location = 0;
                        uint8_t where = 0;
                        valid = tier1_cfg_location_oracle(result, v, q, &location, &where) || tier1_cfg_fail(check, "a definition outside its value's intervals", b, v, q);
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
            const bool copies = input->edge_copies != 0 && input->edge_copies[e] != 0;
            valid = ((moves == 0 && !copies) == (placement == TIER1_CFG_EDGE_EMPTY)) || tier1_cfg_fail(check, "an edge's placement disagrees with its moves", b, BUSTER_TIER1_CFG_NONE, s_entry);
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
                // An undefined operand: whatever the phi's location holds is its value along the edge.
                const bool undefined = expected == BUSTER_TIER1_CFG_NONE;
                uint32_t location = 0;
                uint8_t kind = 0;
                valid = tier1_cfg_entry_location_oracle(result, v, s_entry, &location, &kind) || tier1_cfg_fail(check, "a value live at a block's start without a location", s, v, s_entry);
                if (valid && kind == BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
                {
                    valid = (!phi && input->rematerializable != 0 && input->rematerializable[v] != 0) || tier1_cfg_fail(check, "a phi without a location", s, v, s_entry);
                }
                else if (valid)
                {
                    valid = undefined || tier1_cfg_state_get(&edge, location) == expected || tier1_cfg_fail(check, phi ? "an edge that doesn't bring a phi its operand" : "an edge that doesn't bring a live value", s, v, s_entry);
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

