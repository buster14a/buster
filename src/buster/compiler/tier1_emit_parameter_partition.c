// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Measurement-only allocation normalization for #2610. A bound parameter's complete register interval may span
// a call block and a call-free bypass. Partition it at CFG entries after allocation, without changing any physical
// location or transport program, so a later regional policy can choose the call block alone. The new columns become
// immutable before any epoch cache is prepared. This pass neither allocates registers nor widens an allowed mask.

#define BUSTER_TIER1_PARTITION_WORK (UINT64_C(1) << 26)

// The owner maps repeat an old index for each new child and otherwise advance by one. Sixteen consecutive children
// therefore address at most sixteen consecutive source bytes; expand that bounded window before permuting it.
static uint8_t* tier1_emit_partition_bytes(const uint8_t* source, const uint32_t* owners, uint32_t count, Arena* output, bool* ok)
{
    uint8_t* result = source != 0 && *ok ? ir_allocate(output, (uint64_t)count + 1, 1, ok) : 0;
    for (uint32_t i = 0; result != 0 && i < count; i += 16)
    {
        const uint32_t left = count - i;
        const uint32_t lanes = left < 16 ? left : 16;
        const __mmask16 mask = (__mmask16)((1u << lanes) - 1);
        const uint32_t first = owners[i];
        const uint32_t span = owners[i + lanes - 1] - first + 1;
        const __mmask16 readable = (__mmask16)((1u << span) - 1);
        const __m512i indices = _mm512_sub_epi32(_mm512_maskz_loadu_epi32(mask, owners + i), _mm512_set1_epi32((int)first));
        const __m512i values = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(readable, source + first));
        _mm_mask_storeu_epi8(result + i, mask, _mm512_cvtepi32_epi8(_mm512_permutexvar_epi32(indices, values)));
    }
    if (result != 0)
    {
        result[count] = 0;
    }
    return result;
}

static uint32_t* tier1_emit_partition_words(const uint32_t* source, const uint32_t* owners, uint32_t count, Arena* output, bool* ok)
{
    uint32_t* result = source != 0 && *ok ? ir_allocate(output, (uint64_t)count + 1, 4, ok) : 0;
    for (uint32_t i = 0; result != 0 && i < count; i += 16)
    {
        const uint32_t left = count - i;
        const __mmask16 lanes = left < 16 ? (__mmask16)((1u << left) - 1) : 0xffff;
        const __m512i indices = _mm512_maskz_loadu_epi32(lanes, owners + i);
        _mm512_mask_storeu_epi32(result + i, lanes, _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), lanes, indices, source, 4));
    }
    if (result != 0)
    {
        result[count] = 0;
    }
    return result;
}

// Hint references name either the opening child or (with EXIT) the closing child. NONE includes the high bit too,
// so exclude it before gathering. Alternate and continuation columns have no EXIT tag and use the opening child.
static uint32_t* tier1_emit_partition_references(const uint32_t* source, const uint32_t* owners, const uint32_t* offsets,
                                                uint32_t count, Arena* output, bool* ok)
{
    uint32_t* result = tier1_emit_partition_words(source, owners, count, output, ok);
    for (uint32_t i = 0; result != 0 && i < count; i += 16)
    {
        const uint32_t left = count - i;
        const __mmask16 lanes = left < 16 ? (__mmask16)((1u << left) - 1) : 0xffff;
        const __m512i named = _mm512_maskz_loadu_epi32(lanes, result + i);
        const __mmask16 present = lanes & _mm512_cmpneq_epi32_mask(named, _mm512_set1_epi32(-1));
        const __mmask16 closing = present & _mm512_test_epi32_mask(named, _mm512_set1_epi32(BUSTER_TIER1_ALLOCATE_HINT_EXIT));
        const __m512i indices = _mm512_and_epi32(named, _mm512_set1_epi32(~BUSTER_TIER1_ALLOCATE_HINT_EXIT));
        const __m512i take = _mm512_mask_add_epi32(indices, closing, indices, _mm512_set1_epi32(1));
        __m512i mapped = _mm512_mask_i32gather_epi32(_mm512_set1_epi32(-1), present, take, offsets, 4);
        mapped = _mm512_mask_sub_epi32(mapped, closing, mapped, _mm512_set1_epi32(1));
        mapped = _mm512_mask_or_epi32(mapped, closing, mapped, _mm512_set1_epi32(BUSTER_TIER1_ALLOCATE_HINT_EXIT));
        _mm512_mask_storeu_epi32(result + i, lanes, mapped);
    }
    return result;
}

