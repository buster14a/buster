// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Include after tier1_allocate.c and tier1_certificate.c. This adapter checks
// the current interval allocator's published fragments, NOT emitted code.
// Numeric positions contain no CFG, source operations, widths or actual edits.
// The stream checker must be used separately once a producer supplies those.
//
// Independent linear event sweep: all half-open ends precede starts. Fixed
// ranges are reference-counted, so overlapping reservations remain legal.
// Work: O(positions + intervals + fragments + uses + fixed register updates).

typedef struct Tier1IntervalReport Tier1IntervalReport;
struct Tier1IntervalReport
{
    Tier1CertError error;
    uint64_t positions;
    uint64_t intervals;
    uint64_t fragments;
    uint64_t uses;
    uint64_t fixed;
    uint64_t events;
    uint64_t register_updates;
    uint64_t certificate_bytes;
    uint64_t scratch_bytes;
};

typedef struct Tier1IntervalEvent Tier1IntervalEvent;
struct Tier1IntervalEvent
{
    uint32_t next;
    uint32_t index;
    uint8_t kind;
};

static size_t tier1_interval_scratch_size(const Tier1AllocationInput* input, const Tier1Allocation* allocation)
{
    size_t result = 0;
    if (input != 0 && allocation != 0)
    {
        const uint64_t events = 2 * ((uint64_t)input->count + allocation->fragment_count + input->fixed.count);
        const uint64_t size = 8 * ((uint64_t)input->position_count + 1) + events * sizeof(Tier1IntervalEvent) + (uint64_t)allocation->fragment_count * 4 + (uint64_t)input->value_count * 5 + 128;
        result = size <= SIZE_MAX ? (size_t)size : 0;
    }
    return result;
}

