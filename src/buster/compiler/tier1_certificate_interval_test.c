// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Fragment-only mutations, paired with the existing independent quadratic
// oracle. These tests do not substitute synthetic transitions for actual code.

static void tier1_cert_interval_expect(const Tier1AllocationInput* input, const Tier1Allocation* allocation, bool expected, uint64_t* checks, uint64_t* failures, const char* name)
{
    const size_t capacity = tier1_interval_scratch_size(input, allocation);
    unsigned char* memory = malloc(capacity + 2);
    bool passed = memory != 0;
    if (passed)
    {
        memset(memory, 0xa5, capacity + 2);
        Tier1IntervalReport report = { 0 };
        const bool actual = tier1_allocation_certificate_check(input, allocation, memory + 1, capacity, &report) == TIER1_CERT_OK;
        const bool oracle = tier1_allocation_verify(input, allocation);
        passed = actual == expected && oracle == expected && memory[0] == 0xa5 && memory[capacity + 1] == 0xa5;
        if (actual)
        {
            passed = passed && report.events == 2 * ((uint64_t)input->count + allocation->fragment_count + input->fixed.count);
        }
        free(memory);
    }
    *checks += 1;
    if (!passed)
    {
        fprintf(stderr, "test-tier1-certificate-interval: %s\n", name);
        *failures += 1;
    }
}

