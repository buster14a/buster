// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Fixed event traces for the stopgap's end-event ownership and required-use paths. Include after the allocation
// test helpers. Expected fragments are literal fixtures, not a replay of either allocator's event algorithm.

typedef struct Tier1AllocationExpiryCase Tier1AllocationExpiryCase;
struct Tier1AllocationExpiryCase
{
    Tier1AllocationInput input;
    Tier1Allocation expected;
    uint32_t minimum_capacity[2];
    const char* name;
};

static bool tier1_allocation_test_expiry_equal(const Tier1AllocationInput* input, const Tier1Allocation* a, const Tier1Allocation* b, bool scratch)
{
    bool same = tier1_allocation_test_equal(input, a, b) && a->interval_count == b->interval_count && a->splits == b->splits;
    same = same && memcmp(a->peak_pressure, b->peak_pressure, sizeof(a->peak_pressure)) == 0 && a->retained_bytes == b->retained_bytes;
    same = same && (!scratch || a->scratch_bytes == b->scratch_bytes);
    return same;
}

// Stores happen before loads at a shared position. Check the actual value at every operand, including memory
// operands and duplicate required uses; a reload must transfer the spilled value into its new physical register.
static bool tier1_allocation_test_expiry_values(const Tier1AllocationInput* input, const Tier1Allocation* allocation)
{
    uint64_t registers[32] = { 0 };
    uint64_t slots[4] = { 0 };
    const uint64_t values[] = { UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210), UINT64_C(0x13579bdf2468ace0), UINT64_C(0xcafebabedeadbeef) };
    bool valid = input->count <= 4 && input->value_count <= 4 && allocation->spill_count <= 4;
    for (uint32_t position = 0; valid && position < input->position_count; position += 1)
    {
        for (uint32_t interval = 0; valid && interval < input->count; interval += 1)
        {
            for (uint32_t f = allocation->fragment_offsets[interval]; valid && f < allocation->fragment_offsets[interval + 1]; f += 1)
            {
                if (allocation->starts[f] == position && allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_SPILL)
                {
                    const uint32_t slot = allocation->spill_ids[input->values[interval]];
                    valid = slot < 4;
                    if (valid)
                    {
                        slots[slot] = position == input->starts[interval] ? values[input->values[interval]] : registers[allocation->registers[f - 1]];
                    }
                }
            }
        }
        for (uint32_t interval = 0; valid && interval < input->count; interval += 1)
        {
            for (uint32_t f = allocation->fragment_offsets[interval]; valid && f < allocation->fragment_offsets[interval + 1]; f += 1)
            {
                if (allocation->starts[f] == position && allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER)
                {
                    const uint32_t slot = allocation->spill_ids[input->values[interval]];
                    const bool initial = position == input->starts[interval];
                    valid = allocation->registers[f] < 32 && (initial || slot < 4);
                    if (valid)
                    {
                        registers[allocation->registers[f]] = initial ? values[input->values[interval]] : slots[slot];
                    }
                }
            }
        }
        for (uint32_t interval = 0; valid && interval < input->count; interval += 1)
        {
            for (uint32_t use = input->use_offsets[interval]; valid && use < input->use_offsets[interval + 1]; use += 1)
            {
                if (input->use_positions[use] == position)
                {
                    Tier1AllocationLocation location = { 0 };
                    valid = tier1_allocation_location(input, allocation, interval, position, &location);
                    const bool required = (input->flags != 0 && (input->flags[interval] & BUSTER_TIER1_ALLOCATE_REQUIRED) != 0) || (input->use_required != 0 && input->use_required[use] != 0);
                    valid = valid && (!required || location.kind == BUSTER_TIER1_ALLOCATE_REGISTER);
                    if (valid && location.kind == BUSTER_TIER1_ALLOCATE_REGISTER)
                    {
                        valid = location.reg < 32 && registers[location.reg] == values[input->values[interval]];
                    }
                    else if (valid)
                    {
                        valid = location.kind == BUSTER_TIER1_ALLOCATE_SPILL && location.spill_id < 4 && slots[location.spill_id] == values[input->values[interval]];
                    }
                }
            }
        }
    }
    return valid;
}

