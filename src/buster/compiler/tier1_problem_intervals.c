// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Include after lane.c, tier1_allocate.c and tier1_problem.c.
// This bounded bridge targets the EXISTING interval API, not a replacement IR.
// It refuses CFGs, phis, boundary values, implicit/fixed operands, subregisters,
// ties, early clobbers and alternative forms until their production consumers
// exist. No union/intersection of different per-use masks weakens the problem.
// An imported problem is NOT a certified allocation or a quality measurement.
//
// Source, result, stats and the arena descriptor must not overlap unused capacity.
// The three mutable descriptors are mutually disjoint and do not overlap source
// tables/metadata or its descriptor; existing arena-prefix inputs remain supported.
// On failure the result/stats and entry arena length are preserved. Borrowed
// snapshot data stays immutable. Retained columns survive; temporary maps do not.

typedef struct Tier1ProblemAdapterStats Tier1ProblemAdapterStats;
struct Tier1ProblemAdapterStats
{
    uint64_t retained_bytes;
    uint64_t scratch_bytes;
    uint32_t original_instructions;
    uint32_t original_operands;
};

static Tier1ProblemReason tier1_problem_intervals(const Tier1Problem* source, Arena* arena, Tier1AllocationInput* output, Tier1ProblemAdapterStats* stats)
{
    Tier1ProblemReason result = tier1_problem_validate(source);
    if (result == TIER1_PROBLEM_OK && (arena == 0 || output == 0 || stats == 0 || arena->length > arena->capacity || arena->capacity > BUSTER_ARENA_MAX_CAPACITY || arena->base == 0))
    {
        result = TIER1_PROBLEM_ARGUMENT;
    }
    uint64_t entry = 0;
    if (result == TIER1_PROBLEM_OK)
    {
        entry = arena->length;
        const void* unused = arena->base + entry;
        const uint64_t available = arena->capacity - entry;
        if (tier1_problem_overlap(unused, available, source, sizeof(*source)) || tier1_problem_overlap(unused, available, output, sizeof(*output)) || tier1_problem_overlap(unused, available, stats, sizeof(*stats)) || tier1_problem_overlap(unused, available, arena, sizeof(*arena)))
        {
            result = TIER1_PROBLEM_ARGUMENT;
        }
        const void* mutable[3] = { arena, output, stats };
        const uint64_t sizes[3] = { sizeof(*arena), sizeof(*output), sizeof(*stats) };
        for (uint32_t a = 0; result == TIER1_PROBLEM_OK && a < 3; a += 1)
        {
            if (tier1_problem_overlap(mutable[a], sizes[a], source, sizeof(*source)))
            {
                result = TIER1_PROBLEM_ARGUMENT;
            }
            for (uint32_t b = a + 1; result == TIER1_PROBLEM_OK && b < 3; b += 1)
            {
                if (tier1_problem_overlap(mutable[a], sizes[a], mutable[b], sizes[b]))
                {
                    result = TIER1_PROBLEM_ARGUMENT;
                }
            }
            for (uint32_t m = 0; result == TIER1_PROBLEM_OK && m < BUSTER_TP_META_COUNT; m += 1)
            {
                if (tier1_problem_overlap(mutable[a], sizes[a], source->metadata[m].data, source->metadata[m].size))
                {
                    result = TIER1_PROBLEM_ARGUMENT;
                }
            }
            for (uint32_t t = 0; result == TIER1_PROBLEM_OK && t < BUSTER_TP_TABLE_COUNT; t += 1)
            {
                if (tier1_problem_overlap(mutable[a], sizes[a], source->tables[t].data, (uint64_t)source->tables[t].count * tier1_problem_widths[t] * 4))
                {
                    result = TIER1_PROBLEM_ARGUMENT;
                }
            }
        }
        for (uint32_t m = 0; result == TIER1_PROBLEM_OK && m < BUSTER_TP_META_COUNT; m += 1)
        {
            if (tier1_problem_overlap(unused, available, source->metadata[m].data, source->metadata[m].size))
            {
                result = TIER1_PROBLEM_ARGUMENT;
            }
        }
        for (uint32_t t = 0; result == TIER1_PROBLEM_OK && t < BUSTER_TP_TABLE_COUNT; t += 1)
        {
            if (tier1_problem_overlap(unused, available, source->tables[t].data, (uint64_t)source->tables[t].count * tier1_problem_widths[t] * 4))
            {
                result = TIER1_PROBLEM_ARGUMENT;
            }
        }
    }
    if (result == TIER1_PROBLEM_OK && (!tier1_problem_text_equal(source->metadata[4], "x86_64-unknown-linux-gnu") || !tier1_problem_text_equal(source->metadata[8], "sysv-x86_64")))
    {
        result = TIER1_PROBLEM_TARGET;
    }
    if (result == TIER1_PROBLEM_OK && source->tables[BUSTER_TP_PHI].count != 0)
    {
        result = TIER1_PROBLEM_PHI;
    }
    if (result == TIER1_PROBLEM_OK && (source->tables[BUSTER_TP_BLOCK].count != 1 || source->tables[BUSTER_TP_EDGE].count != 0))
    {
        result = TIER1_PROBLEM_CFG;
    }
    if (result == TIER1_PROBLEM_OK && source->tables[BUSTER_TP_FORM].count != 0)
    {
        result = TIER1_PROBLEM_FORM;
    }
    bool allocated = false;
    if (result == TIER1_PROBLEM_OK)
    {
        // Source arrays do not overlap any arena writes; descriptor counts and
        // bases are stable for this invocation. No restrict assumption is used.
        const Tier1Problem p = *source;
        const uint32_t values = p.tables[BUSTER_TP_VALUE].count;
        const uint32_t instructions = p.tables[BUSTER_TP_INSTRUCTION].count;
        const uint32_t operands = p.original_operands;
        const uint32_t class_count = p.tables[BUSTER_TP_CLASS].count;
        const uint32_t register_count = p.tables[BUSTER_TP_REGISTER].count;
        const uint32_t unit_count = p.unit_count;
        Tier1AllocationInput input = { 0 };
        Tier1ProblemAdapterStats measured = { 0 };
        uint32_t* starts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, values);
        uint32_t* ends = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, values);
        uint32_t* ids = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, values);
        uint32_t* allowed = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, values);
        uint8_t* classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, values);
        uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)values + 1);
        uint32_t* positions = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, operands);
        uint8_t* required = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, operands);
        const uint64_t retained = arena->length;
        uint32_t* masks = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, class_count);
        uint32_t* units = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, unit_count);
        uint32_t* cursors = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, values);
        allocated = true;
        if (offsets == 0 || (values != 0 && (starts == 0 || ends == 0 || ids == 0 || allowed == 0 || classes == 0 || cursors == 0)) || (operands != 0 && (positions == 0 || required == 0)) || (class_count != 0 && masks == 0) || (unit_count != 0 && units == 0))
        {
            result = TIER1_PROBLEM_CAPACITY;
        }
        uint32_t physical[64];
        for (uint32_t r = 0; r < 64; r += 1)
        {
            physical[r] = BUSTER_TP_NONE;
        }
        for (uint32_t u = 0; result == TIER1_PROBLEM_OK && u < unit_count; u += 1)
        {
            units[u] = BUSTER_TP_NONE;
        }
        // Reserving a register also reserves all its aliases. Scratch never
        // silently becomes an additional allocatable physical register. A
        // reserved row also claims its encoding, so a second, unreserved row
        // cannot reintroduce the same bank/number under disjoint alias units.
        for (uint32_t r = 0; result == TIER1_PROBLEM_OK && r < register_count; r += 1)
        {
            if ((tier1_problem_word(&p, BUSTER_TP_REGISTER, r, 6) & BUSTER_TP_RESERVED) != 0)
            {
                const uint32_t begin = tier1_problem_word(&p, BUSTER_TP_REGISTER, r, 4);
                const uint32_t count = tier1_problem_word(&p, BUSTER_TP_REGISTER, r, 5);
                for (uint32_t u = begin; u < begin + count; u += 1)
                {
                    units[tier1_problem_word(&p, BUSTER_TP_UNIT, u, 0)] = 64;
                }
                const uint32_t bank = tier1_problem_word(&p, BUSTER_TP_REGISTER, r, 1);
                const uint32_t number = tier1_problem_word(&p, BUSTER_TP_REGISTER, r, 2);
                if (bank <= 1 && number < 32)
                {
                    physical[bank * 32 + number] = r;
                }
            }
        }
        for (uint32_t c = 0; result == TIER1_PROBLEM_OK && c < class_count; c += 1)
        {
            const uint32_t bank = tier1_problem_word(&p, BUSTER_TP_CLASS, c, 1);
            const uint32_t width = tier1_problem_word(&p, BUSTER_TP_CLASS, c, 2);
            const uint32_t begin = tier1_problem_word(&p, BUSTER_TP_CLASS, c, 3);
            const uint32_t count = tier1_problem_word(&p, BUSTER_TP_CLASS, c, 4);
            uint32_t mask = 0;
            if (bank > 1 || width != (bank == 0 ? 64u : 128u))
            {
                result = TIER1_PROBLEM_CLASS;
            }
            for (uint32_t m = begin; result == TIER1_PROBLEM_OK && m < begin + count; m += 1)
            {
                const uint32_t reg = tier1_problem_word(&p, BUSTER_TP_MEMBER, m, 0);
                if ((tier1_problem_word(&p, BUSTER_TP_REGISTER, reg, 6) & BUSTER_TP_RESERVED) == 0)
                {
                    const uint32_t number = tier1_problem_word(&p, BUSTER_TP_REGISTER, reg, 2);
                    if (tier1_problem_word(&p, BUSTER_TP_REGISTER, reg, 1) != bank || tier1_problem_word(&p, BUSTER_TP_REGISTER, reg, 3) != width || number >= (bank == 0 ? 16u : 32u) || (bank == 0 && number == 4))
                    {
                        result = TIER1_PROBLEM_CLASS;
                    }
                    else
                    {
                        const uint32_t encoding = bank * 32 + number;
                        const uint32_t ub = tier1_problem_word(&p, BUSTER_TP_REGISTER, reg, 4);
                        const uint32_t uc = tier1_problem_word(&p, BUSTER_TP_REGISTER, reg, 5);
                        if (physical[encoding] != BUSTER_TP_NONE && physical[encoding] != reg)
                        {
                            result = TIER1_PROBLEM_ALIAS;
                        }
                        physical[encoding] = reg;
                        for (uint32_t u = ub; result == TIER1_PROBLEM_OK && u < ub + uc; u += 1)
                        {
                            const uint32_t unit = tier1_problem_word(&p, BUSTER_TP_UNIT, u, 0);
                            if (units[unit] != BUSTER_TP_NONE && units[unit] != encoding)
                            {
                                result = TIER1_PROBLEM_ALIAS;
                            }
                            units[unit] = encoding;
                        }
                        mask |= 1u << number;
                    }
                }
            }
            masks[c] = mask;
            if (result == TIER1_PROBLEM_OK)
            {
                input.registers[bank] |= mask;
            }
        }
        for (uint32_t v = 0; result == TIER1_PROBLEM_OK && v < values; v += 1)
        {
            const uint32_t cls = tier1_problem_word(&p, BUSTER_TP_VALUE, v, 1);
            if (tier1_problem_word(&p, BUSTER_TP_VALUE, v, 3) != 0)
            {
                result = TIER1_PROBLEM_BOUNDARY;
            }
            else if (tier1_problem_word(&p, BUSTER_TP_VALUE, v, 2) != tier1_problem_word(&p, BUSTER_TP_CLASS, cls, 2))
            {
                result = TIER1_PROBLEM_CLASS;
            }
            starts[v] = BUSTER_TP_NONE;
            ends[v] = 0;
            ids[v] = v;
            allowed[v] = masks[cls];
            classes[v] = (uint8_t)tier1_problem_word(&p, BUSTER_TP_CLASS, cls, 1);
            offsets[v] = 0;
        }
        for (uint32_t i = 0; result == TIER1_PROBLEM_OK && i < instructions; i += 1)
        {
            const uint32_t begin = tier1_problem_word(&p, BUSTER_TP_INSTRUCTION, i, 3);
            const uint32_t count = tier1_problem_word(&p, BUSTER_TP_INSTRUCTION, i, 4);
            if (tier1_problem_word(&p, BUSTER_TP_INSTRUCTION, i, 5) != 0)
            {
                result = TIER1_PROBLEM_IMPLICIT;
            }
            for (uint32_t o = begin; result == TIER1_PROBLEM_OK && o < begin + count; o += 1)
            {
                const uint32_t kind = tier1_problem_word(&p, BUSTER_TP_OPERAND, o, 0);
                const uint32_t value = tier1_problem_word(&p, BUSTER_TP_OPERAND, o, 1);
                const uint32_t phase = tier1_problem_word(&p, BUSTER_TP_OPERAND, o, 2);
                const uint32_t flags = tier1_problem_word(&p, BUSTER_TP_OPERAND, o, 3);
                const uint32_t cls = tier1_problem_word(&p, BUSTER_TP_OPERAND, o, 4);
                if (tier1_problem_word(&p, BUSTER_TP_OPERAND, o, 5) != BUSTER_TP_NONE)
                {
                    result = TIER1_PROBLEM_TIE;
                }
                else if (kind != 0 || (flags & BUSTER_TP_IMPLICIT) != 0 || tier1_problem_word(&p, BUSTER_TP_OPERAND, o, 8) != 0 || tier1_problem_word(&p, BUSTER_TP_OPERAND, o, 9) != 0)
                {
                    result = TIER1_PROBLEM_IMPLICIT;
                }
                else if (tier1_problem_word(&p, BUSTER_TP_OPERAND, o, 7) != 0)
                {
                    result = TIER1_PROBLEM_SUBREGISTER;
                }
                else if (!((flags == BUSTER_TP_READ && phase == 0) || (flags == BUSTER_TP_WRITE && phase == 3)))
                {
                    result = TIER1_PROBLEM_PHASE;
                }
                else if (tier1_problem_word(&p, BUSTER_TP_OPERAND, o, 6) != tier1_problem_word(&p, BUSTER_TP_VALUE, value, 2) || tier1_problem_word(&p, BUSTER_TP_CLASS, cls, 1) != classes[value])
                {
                    result = TIER1_PROBLEM_CLASS;
                }
                else if (masks[cls] != allowed[value])
                {
                    result = TIER1_PROBLEM_MASK_CHANGE;
                }
                if (result == TIER1_PROBLEM_OK)
                {
                    const uint32_t position = 4 * i + phase;
                    if (flags == BUSTER_TP_WRITE)
                    {
                        if (starts[value] != BUSTER_TP_NONE)
                        {
                            result = TIER1_PROBLEM_SSA;
                        }
                        starts[value] = position;
                    }
                    if (position + 1 > ends[value])
                    {
                        ends[value] = position + 1;
                    }
                    offsets[value] += 1;
                }
            }
        }
        uint32_t total = 0;
        for (uint32_t v = 0; result == TIER1_PROBLEM_OK && v < values; v += 1)
        {
            const uint32_t count = offsets[v];
            if (starts[v] == BUSTER_TP_NONE)
            {
                result = TIER1_PROBLEM_SSA;
            }
            offsets[v] = total;
            cursors[v] = total;
            total += count;
        }
        if (result == TIER1_PROBLEM_OK)
        {
            offsets[values] = total;
            if (total != operands)
            {
                result = TIER1_PROBLEM_LAYOUT;
            }
        }
        for (uint32_t i = 0; result == TIER1_PROBLEM_OK && i < instructions; i += 1)
        {
            const uint32_t begin = tier1_problem_word(&p, BUSTER_TP_INSTRUCTION, i, 3);
            const uint32_t count = tier1_problem_word(&p, BUSTER_TP_INSTRUCTION, i, 4);
            for (uint32_t o = begin; result == TIER1_PROBLEM_OK && o < begin + count; o += 1)
            {
                const uint32_t value = tier1_problem_word(&p, BUSTER_TP_OPERAND, o, 1);
                const uint32_t phase = tier1_problem_word(&p, BUSTER_TP_OPERAND, o, 2);
                const uint32_t position = 4 * i + phase;
                if (position < starts[value])
                {
                    result = TIER1_PROBLEM_SSA;
                }
                const uint32_t cursor = cursors[value]++;
                positions[cursor] = position;
                // A definition also requires a physical register at its phase.
                required[cursor] = 1;
            }
        }
        if (result == TIER1_PROBLEM_OK)
        {
            input.starts = starts;
            input.ends = ends;
            input.values = ids;
            input.allowed_registers = allowed;
            input.classes = classes;
            input.use_offsets = offsets;
            input.use_positions = positions;
            input.use_required = required;
            input.use_count = operands;
            input.count = values;
            input.value_count = values;
            input.position_count = instructions * 4;
            if (!tier1_allocation_input_valid(&input))
            {
                result = TIER1_PROBLEM_CONSTRAINT;
            }
        }
        if (result == TIER1_PROBLEM_OK)
        {
            measured.retained_bytes = retained - entry;
            measured.scratch_bytes = arena->length - retained;
            measured.original_instructions = instructions;
            measured.original_operands = operands;
            arena_pop_to(arena, retained);
            *output = input;
            *stats = measured;
        }
    }
    if (result != TIER1_PROBLEM_OK && allocated)
    {
        arena_pop_to(arena, entry);
    }
    return result;
}