static Tier1CertError tier1_allocation_certificate_check(const Tier1AllocationInput* input, const Tier1Allocation* allocation, void* scratch, size_t capacity, Tier1IntervalReport* report)
{
    Tier1IntervalReport result = {
        .error = TIER1_CERT_INVALID,
    };
    bool valid = report != 0 && scratch != 0 && allocation != 0 && tier1_allocation_input_valid(input);
    if (valid)
    {
        valid = allocation->interval_count == input->count && allocation->fragment_count <= (uint64_t)input->count * 2 + (uint64_t)input->use_count * 2 && allocation->spill_count <= input->value_count;
        valid = valid && allocation->fragment_offsets != 0 && allocation->fragment_offsets[0] == 0 && allocation->fragment_offsets[input->count] == allocation->fragment_count;
        valid = valid && (allocation->fragment_count == 0 || (allocation->starts != 0 && allocation->ends != 0 && allocation->kinds != 0 && allocation->registers != 0));
        valid = valid && (input->value_count == 0 || (allocation->spill_ids != 0 && allocation->spill_values != 0 && allocation->value_registers != 0));
    }
    if (valid)
    {
        // Inputs are borrowed immutable columns; scratch is disjoint. Snapshot
        // the descriptors once rather than relying on type-based aliasing.
        const Tier1AllocationInput in = *input;
        const Tier1Allocation out = *allocation;
        const uint32_t positions = in.position_count + 1;
        const uint32_t event_count = 2 * (in.count + out.fragment_count + in.fixed.count);
        Tier1CertMemory memory = {
            .data = scratch,
            .capacity = capacity,
        };
        uint32_t* ends = tier1_cert_take(&memory, positions, sizeof(uint32_t));
        uint32_t* starts = tier1_cert_take(&memory, positions, sizeof(uint32_t));
        Tier1IntervalEvent* events = tier1_cert_take(&memory, event_count, sizeof(Tier1IntervalEvent));
        uint32_t* owners = tier1_cert_take(&memory, out.fragment_count, sizeof(uint32_t));
        uint32_t* active_values = tier1_cert_take(&memory, in.value_count, sizeof(uint32_t));
        uint8_t* classes = tier1_cert_take(&memory, in.value_count, sizeof(uint8_t));
        result.scratch_bytes = memory.used;
        result.error = TIER1_CERT_CAPACITY;
        valid = !memory.failed;
        if (valid)
        {
            memset(ends, 0xff, (size_t)positions * sizeof(uint32_t));
            memset(starts, 0xff, (size_t)positions * sizeof(uint32_t));
            memset(active_values, 0xff, (size_t)in.value_count * sizeof(uint32_t));
            memset(classes, 0xff, in.value_count);
            result.error = TIER1_CERT_INVALID;
        }
        uint32_t cursor = 0;
        for (uint32_t i = 0; valid && i < in.count; i += 1)
        {
            const uint32_t begin = out.fragment_offsets[i];
            const uint32_t end = out.fragment_offsets[i + 1];
            const uint32_t value = in.values[i];
            valid = begin < end && end <= out.fragment_count;
            // Every interval of one value must share its class; the per-value
            // class column below drives the register-occupancy sweep. This is
            // stricter than tier1_allocation_input_valid, which leaves the
            // per-value class unrestricted, so mixed-class inputs fail here.
            valid = valid && (classes[value] == UINT8_MAX || classes[value] == in.classes[i]);
            classes[value] = in.classes[i];
            uint32_t position = in.starts[i];
            for (uint32_t f = begin; valid && f < end; f += 1)
            {
                const uint8_t kind = out.kinds[f];
                const uint32_t reg = out.registers[f];
                valid = out.starts[f] == position && position < out.ends[f] && out.ends[f] <= in.ends[i] && kind <= BUSTER_TIER1_ALLOCATE_REMATERIALIZE;
                position = out.ends[f];
                const uint32_t direct = out.value_registers[value];
                valid = valid && (direct == UINT8_MAX || (kind == BUSTER_TIER1_ALLOCATE_REGISTER && direct == reg));
                if (valid && kind == BUSTER_TIER1_ALLOCATE_REGISTER)
                {
                    valid = reg < 32 && ((in.allowed_registers[i] >> reg) & 1) != 0;
                }
                else if (valid)
                {
                    valid = reg == UINT8_MAX && (in.flags == 0 || (in.flags[i] & BUSTER_TIER1_ALLOCATE_REQUIRED) == 0);
                    if (kind == BUSTER_TIER1_ALLOCATE_SPILL)
                    {
                        const uint32_t slot = out.spill_ids[value];
                        valid = valid && slot < out.spill_count && out.spill_values[slot] == value;
                    }
                    else
                    {
                        valid = valid && in.rematerializable != 0 && in.rematerializable[i] != 0;
                    }
                }
                if (valid)
                {
                    owners[f] = i;
                    events[cursor] = (Tier1IntervalEvent){
                        .next = starts[out.starts[f]],
                        .index = f,
                        .kind = 0,
                    };
                    starts[out.starts[f]] = cursor++;
                    events[cursor] = (Tier1IntervalEvent){
                        .next = ends[out.ends[f]],
                        .index = f,
                        .kind = 0,
                    };
                    ends[out.ends[f]] = cursor++;
                }
            }
            valid = valid && position == in.ends[i];
            if (valid && in.use_required != 0)
            {
                uint32_t f = begin;
                for (uint32_t u = in.use_offsets[i]; valid && u < in.use_offsets[i + 1]; u += 1)
                {
                    const uint32_t use = in.use_positions[u];
                    while (f < end && use >= out.ends[f])
                    {
                        f += 1;
                    }
                    valid = f < end && (in.use_required[u] == 0 || out.kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER);
                }
            }
            if (valid)
            {
                events[cursor] = (Tier1IntervalEvent){
                    .next = starts[in.starts[i]],
                    .index = i,
                    .kind = 1,
                };
                starts[in.starts[i]] = cursor++;
                events[cursor] = (Tier1IntervalEvent){
                    .next = ends[in.ends[i]],
                    .index = i,
                    .kind = 1,
                };
                ends[in.ends[i]] = cursor++;
            }
        }
        for (uint32_t f = 0; valid && f < in.fixed.count; f += 1)
        {
            events[cursor] = (Tier1IntervalEvent){
                .next = starts[in.fixed.starts[f]],
                .index = f,
                .kind = 2,
            };
            starts[in.fixed.starts[f]] = cursor++;
            events[cursor] = (Tier1IntervalEvent){
                .next = ends[in.fixed.ends[f]],
                .index = f,
                .kind = 2,
            };
            ends[in.fixed.ends[f]] = cursor++;
        }
        // A register's owner and its fixed ranges' count, per class: general, vector and opmask (#179).
        uint32_t registers[BUSTER_TIER1_ALLOCATE_CLASSES][32];
        uint32_t reserved[BUSTER_TIER1_ALLOCATE_CLASSES][32] = { { 0 } };
        memset(registers, 0xff, sizeof(registers));
        for (uint32_t p = 0; valid && p < positions; p += 1)
        {
            for (uint32_t phase = 0; valid && phase < 2; phase += 1)
            {
                uint32_t event = phase == 0 ? ends[p] : starts[p];
                while (valid && event != UINT32_MAX)
                {
                    const Tier1IntervalEvent item = events[event];
                    const uint32_t i = item.index;
                    if (item.kind == 0 && out.kinds[i] == BUSTER_TIER1_ALLOCATE_REGISTER)
                    {
                        const uint32_t class = in.classes[owners[i]];
                        const uint32_t reg = out.registers[i];
                        if (phase == 0)
                        {
                            valid = registers[class][reg] == i;
                            registers[class][reg] = UINT32_MAX;
                        }
                        else
                        {
                            valid = registers[class][reg] == UINT32_MAX && reserved[class][reg] == 0;
                            registers[class][reg] = i;
                        }
                        result.register_updates += 1;
                    }
                    else if (item.kind == 1)
                    {
                        const uint32_t value = in.values[i];
                        valid = active_values[value] == (phase == 0 ? i : UINT32_MAX);
                        active_values[value] = phase == 0 ? UINT32_MAX : i;
                    }
                    else if (item.kind == 2)
                    {
                        const uint32_t class = in.fixed.classes[i];
                        uint32_t mask = in.fixed.registers[i];
                        while (valid && mask != 0)
                        {
                            const uint32_t reg = (uint32_t)__builtin_ctz(mask);
                            if (phase == 0)
                            {
                                valid = reserved[class][reg] != 0;
                                reserved[class][reg] -= 1;
                            }
                            else
                            {
                                valid = registers[class][reg] == UINT32_MAX;
                                reserved[class][reg] += 1;
                            }
                            result.register_updates += 1;
                            mask &= mask - 1;
                        }
                    }
                    event = item.next;
                }
            }
        }
        for (uint32_t s = 0; valid && s < out.spill_count; s += 1)
        {
            const uint32_t value = out.spill_values[s];
            valid = value < in.value_count && out.spill_ids[value] == s;
        }
        for (uint32_t v = 0; valid && v < in.value_count; v += 1)
        {
            const uint32_t slot = out.spill_ids[v];
            valid = slot == UINT32_MAX || (slot < out.spill_count && out.spill_values[slot] == v);
        }
        result.positions = in.position_count;
        result.intervals = in.count;
        result.fragments = out.fragment_count;
        result.uses = in.use_count;
        result.fixed = in.fixed.count;
        result.events = cursor;
        result.certificate_bytes = 4 * ((uint64_t)in.count + 1) + 10 * (uint64_t)out.fragment_count + 5 * (uint64_t)in.value_count + 4 * (uint64_t)out.spill_count;
    }
    if (valid)
    {
        result.error = TIER1_CERT_OK;
    }
    if (report != 0)
    {
        *report = result;
    }
    return result.error;
}