static void tier1_allocation_test_expiry_case(Tier1AllocationTestTally* tally, const Tier1AllocationExpiryCase* fixture, uint32_t class)
{
    uint8_t classes[4] = { (uint8_t)class, (uint8_t)class, (uint8_t)class, (uint8_t)class };
    Tier1AllocationInput input = fixture->input;
    input.classes = classes;
    Tier1Allocation expected = fixture->expected;
    for (uint32_t other = 0; other < BUSTER_TIER1_ALLOCATE_CLASSES; other += 1)
    {
        input.registers[other] = other == class ? fixture->input.registers[0] : 0;
        expected.peak_pressure[other] = other == class ? fixture->expected.peak_pressure[0] : 0;
    }
    _Alignas(64) uint8_t memory[2][2048];
    Tier1Allocation ample[2] = { { 0 } };
    bool produced[2] = { false, false };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        memset(memory[path], 0xa5, sizeof(memory[path]));
        Arena arena = { .base = memory[path], .capacity = sizeof(memory[path]), .length = 19 };
        const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, &arena, &ample[path]) : tier1_allocate_scalar(&input, &arena, &ample[path]);
        produced[path] = error == TIER1_ALLOCATION_OK;
        bool valid = produced[path] && tier1_allocation_test_expiry_equal(&input, &ample[path], &expected, false);
        valid = valid && arena.length == 19 + expected.retained_bytes && tier1_allocation_verify(&input, &ample[path]) && tier1_allocation_test_occupancy(&input, &ample[path]);
        valid = valid && tier1_allocation_test_expiry_values(&input, &ample[path]);
        tier1_allocation_test_expect(tally, valid, fixture->name);
    }
    tier1_allocation_test_expect(tally, produced[0] && produced[1] && tier1_allocation_test_expiry_equal(&input, &ample[0], &ample[1], false), "focused ample-capacity entry points agree on every logical result field");
    // Sweep each entry point's own boundary; never require equal status at one capacity. In particular, the old
    // vector implementation's extra scratch may legitimately fail where this version or the scalar entry succeeds.
    for (uint32_t path = 0; path < 2; path += 1)
    {
        bool failed = false;
        bool succeeded = false;
        uint32_t minimum = 0;
        _Alignas(64) uint8_t limited_memory[2048];
        for (uint32_t capacity = 19; produced[path] && capacity <= sizeof(limited_memory); capacity += 1)
        {
            memset(limited_memory, 0xa5, sizeof(limited_memory));
            Arena arena = { .base = limited_memory, .capacity = capacity, .length = 19 };
            Tier1Allocation before;
            memset(&before, 0x5a, sizeof(before));
            Tier1Allocation output = before;
            const Tier1AllocationError error = path == 0 ? tier1_allocate(&input, &arena, &output) : tier1_allocate_scalar(&input, &arena, &output);
            bool prefix = true;
            for (uint32_t i = 0; i < 19; i += 1)
            {
                prefix = prefix && limited_memory[i] == 0xa5;
            }
            bool valid = prefix;
            if (error == TIER1_ALLOCATION_OK)
            {
                minimum = !succeeded ? capacity : minimum;
                succeeded = true;
                valid = valid && tier1_allocation_test_expiry_equal(&input, &output, &ample[path], true) && arena.length == 19 + output.retained_bytes;
            }
            else
            {
                failed = true;
                valid = valid && !succeeded && error == TIER1_ALLOCATION_CAPACITY && arena.length == 19 && memcmp(&output, &before, sizeof(output)) == 0;
            }
            tier1_allocation_test_expect(tally, valid, "focused capacity sweep preserves full output on success and prefix/result transaction on failure");
        }
        tier1_allocation_test_expect(tally, failed && succeeded, "focused capacity sweep observes both failure and success without equating entry-point boundaries");
        const uint32_t expected_minimum = fixture->minimum_capacity[path == 0 && BUSTER_HOST_VECTOR ? 0 : 1];
        tier1_allocation_test_expect(tally, expected_minimum == 0 || minimum == expected_minimum, "focused capacity layout has the documented boundary for this entry point and host path");
    }
}