static bool tier1_emit_partition_parameters(const Tier1CfgInput* input, const uint8_t* parameters, Arena* output,
                                             Arena* scratch, Tier1CfgAllocation* cfg)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const Tier1CfgAllocation original = *cfg;
    const Tier1AllocationInput* intervals = &original.intervals;
    const Tier1Allocation* allocation = &original.allocation;
    bool ok = output != scratch && parameters != 0 && input->definition_registers != 0 && input->block_count != 0 &&
              (uint64_t)intervals->count + allocation->fragment_count + input->value_count + input->block_count <= BUSTER_TIER1_PARTITION_WORK;
    uint32_t* offsets = ok ? ir_allocate(scratch, (uint64_t)intervals->count + 1, 4, &ok) : 0;
    uint64_t count = 0;
    // The position columns belong to the producer's two-phase instruction model. An odd cut could alter a dying
    // operand/result alias; even CFG entries instead have exactly the same register at entry and entry-minus-one.
    for (uint32_t b = 0; ok && b < input->block_count; b += 16)
    {
        const uint32_t left = input->block_count - b;
        const __mmask16 lanes = left < 16 ? (__mmask16)((1u << left) - 1) : 0xffff;
        const __m512i starts = _mm512_maskz_loadu_epi32(lanes, input->block_starts + b);
        ok = (lanes & _mm512_test_epi32_mask(starts, _mm512_set1_epi32(1))) == 0;
    }
    for (uint32_t i = 0; ok && i < intervals->count; i += 1)
    {
        const uint32_t v = intervals->values[i];
        const uint32_t f = allocation->fragment_offsets[i];
        const uint32_t first = tier1_cfg_block_at(input, &original, intervals->starts[i]);
        const uint32_t last = tier1_cfg_block_at(input, &original, intervals->ends[i] - 1);
        const bool split = parameters[v] != 0 && intervals->classes[i] == BUSTER_TIER1_ALLOCATE_GPR &&
                           input->definition_registers[v] != 0 && intervals->starts[i] > input->definitions[v] &&
                           allocation->fragment_offsets[i + 1] == f + 1 && allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER &&
                           allocation->starts[f] == intervals->starts[i] && allocation->ends[f] == intervals->ends[i] &&
                           (intervals->rematerializable == 0 || intervals->rematerializable[i] == 0);
        offsets[i] = (uint32_t)count;
        count += split ? (uint64_t)last - first + 1 : 1;
        ok = count <= BUSTER_TIER1_ALLOCATE_MAX_INTERVALS;
    }
    if (ok)
    {
        offsets[intervals->count] = (uint32_t)count;
    }
    const uint64_t fragments = allocation->fragment_count + count - intervals->count;
    // The lookup helper retains at most BLOCK_INTERVALS entries. Its chosen rows cost at most that many block
    // visits plus the intervals' intra-block pieces; sorting its value keys is bounded by the allocator's value cap.
    const uint64_t work = count * 64 + fragments * 16 + (uint64_t)input->value_count * 24 +
                          input->use_count + BUSTER_TIER1_CFG_BLOCK_INTERVALS * UINT64_C(8);
    ok = ok && fragments <= UINT32_MAX && work <= BUSTER_TIER1_PARTITION_WORK;
    Tier1CfgAllocation built = original;
    if (ok && count != intervals->count)
    {
        uint32_t* owners = ir_allocate(scratch, count + 1, 4, &ok);
        uint32_t* fragment_owners = ok ? ir_allocate(scratch, fragments + 1, 4, &ok) : 0;
        uint32_t* starts = ok ? ir_allocate(output, count + 1, 4, &ok) : 0;
        uint32_t* ends = ok ? ir_allocate(output, count + 1, 4, &ok) : 0;
        uint32_t* uses = ok && intervals->use_offsets != 0 ? ir_allocate(output, count + 1, 4, &ok) : 0;
        built.value_interval_offsets = ok ? ir_allocate(output, (uint64_t)input->value_count + 1, 4, &ok) : 0;
        built.allocation.fragment_offsets = ok ? ir_allocate(output, count + 1, 4, &ok) : 0;
        built.allocation.starts = ok ? ir_allocate(output, fragments + 1, 4, &ok) : 0;
        built.allocation.ends = ok ? ir_allocate(output, fragments + 1, 4, &ok) : 0;
        uint32_t next_fragment = 0;
        // As in the CFG constructor, each value's ordered ranges and use cursor are variable-length runs. Physical
        // placement is copied here; the repeated metadata columns below use vector gathers and bounded byte windows.
        for (uint32_t i = 0; ok && i < intervals->count; i += 1)
        {
            const uint32_t first = allocation->fragment_offsets[i];
            const uint32_t past = allocation->fragment_offsets[i + 1];
            const uint32_t block = tier1_cfg_block_at(input, &original, intervals->starts[i]);
            uint32_t use = uses != 0 ? intervals->use_offsets[i] : 0;
            for (uint32_t j = offsets[i]; j < offsets[i + 1]; j += 1)
            {
                owners[j] = i;
                starts[j] = j == offsets[i] ? intervals->starts[i] : input->block_starts[block + j - offsets[i]];
                ends[j] = j + 1 == offsets[i + 1] ? intervals->ends[i] : input->block_starts[block + j - offsets[i] + 1];
                if (uses != 0)
                {
                    uses[j] = use;
                    while (use < intervals->use_offsets[i + 1] && intervals->use_positions[use] < ends[j])
                    {
                        use += 1;
                    }
                }
                built.allocation.fragment_offsets[j] = next_fragment;
                for (uint32_t f = first; f < past; f += 1)
                {
                    fragment_owners[next_fragment] = f;
                    built.allocation.starts[next_fragment] = offsets[i + 1] == offsets[i] + 1 ? allocation->starts[f] : starts[j];
                    built.allocation.ends[next_fragment] = offsets[i + 1] == offsets[i] + 1 ? allocation->ends[f] : ends[j];
                    next_fragment += 1;
                }
            }
        }
        if (ok)
        {
            starts[count] = 0;
            ends[count] = 0;
            built.allocation.starts[fragments] = 0;
            built.allocation.ends[fragments] = 0;
            built.allocation.fragment_offsets[count] = next_fragment;
            if (uses != 0)
            {
                uses[count] = intervals->use_count;
            }
            for (uint32_t v = 0; v <= input->value_count; v += 1)
            {
                built.value_interval_offsets[v] = offsets[original.value_interval_offsets[v]];
            }
            built.intervals.starts = starts;
            built.intervals.ends = ends;
            built.intervals.use_offsets = uses;
            built.intervals.count = (uint32_t)count;
            built.allocation.interval_count = (uint32_t)count;
            built.allocation.fragment_count = (uint32_t)fragments;
            built.intervals.values = tier1_emit_partition_words(intervals->values, owners, (uint32_t)count, output, &ok);
            built.intervals.allowed_registers = tier1_emit_partition_words(intervals->allowed_registers, owners, (uint32_t)count, output, &ok);
            built.intervals.classes = tier1_emit_partition_bytes(intervals->classes, owners, (uint32_t)count, output, &ok);
            built.intervals.flags = tier1_emit_partition_bytes(intervals->flags, owners, (uint32_t)count, output, &ok);
            built.intervals.hints = tier1_emit_partition_bytes(intervals->hints, owners, (uint32_t)count, output, &ok);
            built.intervals.hint_values = tier1_emit_partition_words(intervals->hint_values, owners, (uint32_t)count, output, &ok);
            built.intervals.hint_intervals = tier1_emit_partition_references(intervals->hint_intervals, owners, offsets, (uint32_t)count, output, &ok);
            built.intervals.alternate_intervals = tier1_emit_partition_references(intervals->alternate_intervals, owners, offsets, (uint32_t)count, output, &ok);
            built.intervals.continuation_intervals = tier1_emit_partition_references(intervals->continuation_intervals, owners, offsets, (uint32_t)count, output, &ok);
            built.intervals.register_hints = tier1_emit_partition_bytes(intervals->register_hints, owners, (uint32_t)count, output, &ok);
            built.intervals.avoided_registers = tier1_emit_partition_words(intervals->avoided_registers, owners, (uint32_t)count, output, &ok);
            built.intervals.rematerializable = tier1_emit_partition_bytes(intervals->rematerializable, owners, (uint32_t)count, output, &ok);
            built.unread_entries = tier1_emit_partition_bytes(original.unread_entries, owners, (uint32_t)count, output, &ok);
            built.allocation.kinds = tier1_emit_partition_bytes(allocation->kinds, fragment_owners, (uint32_t)fragments, output, &ok);
            built.allocation.registers = tier1_emit_partition_bytes(allocation->registers, fragment_owners, (uint32_t)fragments, output, &ok);
        }
        if (ok)
        {
            built.value_block_base = 0;
            built.value_block_intervals = 0;
            tier1_cfg_install_lookup(input, output, scratch, &built);
            built.retained_bytes += output->length - output_mark;
            *cfg = built;
        }
    }
    if (!ok)
    {
        arena_pop_to(output, output_mark);
    }
    arena_pop_to(scratch, scratch_mark);
    return ok;
}

