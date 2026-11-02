// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Certificates of tier1_cfg.c's allocations (#939). Include after tier1_cfg.c and tier1_certificate.c.
//
// tier1_certificate_cfg publishes the program tier1_certificate_check proves for one CFG allocation. Its parts come
// from two places, which the trust boundary keeps apart:
//
// - The source is the preallocation problem alone (Tier1CfgInput): at each position, the uses the value's own
//   definition doesn't carry, the fixed ranges that start there (their registers clobbered after the uses) and the
//   definitions, as source instructions of at most BUSTER_CERT_MAX_OPERANDS operands each. A use or a definition that
//   isn't required may be in a slot (BUSTER_CERT_MEMORY), or, for a rematerializable value, its recipe itself
//   (BUSTER_CERT_RECIPE): an immediate, or a definition nothing keeps. Each position's first instruction clobbers the
//   general class's scratch register first: the selector's expansions use it (tier1_select.c's
//   BUSTER_TIER1_SELECT_SCRATCH), so no value may stay there between moves. The other classes' scratch registers carry
//   moves alone. The
//   values' classes, the CFG, the phis and the recipes are the input's; a rematerializable value is an integer recipe of
//   its class whose payload is its own id.
// - The allocation is untrusted. Its stream, block by block, is what a consumer runs: at each position the stores of
//   the fragments that leave a register for their slot, then the moves of a split there, then the reloads, register
//   moves and recomputations of the fragments that enter a register, then the position's instructions at their
//   fragments' locations. Each edge carries its resolved moves in order, a slot-to-slot move through its class's scratch
//   register as the consumers make it, and the successor's phis as simultaneous renames. Each block's certificate is
//   where the allocation keeps each value its intervals carry into the block (tier1_cfg_entry_location), a recipe's
//   availability for a value it recomputes there. Only the allocation's shape is validated here; its meaning is the
//   checker's to prove.
//
// Storage: the registers the source declares for each class that has values (allocatable, scratch and the fixed
// ranges'), each the class's width (a general register's 8 bytes, a vector register's 16 or, where one past xmm15 is
// declared, 64, an opmask register's 8), then one slot per spilled value and each class's temporary slot. A class
// without values holds none, so its registers and the fixed ranges' clobbers of them are left out. An allocation that
// names other storage is refused (TIER1_CERT_TRANSITION). Blocks that block 0 doesn't reach never run: the program
// leaves them out.
//
// This certifies the allocation, not machine code: the emitter's records are not decoded here (see
// docs/tier1-certificate.md). Inputs are borrowed and immutable. The program is pushed onto `output`; `scratch` is
// restored. On failure both arenas are restored and the program is left unchanged.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct Tier1CertCfgStats Tier1CertCfgStats;
struct Tier1CertCfgStats
{
    // The source: positions with an instruction, its instruction records and their operands.
    uint64_t positions;
    uint64_t sources;
    uint64_t operands;
    // The stream's edits: fragment transitions in blocks, splits' moves and edges' moves, each after expansion.
    uint64_t transitions;
    uint64_t split_moves;
    uint64_t edge_moves;
    // The certificate: block-entry facts (of which recipe availabilities) and phi renames on edges.
    uint64_t facts;
    uint64_t recipe_facts;
    uint64_t renames;
    uint64_t blocks;
    uint64_t unreachable_blocks;
    uint64_t edges;
    uint64_t locations;
    uint64_t units;
    // The published program's bytes: those its records take, and the output arena's growth, which bounds each array's
    // records before they are counted.
    uint64_t bytes;
    uint64_t arena_bytes;
    // Where publication refused the allocation: its block, value and position, or BUSTER_CERT_NONE.
    uint32_t block;
    uint32_t value;
    uint32_t position;
};

typedef struct Tier1CertCfgBuild Tier1CertCfgBuild;
struct Tier1CertCfgBuild
{
    const Tier1CfgInput* input;
    const Tier1CfgAllocation* result;
    Tier1CertCfgStats* stats;
    // Each class's register's location, or BUSTER_CERT_NONE where the source declares no such register.
    uint32_t registers[BUSTER_TIER1_ALLOCATE_CLASSES][32];
    uint32_t slot_base;
    uint32_t temp_base;
    uint32_t location_count;
};

static const uint8_t tier1_cert_cfg_classes[BUSTER_TIER1_ALLOCATE_CLASSES] = { TIER1_CERT_GPR, TIER1_CERT_VECTOR, TIER1_CERT_MASK };

static bool tier1_cert_cfg_refuse(Tier1CertCfgBuild* build, uint32_t block, uint32_t value, uint32_t position)
{
    if (build->stats->block == BUSTER_CERT_NONE && build->stats->value == BUSTER_CERT_NONE && build->stats->position == BUSTER_CERT_NONE)
    {
        build->stats->block = block;
        build->stats->value = value;
        build->stats->position = position;
    }
    return false;
}

