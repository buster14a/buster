// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Shared admission for scoring-only proposals and their qualified final materializer. The unchanged full
// transformer remains the independent oracle. Include after tier1_emit_region_exchange.c and recolor helpers.

static bool tier1_emit_region_visit(uint64_t amount, uint64_t* work, Tier1EmitError* error)
{
    if (*error == TIER1_EMIT_OK && (work == 0 || !tier1_emit_recolor_charge(amount, work)))
    {
        *error = TIER1_EMIT_LIMIT;
    }
    return *error == TIER1_EMIT_OK;
}

static void* tier1_emit_region_storage(Arena* output, uint64_t count, uint64_t width, uint64_t* work, Tier1EmitError* error)
{
    void* result = 0;
    if (tier1_emit_region_visit(count, work, error))
    {
        bool ok = true;
        result = ir_allocate(output, count, width, &ok);
        *error = ok ? *error : TIER1_EMIT_MEMORY;
    }
    return result;
}

typedef struct Tier1EmitRegionAdmission Tier1EmitRegionAdmission;
struct Tier1EmitRegionAdmission
{
    Tier1CfgInput input;
    Tier1CfgAllocation original;
    uint32_t* fragments;
    uint32_t offsets[17];
    uint32_t values[16];
    uint32_t intervals[16];
    Tier1EmitRegionExchangeFailure failures[16];
    uint32_t interval;
    uint32_t value;
    uint32_t first;
    uint32_t start;
    uint32_t end;
    uint32_t targets;
    uint32_t callee_used;
    uint8_t source;
};