static void tier1_allocation_test_expiry(Tier1AllocationTestTally* tally)
{
    const Tier1AllocationExpiryCase fixtures[] = {
        {
            // Interval0 loses r0 at1 and ends at4. Interval1 still owns r0 until9, so interval2 at5 must spill.
            .input = {
                .starts = (uint32_t[]){ 0, 1, 5 },
                .ends = (uint32_t[]){ 4, 9, 8 },
                .values = (uint32_t[]){ 0, 1, 2 },
                .allowed_registers = (uint32_t[]){ 1, 1, 1 },
                .flags = (uint8_t[]){ 0, BUSTER_TIER1_ALLOCATE_REQUIRED, 0 },
                .use_offsets = (uint32_t[]){ 0, 1, 2, 3 },
                .use_positions = (uint32_t[]){ 3, 8, 7 },
                .use_count = 3,
                .count = 3,
                .value_count = 3,
                .position_count = 9,
                .registers = { 1, 0 },
            },
            .expected = {
                .fragment_offsets = (uint32_t[]){ 0, 2, 3, 4 },
                .starts = (uint32_t[]){ 0, 1, 1, 5 },
                .ends = (uint32_t[]){ 1, 4, 9, 8 },
                .kinds = (uint8_t[]){ 0, 1, 0, 1 },
                .registers = (uint8_t[]){ 0, UINT8_MAX, 0, UINT8_MAX },
                .fragment_count = 4,
                .interval_count = 3,
                .value_registers = (uint8_t[]){ UINT8_MAX, 0, UINT8_MAX },
                .spill_ids = (uint32_t[]){ 0, UINT32_MAX, 1 },
                .spill_values = (uint32_t[]){ 0, 2 },
                .spill_count = 2,
                .scalar_intervals = 3,
                .splits = 1,
                .peak_pressure = { 2, 0 },
                .retained_bytes = 105,
            },
            .name = "old end cannot free the reassigned register while its successor remains live",
        },
        {
            // Interval0: r0[0,1), spill[1,4), r1[4,10). Its current r1 must be freed at10; interval1 keeps r0
            // through12, and interval3 must obtain r1 at11. No fixed input has changed interval0's allowed mask.
            .input = {
                .starts = (uint32_t[]){ 0, 1, 2, 11 },
                .ends = (uint32_t[]){ 10, 12, 6, 13 },
                .values = (uint32_t[]){ 0, 1, 2, 3 },
                .allowed_registers = (uint32_t[]){ 3, 1, 2, 2 },
                .flags = (uint8_t[]){ 0, BUSTER_TIER1_ALLOCATE_REQUIRED, 0, 0 },
                .use_offsets = (uint32_t[]){ 0, 2, 3, 4, 5 },
                .use_positions = (uint32_t[]){ 4, 8, 9, 3, 12 },
                .use_required = (uint8_t[]){ 1, 1, 0, 0, 1 },
                .use_count = 5,
                .count = 4,
                .value_count = 4,
                .position_count = 13,
                .registers = { 3, 0 },
            },
            .expected = {
                .fragment_offsets = (uint32_t[]){ 0, 3, 4, 6, 7 },
                .starts = (uint32_t[]){ 0, 1, 4, 1, 2, 4, 11 },
                .ends = (uint32_t[]){ 1, 4, 10, 12, 4, 6, 13 },
                .kinds = (uint8_t[]){ 0, 1, 0, 0, 0, 1, 0 },
                .registers = (uint8_t[]){ 0, UINT8_MAX, 1, 0, 1, UINT8_MAX, 1 },
                .fragment_count = 7,
                .interval_count = 4,
                .value_registers = (uint8_t[]){ UINT8_MAX, 0, UINT8_MAX, 1 },
                .spill_ids = (uint32_t[]){ 0, UINT32_MAX, 1, UINT32_MAX },
                .spill_values = (uint32_t[]){ 0, 2 },
                .spill_count = 2,
                .scalar_intervals = 4,
                .splits = 3,
                .peak_pressure = { 3, 0 },
                .retained_bytes = 237,
            },
            .name = "required use reloads the same value into a different register and frees that current register at its end",
        },
        {
            // Interval0's required use at3 is processed before interval1's later duplicate use event at3. That
            // resident r0 is already pinned, so reload interval0 into interval2's r1, not the first occupied slot.
            .input = {
                .starts = (uint32_t[]){ 2, 0, 1 },
                .ends = (uint32_t[]){ 8, 9, 10 },
                .values = (uint32_t[]){ 0, 1, 2 },
                .allowed_registers = (uint32_t[]){ 3, 3, 3 },
                .use_offsets = (uint32_t[]){ 0, 3, 6, 8 },
                .use_positions = (uint32_t[]){ 3, 3, 7, 3, 3, 8, 3, 9 },
                .use_required = (uint8_t[]){ 1, 1, 1, 0, 1, 0, 0, 0 },
                .use_count = 8,
                .count = 3,
                .value_count = 3,
                .position_count = 10,
                .registers = { 3, 0 },
            },
            .expected = {
                .fragment_offsets = (uint32_t[]){ 0, 2, 3, 5 },
                .starts = (uint32_t[]){ 2, 3, 0, 1, 3 },
                .ends = (uint32_t[]){ 3, 8, 9, 3, 10 },
                .kinds = (uint8_t[]){ 1, 0, 0, 0, 1 },
                .registers = (uint8_t[]){ UINT8_MAX, 1, 0, 1, UINT8_MAX },
                .fragment_count = 5,
                .interval_count = 3,
                .value_registers = (uint8_t[]){ UINT8_MAX, 0, UINT8_MAX },
                .spill_ids = (uint32_t[]){ 0, UINT32_MAX, 1 },
                .spill_values = (uint32_t[]){ 0, 2 },
                .spill_count = 2,
                .scalar_intervals = 3,
                .splits = 2,
                .peak_pressure = { 3, 0 },
                .retained_bytes = 265,
            },
            .name = "simultaneous required operands are pinned before earlier events and duplicates need no extra register",
        },
        {
            // At aligned base+19: retained end156, scalar peak288, vector peak320. The old vector path's four
            // removed columns added96bytes for these four intervals, so its historical boundary was416, not320.
            // This fixture does not call or certify that old implementation; a paired baseline run is separate.
            .input = {
                .starts = (uint32_t[]){ 0, 2, 4, 6 },
                .ends = (uint32_t[]){ 1, 3, 5, 7 },
                .values = (uint32_t[]){ 0, 1, 2, 3 },
                .allowed_registers = (uint32_t[]){ 1, 1, 1, 1 },
                .use_offsets = (uint32_t[]){ 0, 0, 0, 0, 0 },
                .count = 4,
                .value_count = 4,
                .position_count = 8,
                .registers = { 3, 0 },
            },
            .expected = {
                .fragment_offsets = (uint32_t[]){ 0, 1, 2, 3, 4 },
                .starts = (uint32_t[]){ 0, 2, 4, 6 },
                .ends = (uint32_t[]){ 1, 3, 5, 7 },
                .kinds = (uint8_t[]){ 0, 0, 0, 0 },
                .registers = (uint8_t[]){ 0, 0, 0, 0 },
                .fragment_count = 4,
                .interval_count = 4,
                .value_registers = (uint8_t[]){ 0, 0, 0, 0 },
                .spill_ids = (uint32_t[]){ UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX },
                .spill_values = (uint32_t[]){ 0 },
                .scalar_intervals = 4,
                .peak_pressure = { 1, 0 },
                .retained_bytes = 137,
            },
            .minimum_capacity = { 320, 288 },
            .name = "capacity reference preserves all public semantic fields and retained bytes with an independent fixed layout",
        },
    };
    for (uint32_t class = 0; class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
    {
        for (uint32_t i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i += 1)
        {
            tier1_allocation_test_expiry_case(tally, &fixtures[i], class);
        }
        Tier1AllocationInput impossible = fixtures[2].input;
        uint8_t classes[] = { (uint8_t)class, (uint8_t)class, (uint8_t)class };
        const uint32_t allowed[] = { 1, 1, 1 };
        impossible.classes = classes;
        impossible.allowed_registers = allowed;
        for (uint32_t other = 0; other < BUSTER_TIER1_ALLOCATE_CLASSES; other += 1)
        {
            impossible.registers[other] = other == class ? 1 : 0;
        }
        for (uint32_t path = 0; path < 2; path += 1)
        {
            _Alignas(64) uint8_t memory[2048];
            memset(memory, 0xa5, sizeof(memory));
            Arena arena = { .base = memory, .capacity = sizeof(memory), .length = 19 };
            Tier1Allocation before;
            memset(&before, 0x5a, sizeof(before));
            Tier1Allocation output = before;
            const Tier1AllocationError error = path == 0 ? tier1_allocate(&impossible, &arena, &output) : tier1_allocate_scalar(&impossible, &arena, &output);
            bool prefix = true;
            for (uint32_t i = 0; i < 19; i += 1)
            {
                prefix = prefix && memory[i] == 0xa5;
            }
            tier1_allocation_test_expect(tally, error == TIER1_ALLOCATION_CONSTRAINT && arena.length == 19 && prefix && memcmp(&output, &before, sizeof(output)) == 0, "two simultaneous values cannot share one register; failure preserves prefix and result");
        }
    }
}