// The allocation's shape, before anything reads through it: every column in range, the intervals and fragments in
// order, each offset bounded before it bounds a read, each fragment kind's field meaningful (a register only on a
// register fragment, a slot only where the allocation has slots), the moves' extents and the splits inside blocks,
// each edge's placement consistent with its moves and degrees. Values' meanings are left to the checker.
static bool tier1_cert_cfg_shape(const Tier1CfgInput* input, const Tier1CfgAllocation* result)
{
    const Tier1AllocationInput* intervals = &result->intervals;
    const Tier1Allocation* allocation = &result->allocation;
    const uint32_t values = input->value_count;
    bool valid = result->value_interval_offsets != 0 && result->value_interval_offsets[0] == 0 && result->value_interval_offsets[values] == intervals->count && allocation->interval_count == intervals->count;
    valid = valid && (intervals->count == 0 || (intervals->values != 0 && intervals->classes != 0 && intervals->starts != 0 && intervals->ends != 0));
    valid = valid && allocation->fragment_offsets != 0 && allocation->fragment_offsets[0] == 0 && allocation->fragment_offsets[intervals->count] == allocation->fragment_count;
    valid = valid && (allocation->fragment_count == 0 || (allocation->starts != 0 && allocation->ends != 0 && allocation->kinds != 0 && allocation->registers != 0));
    valid = valid && (allocation->spill_count == 0 || (allocation->spill_values != 0 && allocation->spill_ids != 0)) && allocation->spill_count <= values;
    for (uint32_t s = 0; valid && s < allocation->spill_count; s += 1)
    {
        valid = allocation->spill_values[s] < values;
    }
    for (uint32_t v = 0; valid && v < values; v += 1)
    {
        valid = result->value_interval_offsets[v] <= result->value_interval_offsets[v + 1] && result->value_interval_offsets[v + 1] <= intervals->count;
        for (uint32_t i = result->value_interval_offsets[v]; valid && i < result->value_interval_offsets[v + 1]; i += 1)
        {
            valid = intervals->values[i] == v && intervals->starts[i] < intervals->ends[i] && intervals->ends[i] <= input->position_count;
            valid = valid && (i == result->value_interval_offsets[v] || intervals->ends[i - 1] <= intervals->starts[i]);
            valid = valid && allocation->fragment_offsets[i] < allocation->fragment_offsets[i + 1] && allocation->fragment_offsets[i + 1] <= allocation->fragment_count;
            uint32_t position = intervals->starts[i];
            for (uint32_t f = valid ? allocation->fragment_offsets[i] : 0; valid && f < allocation->fragment_offsets[i + 1]; f += 1)
            {
                valid = allocation->starts[f] == position && allocation->ends[f] > position && allocation->kinds[f] <= BUSTER_TIER1_ALLOCATE_REMATERIALIZE;
                valid = valid && (allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER ? allocation->registers[f] < 32 : allocation->registers[f] == UINT8_MAX);
                valid = valid && (allocation->kinds[f] != BUSTER_TIER1_ALLOCATE_SPILL || (allocation->spill_count != 0 && allocation->spill_ids[v] < allocation->spill_count && allocation->spill_values[allocation->spill_ids[v]] == v));
                position = allocation->ends[f];
            }
            valid = valid && position == intervals->ends[i];
        }
    }
    valid = valid && result->edge_move_offsets != 0 && result->split_move_offsets != 0 && result->edge_placements != 0;
    valid = valid && result->edge_move_offsets[0] == 0 && result->edge_move_offsets[input->edge_count] == result->split_move_offsets[0] && result->split_move_offsets[result->split_count] == result->move_count;
    valid = valid && (result->move_count == 0 || (result->move_sources != 0 && result->move_destinations != 0 && result->move_values != 0));
    valid = valid && (result->split_count == 0 || result->split_positions != 0);
    for (uint32_t m = 0; valid && m < result->move_count; m += 1)
    {
        valid = result->move_values[m] < values;
    }
    for (uint32_t k = 0; valid && k < result->split_count; k += 1)
    {
        const uint32_t q = result->split_positions[k];
        valid = q < input->position_count && (k == 0 || q > result->split_positions[k - 1]) && input->block_starts[tier1_cfg_block_of(input, q)] != q && result->split_move_offsets[k] <= result->split_move_offsets[k + 1];
    }
    for (uint32_t b = 0; valid && b < input->block_count; b += 1)
    {
        const uint32_t out_degree = input->successor_offsets[b + 1] - input->successor_offsets[b];
        for (uint32_t e = input->successor_offsets[b]; valid && e < input->successor_offsets[b + 1]; e += 1)
        {
            const uint32_t s = input->successors[e];
            const uint32_t in_degree = input->predecessor_offsets[s + 1] - input->predecessor_offsets[s];
            const uint8_t placement = result->edge_placements[e];
            valid = result->edge_move_offsets[e] <= result->edge_move_offsets[e + 1] && placement <= TIER1_CFG_EDGE_SPLIT;
            valid = valid && (result->edge_move_offsets[e] == result->edge_move_offsets[e + 1]) == (placement == TIER1_CFG_EDGE_EMPTY);
            valid = valid && (placement != TIER1_CFG_EDGE_PREDECESSOR || out_degree == 1) && (placement != TIER1_CFG_EDGE_SUCCESSOR || in_degree == 1);
        }
    }
    return valid;
}

// The certificate location of tier1_cfg's location id, or BUSTER_CERT_NONE for storage the source doesn't declare.
static uint32_t tier1_cert_cfg_storage(const Tier1CertCfgBuild* build, uint32_t location)
{
    const uint32_t spills = build->result->allocation.spill_count;
    uint32_t result = BUSTER_CERT_NONE;
    if (location < BUSTER_TIER1_CFG_SPILL)
    {
        result = build->registers[location / 32][location % 32];
    }
    else if (location - BUSTER_TIER1_CFG_SPILL < spills)
    {
        result = build->slot_base + location - BUSTER_TIER1_CFG_SPILL;
    }
    else if (location - BUSTER_TIER1_CFG_SPILL - spills < BUSTER_TIER1_ALLOCATE_CLASSES)
    {
        result = build->temp_base + location - BUSTER_TIER1_CFG_SPILL - spills;
    }
    return result;
}