// The six physical target plans are built in one owner/fragment pass. The exact qualified predicates apply:
// plain GPR pool, unchanged incoming binding, complete target-owner containment, no bound displaced definition,
// no fixed/sequence/scratch role, actual callee activation, and the semantic entry-minus-one convention.
static Tier1EmitError tier1_emit_region_admit(const Tier1CfgInput* input, const Tier1CfgAllocation* cfg,
                                              const uint32_t* entry_offsets, const uint32_t* entry_intervals,
                                              uint32_t interval, uint32_t callee_used, Arena* output, uint64_t* work,
                                              Tier1EmitRegionAdmission* result)
{
    const uint64_t mark = output != 0 ? output->length : 0;
    bool valid = input != 0 && cfg != 0 && output != 0 && work != 0 && result != 0 && entry_offsets != 0 && entry_intervals != 0;
    valid = valid && interval < cfg->intervals.count && cfg->split_count == 0;
    const uint32_t value = valid ? cfg->intervals.values[interval] : 0;
    const uint32_t first = valid ? cfg->allocation.fragment_offsets[interval] : 0;
    const uint32_t start = valid ? cfg->intervals.starts[interval] : 0;
    const uint32_t end = valid ? cfg->intervals.ends[interval] : 0;
    const uint8_t source = valid ? cfg->allocation.registers[first] : UINT8_MAX;
    const uint32_t source_bit = source < 16 ? 1u << source : 0;
    valid = valid && value < input->value_count && input->classes[value] == BUSTER_TIER1_ALLOCATE_GPR &&
            input->definition_registers != 0 && input->definition_registers[value] == source_bit && start > input->definitions[value] &&
            (input->rematerializable == 0 || input->rematerializable[value] == 0) &&
            cfg->allocation.fragment_offsets[interval + 1] - first == 1 && cfg->allocation.kinds[first] == BUSTER_TIER1_ALLOCATE_REGISTER &&
            cfg->allocation.starts[first] == start && cfg->allocation.ends[first] == end && source < 16 &&
            ((BUSTER_TIER1_EMIT_CFG_CALLEE >> source) & 1) == 0;
    valid = valid && (source_bit & input->registers[BUSTER_TIER1_ALLOCATE_GPR] & cfg->intervals.registers[BUSTER_TIER1_ALLOCATE_GPR]) != 0 &&
            source != input->scratch_registers[BUSTER_TIER1_ALLOCATE_GPR];
    Tier1EmitError error = valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
    Tier1EmitRegionAdmission built = { .interval = interval, .value = value, .first = first, .start = start, .end = end, .source = source, .callee_used = callee_used };
    uint32_t forbidden = 0;
    for (uint32_t f = 0; error == TIER1_EMIT_OK && f < input->fixed.count; f += 16)
    {
        const uint32_t left = input->fixed.count - f;
        const uint32_t count = left < 16 ? left : 16;
        if (tier1_emit_region_visit(count, work, &error))
        {
            const __mmask16 lanes = count == 16 ? 0xffff : (__mmask16)((1u << count) - 1);
            const __m128i classes = _mm_maskz_loadu_epi8(lanes, input->fixed.classes + f);
            const __mmask16 general = lanes & _mm_cmpeq_epi8_mask(classes, _mm_set1_epi8(BUSTER_TIER1_ALLOCATE_GPR));
            forbidden |= (uint32_t)_mm512_mask_reduce_or_epi32(general, _mm512_maskz_loadu_epi32(lanes, input->fixed.registers + f));
        }
    }
    for (uint32_t s = 0; error == TIER1_EMIT_OK && s < input->sequence_count; s += 16)
    {
        const uint32_t left = input->sequence_count - s;
        const uint32_t count = left < 16 ? left : 16;
        if (tier1_emit_region_visit(count, work, &error))
        {
            const __mmask16 lanes = count == 16 ? 0xffff : (__mmask16)((1u << count) - 1);
            forbidden |= (uint32_t)_mm512_reduce_or_epi32(_mm512_maskz_loadu_epi32(lanes, input->sequence_registers + s));
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        error = (forbidden & source_bit) == 0 ? error : TIER1_EMIT_INVALID;
        const uint32_t choices = BUSTER_TIER1_EMIT_CFG_CALLEE & callee_used & input->allowed_registers[value] & cfg->intervals.allowed_registers[interval];
        uint32_t physical = input->registers[BUSTER_TIER1_ALLOCATE_GPR] & cfg->intervals.registers[BUSTER_TIER1_ALLOCATE_GPR] & ~forbidden;
        const uint8_t scratch = input->scratch_registers[BUSTER_TIER1_ALLOCATE_GPR];
        physical &= scratch < 16 ? ~(1u << scratch) : UINT32_MAX;
        built.targets = choices & physical;
        for (uint32_t r = 0; r < 16; r += 1)
        {
            built.failures[r] = (choices & (1u << r)) == 0 ? TIER1_REGION_EXCHANGE_TARGET :
                                (physical & (1u << r)) == 0 ? TIER1_REGION_EXCHANGE_HARD_ROLE : TIER1_REGION_EXCHANGE_NONE;
        }
    }
    const uint32_t fragments = error == TIER1_EMIT_OK ? cfg->allocation.fragment_count : 0;
    uint64_t* records = error == TIER1_EMIT_OK ? tier1_emit_region_storage(output, (uint64_t)fragments + 1, 8, work, &error) : 0;
    uint16_t* seen = error == TIER1_EMIT_OK ? tier1_emit_region_storage(output, (uint64_t)input->value_count + 1, 2, work, &error) : 0;
    built.fragments = error == TIER1_EMIT_OK ? tier1_emit_region_storage(output, (uint64_t)fragments + 1, 4, work, &error) : 0;
    const uint32_t choices = built.targets;
    uint32_t used = 0;
    uint32_t count = 0;
    for (uint32_t i = 0; error == TIER1_EMIT_OK && i < cfg->intervals.count; i += 1)
    {
        if (tier1_emit_region_visit(1, work, &error) && cfg->intervals.classes[i] == BUSTER_TIER1_ALLOCATE_GPR)
        {
            const uint32_t owner = cfg->intervals.values[i];
            const uint32_t begin = cfg->intervals.starts[i];
            const uint32_t past = cfg->intervals.ends[i];
            const bool contained = start <= begin && past <= end;
            const bool bound = input->definition_registers[owner] != 0 && begin <= input->definitions[owner] && input->definitions[owner] < past;
            const uint32_t last = cfg->allocation.fragment_offsets[i + 1];
            uint32_t held = 0;
            for (uint32_t f = cfg->allocation.fragment_offsets[i]; error == TIER1_EMIT_OK && f < last; f += 16)
            {
                const uint32_t left = last - f;
                const uint32_t width = left < 16 ? left : 16;
                if (tier1_emit_region_visit(width, work, &error))
                {
                    const __mmask16 lanes = width == 16 ? 0xffff : (__mmask16)((1u << width) - 1);
                    const __m128i kinds = _mm_maskz_loadu_epi8(lanes, cfg->allocation.kinds + f);
                    const __m512i regs = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(lanes, cfg->allocation.registers + f));
                    const __m512i bits = _mm512_sllv_epi32(_mm512_set1_epi32(1), regs);
                    const __mmask16 general = lanes & _mm_cmpeq_epi8_mask(kinds, _mm_set1_epi8(BUSTER_TIER1_ALLOCATE_REGISTER));
                    used |= (uint32_t)_mm512_mask_reduce_or_epi32(general, bits);
                    const __m512i firsts = _mm512_maskz_loadu_epi32(lanes, cfg->allocation.starts + f);
                    const __m512i ends = _mm512_maskz_loadu_epi32(lanes, cfg->allocation.ends + f);
                    const __mmask16 overlap = _mm512_cmplt_epu32_mask(firsts, _mm512_set1_epi32((int)end)) &
                                              _mm512_cmpgt_epu32_mask(ends, _mm512_set1_epi32((int)start));
                    const __mmask16 chosen = general & overlap & _mm512_test_epi32_mask(bits, _mm512_set1_epi32((int)choices));
                    held |= (uint32_t)_mm512_mask_reduce_or_epi32(chosen, bits);
                    for (uint32_t active = contained && !bound ? chosen : 0; active != 0; active &= active - 1)
                    {
                        const uint32_t at = f + (uint32_t)__builtin_ctz(active);
                        const uint32_t reg = cfg->allocation.registers[at];
                        records[count++] = (uint64_t)reg << 32 | at;
                        built.offsets[reg + 1] += 1;
                    }
                }
            }
            if (error == TIER1_EMIT_OK && held != 0 && (!contained || bound))
            {
                for (uint32_t active = held & built.targets; active != 0; active &= active - 1)
                {
                    const uint32_t reg = (uint32_t)__builtin_ctz(active);
                    built.failures[reg] = !contained ? TIER1_REGION_EXCHANGE_CONTAINMENT : TIER1_REGION_EXCHANGE_BINDING;
                }
                built.targets &= ~held;
            }
            else if (error == TIER1_EMIT_OK && held != 0)
            {
                const uint32_t fresh = held & ~seen[owner];
                seen[owner] |= (uint16_t)held;
                for (uint32_t active = held; active != 0; active &= active - 1)
                {
                    const uint32_t reg = (uint32_t)__builtin_ctz(active);
                    built.intervals[reg] += 1;
                    built.values[reg] += (fresh >> reg) & 1;
                }
            }
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        for (uint32_t active = built.targets & ~used; active != 0; active &= active - 1)
        {
            built.failures[__builtin_ctz(active)] = TIER1_REGION_EXCHANGE_TARGET;
        }
        built.targets &= used;
        // A continued owner's entry convention can still use its prior REGISTER fragment, even when its first
        // current fragment is SPILL. A wholly contained owner cannot have that outside-prefix convention.
        const uint32_t block = tier1_cfg_block_at(input, cfg, start);
        for (uint32_t e = entry_offsets[block]; error == TIER1_EMIT_OK && input->block_starts[block] == start && e < entry_offsets[block + 1]; e += 1)
        {
            if (tier1_emit_region_visit(1, work, &error))
            {
                const uint32_t i = entry_intervals[e];
                const uint32_t owner = cfg->intervals.values[i];
                const uint32_t begin = cfg->intervals.starts[i];
                const bool contained = start <= begin && cfg->intervals.ends[i] <= end;
                if (i != interval && !contained && cfg->intervals.classes[i] == BUSTER_TIER1_ALLOCATE_GPR)
                {
                    uint32_t location = BUSTER_TIER1_CFG_NONE;
                    uint8_t kind = UINT8_MAX;
                    const bool found = tier1_cfg_interval_entry(cfg, owner, i, begin < start ? start - 1 : start, start, &location, &kind);
                    if (found && kind == BUSTER_TIER1_ALLOCATE_REGISTER && location < 16 && (built.targets & (1u << location)) != 0)
                    {
                        built.targets &= ~(1u << location);
                        built.failures[location] = TIER1_REGION_EXCHANGE_ENTRY;
                    }
                }
            }
        }
    }
    uint32_t cursor[16];
    for (uint32_t r = 0; error == TIER1_EMIT_OK && r < 16; r += 1)
    {
        built.offsets[r + 1] += built.offsets[r];
        cursor[r] = built.offsets[r];
    }
    if (error == TIER1_EMIT_OK && tier1_emit_region_visit(count, work, &error))
    {
        for (uint32_t f = 0; f < count; f += 1)
        {
            const uint32_t reg = (uint32_t)(records[f] >> 32);
            built.fragments[cursor[reg]++] = (uint32_t)records[f];
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        built.input = *input;
        built.original = *cfg;
        *result = built;
    }
    else if (output != 0)
    {
        arena_pop_to(output, mark);
    }
    return error;
}

static Tier1EmitError tier1_emit_region_admitted(const Tier1EmitRegionAdmission* admission, uint8_t target, Tier1EmitRegionExchangeStats* stats)
{
    const bool valid = target < 16 && ((admission->targets >> target) & 1) != 0;
    if (stats != 0)
    {
        *stats = (Tier1EmitRegionExchangeStats){
            .edges = { .value = admission->value, .interval = admission->interval, .source = admission->source, .target = target },
            .displaced_values = valid ? admission->values[target] : 0,
            .displaced_intervals = valid ? admission->intervals[target] : 0,
            .displaced_fragments = valid ? admission->offsets[target + 1] - admission->offsets[target] : 0,
            .failure_reason = valid ? TIER1_REGION_EXCHANGE_NONE : target < 16 ? admission->failures[target] : TIER1_REGION_EXCHANGE_ARGUMENT,
        };
    }
    return valid ? TIER1_EMIT_OK : TIER1_EMIT_INVALID;
}