static int run_tier1_certificate_interval_tests(void)
{
    uint64_t checks = 0;
    uint64_t failures = 0;
    uint32_t starts[] = { 0, 4, 2 };
    uint32_t ends[] = { 4, 8, 6 };
    uint32_t values[] = { 0, 1, 2 };
    uint32_t allowed[] = { 3, 3, 3 };
    uint8_t classes[] = { 0, 0, 0 };
    uint32_t fixed_starts[] = { 0, 2 };
    uint32_t fixed_ends[] = { 8, 6 };
    uint32_t fixed_regs[] = { 4, 4 };
    uint8_t fixed_classes[] = { 0, 0 };
    uint32_t offsets[] = { 0, 1, 2, 3 };
    uint32_t fragment_starts[] = { 0, 4, 2, 0 };
    uint32_t fragment_ends[] = { 4, 8, 6, 0 };
    uint8_t kinds[] = { 0, 0, 0, 0 };
    uint8_t regs[] = { 0, 0, 1, 0 };
    uint8_t direct[] = { 0, 0, 1 };
    uint32_t spill_ids[] = { UINT32_MAX, UINT32_MAX, UINT32_MAX };
    uint32_t spill_values[] = { 0, 0, 0 };
    Tier1AllocationInput input = {
        .starts = starts,
        .ends = ends,
        .values = values,
        .allowed_registers = allowed,
        .classes = classes,
        .count = 3,
        .value_count = 3,
        .position_count = 8,
        .registers = { 7, 0 },
        .fixed = {
            .starts = fixed_starts,
            .ends = fixed_ends,
            .registers = fixed_regs,
            .classes = fixed_classes,
            .count = 2,
        },
    };
    Tier1Allocation output = {
        .fragment_offsets = offsets,
        .starts = fragment_starts,
        .ends = fragment_ends,
        .kinds = kinds,
        .registers = regs,
        .fragment_count = 3,
        .interval_count = 3,
        .value_registers = direct,
        .spill_ids = spill_ids,
        .spill_values = spill_values,
    };
    tier1_cert_interval_expect(&input, &output, true, &checks, &failures, "half-open reuse and overlapping fixed reservations");
    starts[1] = fragment_starts[1] = 3;
    tier1_cert_interval_expect(&input, &output, false, &checks, &failures, "one-position interference");
    starts[1] = fragment_starts[1] = 4;
    fixed_regs[1] = 2;
    tier1_cert_interval_expect(&input, &output, false, &checks, &failures, "fixed clobber intersects a live fragment");
    fixed_regs[1] = 4;
    fragment_starts[0] = 1;
    tier1_cert_interval_expect(&input, &output, false, &checks, &failures, "omitted interval prefix");
    fragment_starts[0] = 0;
    regs[2] = direct[2] = 2;
    tier1_cert_interval_expect(&input, &output, false, &checks, &failures, "restricted register mask");
    regs[2] = direct[2] = 1;
    direct[0] = 1;
    tier1_cert_interval_expect(&input, &output, false, &checks, &failures, "forged direct selector view");
    direct[0] = 0;
    offsets[1] = 4;
    tier1_cert_interval_expect(&input, &output, false, &checks, &failures, "malformed fragment offsets");
    offsets[1] = 1;

    // Register-to-home split, then memory versus register-required use.
    offsets[1] = 2;
    offsets[2] = 3;
    offsets[3] = 4;
    fragment_starts[1] = 2;
    fragment_ends[0] = 2;
    fragment_ends[1] = 4;
    fragment_starts[2] = 4;
    fragment_ends[2] = 8;
    fragment_starts[3] = 2;
    fragment_ends[3] = 6;
    regs[1] = UINT8_MAX;
    regs[2] = 0;
    regs[3] = 1;
    kinds[1] = BUSTER_TIER1_ALLOCATE_SPILL;
    direct[0] = UINT8_MAX;
    output.fragment_count = 4;
    output.spill_count = 1;
    spill_ids[0] = 0;
    tier1_cert_interval_expect(&input, &output, true, &checks, &failures, "register-to-spill fragment plan");
    spill_values[0] = 1;
    tier1_cert_interval_expect(&input, &output, false, &checks, &failures, "forged spill inverse");
    spill_values[0] = 0;
    uint32_t use_offsets[] = { 0, 2, 2, 2 };
    uint32_t uses[] = { 1, 3 };
    uint8_t required[] = { 1, 0 };
    input.use_offsets = use_offsets;
    input.use_positions = uses;
    input.use_required = required;
    input.use_count = 2;
    tier1_cert_interval_expect(&input, &output, true, &checks, &failures, "memory-permitted use");
    required[1] = 1;
    tier1_cert_interval_expect(&input, &output, false, &checks, &failures, "register-required use cannot name a home");
    required[1] = 0;
    uint8_t remat[] = { 1, 0, 0 };
    kinds[1] = BUSTER_TIER1_ALLOCATE_REMATERIALIZE;
    input.rematerializable = remat;
    output.spill_count = 0;
    spill_ids[0] = UINT32_MAX;
    tier1_cert_interval_expect(&input, &output, true, &checks, &failures, "eligible rematerialization fragment");
    remat[0] = 0;
    tier1_cert_interval_expect(&input, &output, false, &checks, &failures, "ineligible rematerialization fragment");
    remat[0] = 1;
    uint8_t flags[] = { BUSTER_TIER1_ALLOCATE_REQUIRED, 0, 0 };
    input.flags = flags;
    tier1_cert_interval_expect(&input, &output, false, &checks, &failures, "required whole interval cannot rematerialize");
    input.flags = 0;

    const size_t capacity = tier1_interval_scratch_size(&input, &output);
    unsigned char* memory = malloc(capacity + 2);
    bool capacity_valid = memory != 0;
    bool saw_success = false;
    for (size_t size = 0; capacity_valid && size <= capacity; size += 1)
    {
        memset(memory, 0xa5, capacity + 2);
        Tier1IntervalReport report = { 0 };
        const Tier1CertError error = tier1_allocation_certificate_check(&input, &output, memory + 1, size, &report);
        capacity_valid = (error == TIER1_CERT_OK || error == TIER1_CERT_CAPACITY) && memory[0] == 0xa5 && memory[size + 1] == 0xa5;
        saw_success |= error == TIER1_CERT_OK;
    }
    free(memory);
    checks += 1;
    failures += !capacity_valid || !saw_success;
    uint32_t zero = 0;
    const Tier1AllocationInput empty = { 0 };
    const Tier1Allocation empty_output = {
        .fragment_offsets = &zero,
    };
    tier1_cert_interval_expect(&empty, &empty_output, true, &checks, &failures, "empty fragment output");
    tier1_cert_interval_expect(0, 0, false, &checks, &failures, "null fragment inputs rejected");
    printf("test-tier1-certificate-interval: %" PRIu64 " checks, %" PRIu64 " failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