// Value v's location at `position` for an operand: its fragment's storage, or BUSTER_CERT_NONE for a fragment that
// recomputes it. False where no interval of v covers the position or the storage is undeclared.
static bool tier1_cert_cfg_operand(Tier1CertCfgBuild* build, uint32_t block, uint32_t value, uint32_t position, uint32_t* location)
{
    uint32_t where = 0;
    uint8_t kind = 0;
    bool valid = tier1_cfg_location(build->result, value, position, &where, &kind);
    *location = BUSTER_CERT_NONE;
    if (valid && kind != BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
    {
        *location = tier1_cert_cfg_storage(build, where);
        valid = *location != BUSTER_CERT_NONE;
    }
    return valid || tier1_cert_cfg_refuse(build, block, value, position);
}

// One resolved move of an edge or a split as edits: a move, a recomputation into a register, or, through the carried
// value's class's scratch register, a slot-to-slot move or a recomputation into a slot.
static bool tier1_cert_cfg_move(Tier1CertCfgBuild* build, uint32_t m, uint32_t block, uint32_t position, Tier1CertInstruction* code, uint32_t* count)
{
    const Tier1CfgAllocation* result = build->result;
    const uint32_t value = result->move_values[m];
    const uint32_t class = build->input->classes[value];
    const uint32_t destination = tier1_cert_cfg_storage(build, result->move_destinations[m]);
    const uint32_t source = result->move_sources[m] == BUSTER_TIER1_CFG_REMATERIALIZE ? BUSTER_CERT_NONE : tier1_cert_cfg_storage(build, result->move_sources[m]);
    const bool recompute = result->move_sources[m] == BUSTER_TIER1_CFG_REMATERIALIZE;
    bool valid = (destination != BUSTER_CERT_NONE && (recompute || source != BUSTER_CERT_NONE)) || tier1_cert_cfg_refuse(build, block, value, position);
    const uint32_t scratch = build->registers[class][build->input->scratch_registers[class]];
    if (valid && recompute)
    {
        // A recomputation builds its value in a register: into a slot, it goes through the scratch register.
        const bool slot = destination >= build->slot_base;
        code[(*count)++] = (Tier1CertInstruction){
            .immediate = value,
            .a = slot ? scratch : destination,
            .value = value,
            .op = TIER1_CERT_CONSTANT,
        };
        if (slot)
        {
            code[(*count)++] = (Tier1CertInstruction){
                .a = destination,
                .b = scratch,
                .op = TIER1_CERT_MOVE,
            };
        }
    }
    else if (valid && destination >= build->slot_base && source >= build->slot_base)
    {
        code[(*count)++] = (Tier1CertInstruction){
            .a = scratch,
            .b = source,
            .op = TIER1_CERT_MOVE,
        };
        code[(*count)++] = (Tier1CertInstruction){
            .a = destination,
            .b = scratch,
            .op = TIER1_CERT_MOVE,
        };
    }
    else if (valid)
    {
        code[(*count)++] = (Tier1CertInstruction){
            .a = destination,
            .b = source,
            .op = TIER1_CERT_MOVE,
        };
    }
    return valid;
}

// A fragment's transition at its start (not an interval's first fragment), as the consumers run it: `store` the stores'
// phase (a register into its value's slot), otherwise the reloads' (a slot, another register or a recomputation into
// a register). A fragment that recomputes needs nothing; one that keeps its predecessor's location needs no edit.
static bool tier1_cert_cfg_transition(Tier1CertCfgBuild* build, uint32_t f, uint32_t value, uint32_t block, bool store, Tier1CertInstruction* code, uint32_t* count)
{
    const Tier1Allocation* allocation = &build->result->allocation;
    const uint32_t class = build->input->classes[value];
    const uint8_t previous = allocation->kinds[f - 1];
    const uint8_t current = allocation->kinds[f];
    const uint32_t slot = allocation->spill_ids[value] < allocation->spill_count ? build->slot_base + allocation->spill_ids[value] : BUSTER_CERT_NONE;
    const uint32_t before = previous == BUSTER_TIER1_ALLOCATE_REGISTER ? build->registers[class][allocation->registers[f - 1]] : previous == BUSTER_TIER1_ALLOCATE_SPILL ? slot : BUSTER_CERT_NONE;
    const uint32_t after = current == BUSTER_TIER1_ALLOCATE_REGISTER ? build->registers[class][allocation->registers[f]] : current == BUSTER_TIER1_ALLOCATE_SPILL ? slot : BUSTER_CERT_NONE;
    bool valid = true;
    if (store && current == BUSTER_TIER1_ALLOCATE_SPILL && previous != BUSTER_TIER1_ALLOCATE_SPILL)
    {
        // A store needs its value in a register: a recomputed value has none to store.
        valid = (previous == BUSTER_TIER1_ALLOCATE_REGISTER && before != BUSTER_CERT_NONE && after != BUSTER_CERT_NONE) || tier1_cert_cfg_refuse(build, block, value, allocation->starts[f]);
        if (valid)
        {
            code[(*count)++] = (Tier1CertInstruction){
                .a = after,
                .b = before,
                .op = TIER1_CERT_MOVE,
            };
        }
    }
    else if (!store && current == BUSTER_TIER1_ALLOCATE_REGISTER)
    {
        valid = (after != BUSTER_CERT_NONE && (previous == BUSTER_TIER1_ALLOCATE_REMATERIALIZE || before != BUSTER_CERT_NONE)) || tier1_cert_cfg_refuse(build, block, value, allocation->starts[f]);
        if (valid && previous == BUSTER_TIER1_ALLOCATE_REMATERIALIZE)
        {
            code[(*count)++] = (Tier1CertInstruction){
                .immediate = value,
                .a = after,
                .value = value,
                .op = TIER1_CERT_CONSTANT,
            };
        }
        else if (valid && before != after)
        {
            code[(*count)++] = (Tier1CertInstruction){
                .a = after,
                .b = before,
                .op = TIER1_CERT_MOVE,
            };
        }
    }
    return valid;
}

static Tier1CertError tier1_certificate_cfg(const Tier1CfgInput* input, const Tier1CfgAllocation* result, Arena* output, Arena* scratch, Tier1CertProgram* program, Tier1CertCfgStats* stats)
{
    const uint64_t output_saved = output->length;
    const uint64_t scratch_saved = scratch->length;
    Tier1CertCfgStats tally = {
        .block = BUSTER_CERT_NONE,
        .value = BUSTER_CERT_NONE,
        .position = BUSTER_CERT_NONE,
    };
    Tier1CertCfgBuild build = {
        .input = input,
        .result = result,
        .stats = &tally,
    };
    const Tier1Allocation* allocation = &result->allocation;
    const uint32_t values = input->value_count;
    const uint32_t blocks = input->block_count;
    const uint32_t positions = input->position_count;
    const uint32_t edges = input->edge_count;
    Tier1CertError error = tier1_cert_cfg_shape(input, result) ? TIER1_CERT_OK : TIER1_CERT_INVALID;
    // Storage: the source's registers for each class that has values, then the slots and the temporary slots.
    bool used[BUSTER_TIER1_ALLOCATE_CLASSES] = { false };
    for (uint32_t v = 0; v < values; v += 1)
    {
        used[input->classes[v]] = true;
    }
    uint32_t storage[BUSTER_TIER1_ALLOCATE_CLASSES] = { 0 };
    for (uint32_t c = 0; c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
    {
        storage[c] = used[c] ? input->registers[c] | (UINT32_C(1) << (input->scratch_registers[c] & 31)) : 0;
    }
    for (uint32_t r = 0; error == TIER1_CERT_OK && r < input->fixed.count; r += 1)
    {
        storage[input->fixed.classes[r]] |= used[input->fixed.classes[r]] ? input->fixed.registers[r] : 0;
    }
    if (error == TIER1_CERT_OK && ((storage[BUSTER_TIER1_ALLOCATE_GPR] >> 16) != 0 || (storage[BUSTER_TIER1_ALLOCATE_MASK] >> 8) != 0))
    {
        error = TIER1_CERT_INVALID;
    }
    const uint16_t widths[BUSTER_TIER1_ALLOCATE_CLASSES] = { 8, (storage[BUSTER_TIER1_ALLOCATE_VECTOR] >> 16) == 0 ? 16 : 64, 8 };
    uint32_t declared = 0;
    for (uint32_t c = 0; c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
    {
        declared += (uint32_t)__builtin_popcount(storage[c]);
    }
    build.slot_base = declared;
    build.temp_base = declared + allocation->spill_count;
    build.location_count = build.temp_base + BUSTER_TIER1_ALLOCATE_CLASSES;
    Tier1CertLocation* locations = error == TIER1_CERT_OK ? arena_push_array(output, build.location_count, sizeof(Tier1CertLocation), _Alignof(Tier1CertLocation)) : 0;
    Tier1CertValue* cert_values = error == TIER1_CERT_OK ? arena_push_array(output, (uint64_t)values + 1, sizeof(Tier1CertValue), _Alignof(Tier1CertValue)) : 0;
    error = error == TIER1_CERT_OK && (locations == 0 || cert_values == 0) ? TIER1_CERT_CAPACITY : error;
    uint32_t units = 0;
    if (error == TIER1_CERT_OK)
    {
        uint32_t next = 0;
        for (uint32_t c = 0; c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
        {
            for (uint32_t r = 0; r < 32; r += 1)
            {
                build.registers[c][r] = BUSTER_CERT_NONE;
                if (((storage[c] >> r) & 1) != 0)
                {
                    build.registers[c][r] = next;
                    locations[next] = (Tier1CertLocation){
                        .unit = units,
                        .width = widths[c],
                        .extent = widths[c],
                        .class = tier1_cert_cfg_classes[c],
                        .reg = (uint8_t)r,
                    };
                    units += widths[c];
                    next += 1;
                }
            }
        }
        for (uint32_t s = 0; s < allocation->spill_count; s += 1)
        {
            const uint32_t class = input->classes[allocation->spill_values[s]];
            locations[build.slot_base + s] = (Tier1CertLocation){
                .unit = units,
                .width = widths[class],
                .extent = widths[class],
                .class = TIER1_CERT_STACK,
            };
            units += widths[class];
        }
        for (uint32_t c = 0; c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
        {
            locations[build.temp_base + c] = (Tier1CertLocation){
                .unit = units,
                .width = widths[c],
                .extent = widths[c],
                .class = TIER1_CERT_STACK,
            };
            units += widths[c];
        }
        for (uint32_t v = 0; v < values; v += 1)
        {
            const bool recipe = input->rematerializable != 0 && input->rematerializable[v] != 0;
            cert_values[v] = (Tier1CertValue){
                .payload = recipe ? v : 0,
                .width = widths[input->classes[v]],
                .class = tier1_cert_cfg_classes[input->classes[v]],
                .recipe = recipe ? TIER1_CERT_INTEGER_RECIPE : TIER1_CERT_NO_RECIPE,
            };
        }
    }
    // The source's phis and the requirements each value's definition carries: a use at its own position (a non-phi's)
    // is the definition's.
    uint8_t* is_phi = error == TIER1_CERT_OK ? arena_push(scratch, (uint64_t)values + 1, 1) : 0;
    uint8_t* own_required = error == TIER1_CERT_OK ? arena_push(scratch, (uint64_t)values + 1, 1) : 0;
    error = error == TIER1_CERT_OK && (is_phi == 0 || own_required == 0) ? TIER1_CERT_CAPACITY : error;
    if (error == TIER1_CERT_OK)
    {
        memset(is_phi, 0, (uint64_t)values + 1);
        memset(own_required, 0, (uint64_t)values + 1);
        for (uint32_t k = 0; k < input->phi_count; k += 1)
        {
            is_phi[input->phi_values[k]] = 1;
        }
        for (uint32_t v = 0; v < values; v += 1)
        {
            for (uint32_t u = input->use_offsets[v]; is_phi[v] == 0 && u < input->use_offsets[v + 1]; u += 1)
            {
                own_required[v] |= input->use_positions[u] == input->definitions[v] && input->use_required != 0 && input->use_required[u] != 0;
            }
        }
    }
    // Reachable blocks, renumbered in layout order.
    uint32_t* numbers = error == TIER1_CERT_OK ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* queue = error == TIER1_CERT_OK ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    error = error == TIER1_CERT_OK && (numbers == 0 || queue == 0) ? TIER1_CERT_CAPACITY : error;
    uint32_t reachable = 0;
    if (error == TIER1_CERT_OK)
    {
        memset(numbers, 0xff, (uint64_t)blocks * sizeof(uint32_t));
        numbers[0] = 0;
        queue[0] = 0;
        uint32_t tail = 1;
        for (uint32_t head = 0; head < tail; head += 1)
        {
            const uint32_t b = queue[head];
            for (uint32_t e = input->successor_offsets[b]; e < input->successor_offsets[b + 1]; e += 1)
            {
                const uint32_t s = input->successors[e];
                if (numbers[s] == BUSTER_CERT_NONE)
                {
                    numbers[s] = 0;
                    queue[tail++] = s;
                }
            }
        }
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            numbers[b] = numbers[b] != BUSTER_CERT_NONE ? reachable++ : BUSTER_CERT_NONE;
        }
        tally.blocks = reachable;
        tally.unreachable_blocks = blocks - reachable;
    }
    // Events by position, counted then filled: fragment transitions, operand uses, fixed ranges' starts and non-phi
    // definitions, in that order.
    uint32_t* event_offsets = error == TIER1_CERT_OK ? arena_push_array(scratch, (uint64_t)positions + 2, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    const uint64_t event_capacity = (uint64_t)allocation->fragment_count + input->use_count + input->fixed.count + values;
    uint32_t* events = error == TIER1_CERT_OK ? arena_push_array(scratch, event_capacity * 2 + 2, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* fill = error == TIER1_CERT_OK ? arena_push_array(scratch, (uint64_t)positions + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    error = error == TIER1_CERT_OK && (event_offsets == 0 || events == 0 || fill == 0) ? TIER1_CERT_CAPACITY : error;
    for (uint32_t pass = 0; error == TIER1_CERT_OK && pass < 2; pass += 1)
    {
        if (pass == 0)
        {
            memset(event_offsets, 0, ((uint64_t)positions + 2) * sizeof(uint32_t));
        }
        else
        {
            for (uint32_t q = 0; q <= positions; q += 1)
            {
                event_offsets[q + 1] += event_offsets[q];
                fill[q] = event_offsets[q];
            }
        }
        for (uint32_t i = 0; i < result->intervals.count; i += 1)
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
                    events[(uint64_t)fill[q] * 2] = 0;
                    events[(uint64_t)fill[q] * 2 + 1] = f;
                    fill[q] += 1;
                }
            }
        }
        for (uint32_t v = 0; v < values; v += 1)
        {
            for (uint32_t u = input->use_offsets[v]; u < input->use_offsets[v + 1]; u += 1)
            {
                const uint32_t q = input->use_positions[u];
                if ((is_phi[v] != 0 || q != input->definitions[v]) && pass == 0)
                {
                    event_offsets[q + 1] += 1;
                }
                else if (is_phi[v] != 0 || q != input->definitions[v])
                {
                    events[(uint64_t)fill[q] * 2] = 1;
                    events[(uint64_t)fill[q] * 2 + 1] = u;
                    fill[q] += 1;
                }
            }
        }
        for (uint32_t r = 0; r < input->fixed.count; r += 1)
        {
            const uint32_t q = input->fixed.starts[r];
            if (pass == 0)
            {
                event_offsets[q + 1] += 1;
            }
            else
            {
                events[(uint64_t)fill[q] * 2] = 2;
                events[(uint64_t)fill[q] * 2 + 1] = r;
                fill[q] += 1;
            }
        }
        for (uint32_t v = 0; v < values; v += 1)
        {
            const uint32_t q = input->definitions[v];
            if (is_phi[v] == 0 && pass == 0)
            {
                event_offsets[q + 1] += 1;
            }
            else if (is_phi[v] == 0)
            {
                events[(uint64_t)fill[q] * 2] = 3;
                events[(uint64_t)fill[q] * 2 + 1] = v;
                fill[q] += 1;
            }
        }
    }
    // Each use's value, and each fragment's interval.
    uint32_t* use_values = error == TIER1_CERT_OK ? arena_push_array(scratch, (uint64_t)input->use_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* fragment_values = error == TIER1_CERT_OK ? arena_push_array(scratch, (uint64_t)allocation->fragment_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    error = error == TIER1_CERT_OK && (use_values == 0 || fragment_values == 0) ? TIER1_CERT_CAPACITY : error;
    if (error == TIER1_CERT_OK)
    {
        for (uint32_t v = 0; v < values; v += 1)
        {
            for (uint32_t u = input->use_offsets[v]; u < input->use_offsets[v + 1]; u += 1)
            {
                use_values[u] = v;
            }
        }
        for (uint32_t i = 0; i < result->intervals.count; i += 1)
        {
            for (uint32_t f = allocation->fragment_offsets[i]; f < allocation->fragment_offsets[i + 1]; f += 1)
            {
                fragment_values[f] = result->intervals.values[i];
            }
        }
    }
    // The certificate: per block, the values whose intervals carry them into it (a phi of the block among them, not a
    // value the block's first position defines), counted then filled.
    uint32_t* fact_offsets = error == TIER1_CERT_OK ? arena_push_array(scratch, (uint64_t)blocks + 2, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    error = error == TIER1_CERT_OK && fact_offsets == 0 ? TIER1_CERT_CAPACITY : error;
    Tier1CertFact* facts = 0;
    for (uint32_t pass = 0; error == TIER1_CERT_OK && pass < 2; pass += 1)
    {
        if (pass == 0)
        {
            memset(fact_offsets, 0, ((uint64_t)blocks + 2) * sizeof(uint32_t));
        }
        else
        {
            for (uint32_t b = 0; b < blocks; b += 1)
            {
                fact_offsets[b + 1] += fact_offsets[b];
                fill[b] = fact_offsets[b];
            }
            facts = arena_push_array(output, (uint64_t)fact_offsets[blocks] + 1, sizeof(Tier1CertFact), _Alignof(Tier1CertFact));
            error = facts == 0 ? TIER1_CERT_CAPACITY : error;
        }
        for (uint32_t i = 0; error == TIER1_CERT_OK && i < result->intervals.count; i += 1)
        {
            const uint32_t v = result->intervals.values[i];
            const uint32_t start = result->intervals.starts[i];
            for (uint32_t b = tier1_cfg_block_of(input, start); error == TIER1_CERT_OK && b < blocks && input->block_starts[b] < result->intervals.ends[i]; b += 1)
            {
                const uint32_t entry = input->block_starts[b];
                if (entry >= start && (input->definitions[v] != entry || is_phi[v] != 0) && numbers[b] != BUSTER_CERT_NONE)
                {
                    if (pass == 0)
                    {
                        fact_offsets[b + 1] += 1;
                    }
                    else
                    {
                        uint32_t where = 0;
                        uint8_t kind = 0;
                        const bool found = tier1_cfg_entry_location(result, v, entry, &where, &kind);
                        const uint32_t location = kind == BUSTER_TIER1_ALLOCATE_REMATERIALIZE ? BUSTER_CERT_NONE : tier1_cert_cfg_storage(&build, where);
                        error = found && (kind == BUSTER_TIER1_ALLOCATE_REMATERIALIZE || location != BUSTER_CERT_NONE) ? error : TIER1_CERT_TRANSITION;
                        if (error != TIER1_CERT_OK)
                        {
                            tier1_cert_cfg_refuse(&build, b, v, entry);
                        }
                        facts[fill[b]] = (Tier1CertFact){
                            .value = v,
                            .location = location,
                        };
                        fill[b] += 1;
                        tally.recipe_facts += kind == BUSTER_TIER1_ALLOCATE_REMATERIALIZE;
                    }
                }
            }
        }
    }
    tally.facts = facts != 0 ? fact_offsets[blocks] : 0;
    // Each edge's source block and its index among its destination's predecessors, for the phis' renames.
    uint32_t* edge_index = error == TIER1_CERT_OK ? arena_push_array(scratch, (uint64_t)edges + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* mark = error == TIER1_CERT_OK ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* order = error == TIER1_CERT_OK ? arena_push_array(scratch, blocks, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* incoming_offsets = error == TIER1_CERT_OK ? arena_push_array(scratch, (uint64_t)blocks + 2, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* incoming = error == TIER1_CERT_OK ? arena_push_array(scratch, (uint64_t)edges + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* sources = error == TIER1_CERT_OK ? arena_push_array(scratch, (uint64_t)edges + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    error = error == TIER1_CERT_OK && (edge_index == 0 || mark == 0 || order == 0 || incoming_offsets == 0 || incoming == 0 || sources == 0) ? TIER1_CERT_CAPACITY : error;
    if (error == TIER1_CERT_OK)
    {
        memset(incoming_offsets, 0, ((uint64_t)blocks + 2) * sizeof(uint32_t));
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            for (uint32_t e = input->successor_offsets[b]; e < input->successor_offsets[b + 1]; e += 1)
            {
                sources[e] = b;
                incoming_offsets[input->successors[e] + 1] += 1;
            }
        }
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            incoming_offsets[b + 1] += incoming_offsets[b];
            fill[b] = incoming_offsets[b];
        }
        for (uint32_t e = 0; e < edges; e += 1)
        {
            incoming[fill[input->successors[e]]++] = e;
        }
        memset(mark, 0xff, (uint64_t)blocks * sizeof(uint32_t));
        for (uint32_t s = 0; s < blocks; s += 1)
        {
            for (uint32_t j = 0; j < input->predecessor_offsets[s + 1] - input->predecessor_offsets[s]; j += 1)
            {
                const uint32_t p = input->predecessors[input->predecessor_offsets[s] + j];
                mark[p] = s;
                order[p] = j;
            }
            for (uint32_t k = incoming_offsets[s]; k < incoming_offsets[s + 1]; k += 1)
            {
                const uint32_t e = incoming[k];
                edge_index[e] = mark[sources[e]] == s ? order[sources[e]] : BUSTER_CERT_NONE;
            }
        }
    }
    // The blocks' streams and sources, the edges' moves and renames. Bounds: a use or definition record holds at least
    // one operand; a position's clobber records are its fixed ranges' registers in eights, one more, and one for a fixed
    // range of no registers; each transition is one edit; a move is at most two.
    uint64_t clobbers = 0;
    for (uint32_t r = 0; r < input->fixed.count; r += 1)
    {
        clobbers += (uint64_t)__builtin_popcount(input->fixed.registers[r]);
    }
    const uint64_t source_capacity = (uint64_t)input->use_count + values + clobbers / BUSTER_CERT_MAX_OPERANDS + 2 * (uint64_t)input->fixed.count;
    const uint64_t code_capacity = source_capacity + allocation->fragment_count + 2 * (uint64_t)result->move_count;
    Tier1CertSource* source = error == TIER1_CERT_OK ? arena_push_array(output, source_capacity + 1, sizeof(Tier1CertSource), _Alignof(Tier1CertSource)) : 0;
    Tier1CertInstruction* code = error == TIER1_CERT_OK ? arena_push_array(output, code_capacity + 1, sizeof(Tier1CertInstruction), _Alignof(Tier1CertInstruction)) : 0;
    Tier1CertBlock* cert_blocks = error == TIER1_CERT_OK ? arena_push_array(output, (uint64_t)reachable + 1, sizeof(Tier1CertBlock), _Alignof(Tier1CertBlock)) : 0;
    Tier1CertEdge* cert_edges = error == TIER1_CERT_OK ? arena_push_array(output, (uint64_t)edges + 1, sizeof(Tier1CertEdge), _Alignof(Tier1CertEdge)) : 0;
    Tier1CertRename* renames = error == TIER1_CERT_OK ? arena_push_array(output, (uint64_t)input->phi_operand_offsets[input->phi_count] + 1, sizeof(Tier1CertRename), _Alignof(Tier1CertRename)) : 0;
    error = error == TIER1_CERT_OK && (source == 0 || code == 0 || cert_blocks == 0 || cert_edges == 0 || renames == 0) ? TIER1_CERT_CAPACITY : error;
    uint32_t source_count = 0;
    uint32_t code_count = 0;
    uint32_t edge_count = 0;
    uint32_t rename_count = 0;
    uint32_t split = 0;
    uint32_t scratch_locations[BUSTER_TIER1_ALLOCATE_CLASSES] = { 0 };
    for (uint32_t c = 0; error == TIER1_CERT_OK && c < BUSTER_TIER1_ALLOCATE_CLASSES; c += 1)
    {
        scratch_locations[c] = build.registers[c][input->scratch_registers[c]];
    }
    for (uint32_t b = 0; error == TIER1_CERT_OK && b < blocks; b += 1)
    {
        const uint32_t first_source = source_count;
        const uint32_t first_code = code_count;
        const uint64_t first_split_moves = tally.split_moves;
        // A block that block 0 doesn't reach never runs: its positions publish nothing.
        for (uint32_t q = input->block_starts[b]; error == TIER1_CERT_OK && numbers[b] != BUSTER_CERT_NONE && q < input->block_starts[b + 1]; q += 1)
        {
            const uint32_t low = event_offsets[q];
            const uint32_t high = event_offsets[q + 1];
            // Stores, then the split's moves, then the reloads, register moves and recomputations.
            for (uint32_t e = low; error == TIER1_CERT_OK && e < high && events[(uint64_t)e * 2] == 0; e += 1)
            {
                const uint32_t f = events[(uint64_t)e * 2 + 1];
                error = tier1_cert_cfg_transition(&build, f, fragment_values[f], b, true, code, &code_count) ? error : TIER1_CERT_TRANSITION;
            }
            while (split < result->split_count && result->split_positions[split] < q)
            {
                split += 1;
            }
            if (split < result->split_count && result->split_positions[split] == q)
            {
                for (uint32_t m = result->split_move_offsets[split]; error == TIER1_CERT_OK && m < result->split_move_offsets[split + 1]; m += 1)
                {
                    const uint32_t before = code_count;
                    error = tier1_cert_cfg_move(&build, m, b, q, code, &code_count) ? error : TIER1_CERT_TRANSITION;
                    tally.split_moves += code_count - before;
                }
            }
            for (uint32_t e = low; error == TIER1_CERT_OK && e < high && events[(uint64_t)e * 2] == 0; e += 1)
            {
                const uint32_t f = events[(uint64_t)e * 2 + 1];
                error = tier1_cert_cfg_transition(&build, f, fragment_values[f], b, false, code, &code_count) ? error : TIER1_CERT_TRANSITION;
            }
            // The position's instructions: its uses, its fixed ranges' clobbers and its definitions, each kind in
            // records of at most BUSTER_CERT_MAX_OPERANDS, the first record clobbering the general scratch register.
            bool first = true;
            for (uint32_t kind = 1; error == TIER1_CERT_OK && kind <= 3; kind += 1)
            {
                uint32_t e = low;
                while (e < high && events[(uint64_t)e * 2] < kind)
                {
                    e += 1;
                }
                // A fixed range's registers left for the next record, and their class.
                uint32_t mask = 0;
                uint32_t class = 0;
                uint32_t chunk = 0;
                while (error == TIER1_CERT_OK && ((e < high && events[(uint64_t)e * 2] == kind) || mask != 0))
                {
                    Tier1CertSource* record = source + source_count;
                    Tier1CertInstruction* instruction = code + code_count;
                    *record = (Tier1CertSource){
                        .immediate = chunk,
                        .effect = kind,
                        .opcode = q,
                    };
                    *instruction = (Tier1CertInstruction){
                        .immediate = chunk,
                        .effect = kind,
                        .opcode = q,
                        .op = TIER1_CERT_ORIGINAL,
                    };
                    if (first && scratch_locations[BUSTER_TIER1_ALLOCATE_GPR] != BUSTER_CERT_NONE)
                    {
                        record->before[record->before_count++] = scratch_locations[BUSTER_TIER1_ALLOCATE_GPR];
                    }
                    first = false;
                    if (kind == 2)
                    {
                        while (record->after_count < BUSTER_CERT_MAX_OPERANDS && (mask != 0 || (e < high && events[(uint64_t)e * 2] == kind)))
                        {
                            if (mask == 0)
                            {
                                // A class without values has no storage: its clobbers change nothing.
                                const uint32_t item = events[(uint64_t)e * 2 + 1];
                                class = input->fixed.classes[item];
                                mask = used[class] ? input->fixed.registers[item] : 0;
                                e += 1;
                            }
                            if (mask != 0)
                            {
                                record->after[record->after_count++] = build.registers[class][__builtin_ctz(mask)];
                                mask &= mask - 1;
                            }
                        }
                    }
                    while (error == TIER1_CERT_OK && kind != 2 && e < high && events[(uint64_t)e * 2] == kind && record->use_count + record->def_count < BUSTER_CERT_MAX_OPERANDS)
                    {
                        const uint32_t item = events[(uint64_t)e * 2 + 1];
                        const uint32_t value = kind == 1 ? use_values[item] : item;
                        const bool required = kind == 1 ? input->use_required != 0 && input->use_required[item] != 0 : own_required[value] != 0;
                        const bool recipe = input->rematerializable != 0 && input->rematerializable[value] != 0;
                        const uint32_t definition_registers = input->definition_registers != 0 ? input->definition_registers[value] : 0;
                        const uint32_t o = record->use_count + record->def_count;
                        record->operands[o] = (Tier1CertOperand){
                            .value = value,
                            .allowed_registers = kind == 3 && definition_registers != 0 ? definition_registers : input->allowed_registers[value],
                            .flags = required ? 0 : (uint8_t)(BUSTER_CERT_MEMORY | (recipe ? BUSTER_CERT_RECIPE : 0)),
                        };
                        uint32_t location = BUSTER_CERT_NONE;
                        error = tier1_cert_cfg_operand(&build, b, value, kind == 1 ? q : input->definitions[value], &location) ? error : TIER1_CERT_VALUE;
                        instruction->locations[o] = location;
                        record->use_count += kind == 1;
                        record->def_count += kind == 3;
                        e += 1;
                    }
                    instruction->operand_count = (uint8_t)(record->use_count + record->def_count);
                    tally.operands += instruction->operand_count;
                    source_count += 1;
                    code_count += 1;
                    chunk += 1;
                }
            }
            tally.positions += !first;
        }
        if (numbers[b] != BUSTER_CERT_NONE)
        {
            Tier1CertBlock* block = cert_blocks + numbers[b];
            *block = (Tier1CertBlock){
                .source = source + first_source,
                .code = code + first_code,
                .certificate = facts + fact_offsets[b],
                .edges = cert_edges + edge_count,
                .source_count = source_count - first_source,
                .code_count = code_count - first_code,
                .certificate_count = fact_offsets[b + 1] - fact_offsets[b],
            };
            for (uint32_t e = input->successor_offsets[b]; error == TIER1_CERT_OK && e < input->successor_offsets[b + 1]; e += 1)
            {
                const uint32_t s = input->successors[e];
                const uint32_t j = edge_index[e];
                Tier1CertEdge* edge = cert_edges + edge_count;
                *edge = (Tier1CertEdge){
                    .renames = renames + rename_count,
                    .moves = code + code_count,
                    .destination = numbers[s],
                };
                for (uint32_t k = input->phi_offsets[s]; error == TIER1_CERT_OK && k < input->phi_offsets[s + 1]; k += 1)
                {
                    error = j != BUSTER_CERT_NONE ? error : TIER1_CERT_INVALID;
                    renames[rename_count] = (Tier1CertRename){
                        .destination = input->phi_values[k],
                        .source = j != BUSTER_CERT_NONE ? input->phi_operands[input->phi_operand_offsets[k] + j] : 0,
                    };
                    rename_count += 1;
                }
                const uint32_t before = code_count;
                for (uint32_t m = result->edge_move_offsets[e]; error == TIER1_CERT_OK && m < result->edge_move_offsets[e + 1]; m += 1)
                {
                    error = tier1_cert_cfg_move(&build, m, b, input->block_starts[s], code, &code_count) ? error : TIER1_CERT_TRANSITION;
                }
                edge->rename_count = (uint32_t)(renames + rename_count - edge->renames);
                edge->move_count = code_count - before;
                tally.edge_moves += code_count - before;
                tally.renames += edge->rename_count;
                edge_count += 1;
                block->edge_count += 1;
            }
            tally.sources += block->source_count;
            // The block's edits but its splits' moves, which are counted apart; both are in its code.
            tally.transitions += block->code_count - block->source_count - (tally.split_moves - first_split_moves);
        }
    }
    if (error == TIER1_CERT_OK)
    {
        *program = (Tier1CertProgram){
            .locations = locations,
            .values = cert_values,
            .blocks = cert_blocks,
            .unit_count = units,
            .location_count = build.location_count,
            .value_count = values,
            .block_count = reachable,
            .flags_location = BUSTER_CERT_NONE,
            .features = BUSTER_CERT_AVX512F | BUSTER_CERT_AVX512DQ | BUSTER_CERT_AVX512BW | BUSTER_CERT_AVX2,
        };
        tally.edges = edge_count;
        tally.locations = build.location_count;
        tally.units = units;
        tally.bytes = (uint64_t)build.location_count * sizeof(Tier1CertLocation) + (uint64_t)values * sizeof(Tier1CertValue) + tally.facts * sizeof(Tier1CertFact) +
                      (uint64_t)source_count * sizeof(Tier1CertSource) + (uint64_t)code_count * sizeof(Tier1CertInstruction) + (uint64_t)reachable * sizeof(Tier1CertBlock) +
                      (uint64_t)edge_count * sizeof(Tier1CertEdge) + (uint64_t)rename_count * sizeof(Tier1CertRename);
        tally.arena_bytes = output->length - output_saved;
    }
    else
    {
        arena_pop_to(output, output_saved);
    }
    arena_pop_to(scratch, scratch_saved);
    if (stats != 0)
    {
        *stats = tally;
    }
    return error;
}

// Publishes the certificate of `result` and checks it: tier1_certificate_check's answer, or publication's refusal.
// Both arenas are restored.
static Tier1CertError tier1_certificate_cfg_check(const Tier1CfgInput* input, const Tier1CfgAllocation* result, Arena* output, Arena* scratch, Tier1CertReport* report, Tier1CertCfgStats* stats)
{
    const uint64_t output_saved = output->length;
    const uint64_t scratch_saved = scratch->length;
    Tier1CertProgram program = { 0 };
    Tier1CertReport checked = {
        .block = BUSTER_CERT_NONE,
        .edge = BUSTER_CERT_NONE,
        .instruction = BUSTER_CERT_NONE,
    };
    Tier1CertError error = tier1_certificate_cfg(input, result, output, scratch, &program, stats);
    checked.error = error;
    if (error == TIER1_CERT_OK)
    {
        const size_t size = tier1_certificate_scratch_size(&program);
        void* buffer = size != 0 ? arena_push(scratch, size, 64) : 0;
        error = buffer != 0 ? tier1_certificate_check(&program, buffer, size, &checked) : TIER1_CERT_CAPACITY;
        checked.error = error;
    }
    arena_pop_to(scratch, scratch_saved);
    arena_pop_to(output, output_saved);
    if (report != 0)
    {
        *report = checked;
    }
    return error;
}