static void tier1_emit_cfg_partition_parameters(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted,
                                                Arena* output, Arena* scratch, const Tier1CfgInput* input, Tier1CfgAllocation* cfg)
{
    const uint64_t mark = scratch->length;
    bool ok = true;
    uint8_t* parameters = ir_allocate(scratch, (uint64_t)input->value_count + 1, 1, &ok);
    if (ok)
    {
        memset(parameters, 0, (uint64_t)input->value_count + 1);
        for (uint32_t node = 0; node < plan->function->block_offsets[1]; node += 1)
        {
            if (tier1_select_entry_parameter(plan->function, node))
            {
                const uint32_t value = emitted->node_values[node];
                const Tier1SelectPlace place = tier1_select_parameter_place(plan->function, plan->function->a[node]);
                if (value != BUSTER_TIER1_CFG_NONE && place.kind == BUSTER_TIER1_PLACE_GENERAL)
                {
                    parameters[value] = 1;
                }
            }
        }
        const uint32_t before = cfg->intervals.count;
        ok = tier1_emit_partition_parameters(input, parameters, output, scratch, cfg);
#if defined(BUSTER_TIER1_MEASURE_KINDS)
        const char* trace_env = getenv("BUSTER_TIER1_EXCHANGE_TRACE");
        if (trace_env != 0 && (uint32_t)strtoul(trace_env, 0, 10) == plan->function->instruction_count)
        {
            fprintf(stderr, "PARAMETER_PARTITION instructions=%u supported=%u before=%u after=%u\n", plan->function->instruction_count, ok, before, cfg->intervals.count);
        }
#else
        (void)before;
#endif
    }
    arena_pop_to(scratch, mark);
}
