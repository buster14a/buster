// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Shared original integer-loop fixture for scheduler correctness tests and its benchmark.
// Include after tier1_schedule.c with initialized encoder, effects and model tables. Open a fresh fixture,
// check its 64 independent inputs, then close both mappings, including after failed preparation.
// These cold helpers contain no clocks, counters or benchmark timing loops. Generated code uses baseline GPR forms.

#ifndef BUSTER_TIER1_SCHEDULE_FIXTURE_C
#define BUSTER_TIER1_SCHEDULE_FIXTURE_C

#include <sys/mman.h>

#define BUSTER_TIER1_SCHEDULE_FIXTURE_RECORDS 64
#define BUSTER_TIER1_SCHEDULE_FIXTURE_BYTES 8192

typedef struct Tier1ScheduleFixture Tier1ScheduleFixture;
struct Tier1ScheduleFixture
{
    uint8_t* code[2];
    int64_t sizes[2];
    uint32_t moved;
};

static X86Instruction tier1_schedule_fixture_record(uint16_t form, uint8_t reg, uint8_t rm)
{
    const X86Instruction record = {
        .form = form,
        .reg = reg,
        .rm = rm,
        .mode = BUSTER_X86_REGISTER_DIRECT,
    };
    return record;
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_schedule_fixture_execute(const uint8_t* code, uint64_t first, uint64_t second, uint64_t iterations)
{
    // An external generated-code ABI boundary, not compiler-path dispatch. Only the absent Clang type metadata is
    // excluded from sanitizing; the constructor, bounds and other UB checks remain active.
    return ((uint64_t (*)(uint64_t, uint64_t, uint64_t))code)(first, second, iterations);
}

static void tier1_schedule_fixture_close(Tier1ScheduleFixture* fixture)
{
    for (uint32_t variant = 0; variant < 2; variant += 1)
    {
        if (fixture->code[variant] && fixture->code[variant] != MAP_FAILED)
        {
            munmap(fixture->code[variant], BUSTER_TIER1_SCHEDULE_FIXTURE_BYTES);
        }
    }
    *fixture = (Tier1ScheduleFixture){ 0 };
}

static bool tier1_schedule_fixture_open(Tier1ScheduleFixture* fixture, Tier1ScheduleGraph* graph, uint16_t* order, bool vector)
{
    X86Instruction body[BUSTER_TIER1_SCHEDULE_FIXTURE_RECORDS];
    for (uint32_t i = 0; i < BUSTER_TIER1_SCHEDULE_FIXTURE_RECORDS; i += 4)
    {
        body[i] = tier1_schedule_fixture_record(X86_FORM_NOT_RM64, 0, X86_REGISTER_R8);
        body[i + 1] = tier1_schedule_fixture_record(X86_FORM_NOT_RM64, 0, X86_REGISTER_R9);
        body[i + 2] = tier1_schedule_fixture_record(X86_FORM_IMUL_R64_RM64, X86_REGISTER_R10, X86_REGISTER_R11);
        body[i + 3] = tier1_schedule_fixture_record(X86_FORM_NOT_RM64, 0, X86_REGISTER_R10);
    }
    const Tier1ScheduleInput input = {
        .instructions = body,
        .count = BUSTER_TIER1_SCHEDULE_FIXTURE_RECORDS,
    };
    Tier1ScheduleStats stats = { 0 };
    bool valid = tier1_schedule_block(input, graph, order, input.count, vector, &stats) == TIER1_SCHEDULE_OK && tier1_schedule_verify(input, order);
    fixture->moved = stats.moved;
    for (uint32_t variant = 0; valid && variant < 2; variant += 1)
    {
        fixture->code[variant] = mmap(0, BUSTER_TIER1_SCHEDULE_FIXTURE_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        valid = fixture->code[variant] != MAP_FAILED;
        if (valid)
        {
            X86Instruction records[BUSTER_TIER1_SCHEDULE_FIXTURE_RECORDS + 16];
            uint32_t count = 0;
            records[count++] = tier1_schedule_fixture_record(X86_FORM_MOV_R64_RM64, X86_REGISTER_R8, X86_REGISTER_RDI);
            records[count++] = tier1_schedule_fixture_record(X86_FORM_MOV_R64_RM64, X86_REGISTER_R9, X86_REGISTER_RSI);
            records[count++] = tier1_schedule_fixture_record(X86_FORM_MOV_R64_RM64, X86_REGISTER_R10, X86_REGISTER_RDI);
            records[count++] = tier1_schedule_fixture_record(X86_FORM_MOV_R64_RM64, X86_REGISTER_R11, X86_REGISTER_RSI);
            records[count++] = tier1_schedule_fixture_record(X86_FORM_MOV_R64_RM64, X86_REGISTER_RCX, X86_REGISTER_RDX);
            const uint32_t loop = count;
            for (uint32_t i = 0; i < input.count; i += 1)
            {
                records[count++] = body[variant == 0 ? i : order[i]];
            }
            records[count++] = tier1_schedule_fixture_record(X86_FORM_DEC_RM64, 0, X86_REGISTER_RCX);
            const uint32_t branch = count;
            records[count++] = tier1_schedule_fixture_record(X86_FORM_JNE_REL32, 0, 0);
            records[count++] = tier1_schedule_fixture_record(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R10);
            records[count++] = tier1_schedule_fixture_record(X86_FORM_XOR_RM64_R64, X86_REGISTER_R8, X86_REGISTER_RAX);
            records[count++] = tier1_schedule_fixture_record(X86_FORM_XOR_RM64_R64, X86_REGISTER_R9, X86_REGISTER_RAX);
            records[count++] = tier1_schedule_fixture_record(X86_FORM_RET, 0, 0);
            uint32_t offsets[BUSTER_TIER1_SCHEDULE_FIXTURE_RECORDS + 16];
            valid = x86_validate(records, count) == count && x86_measure(records, count, offsets) >= 0;
            if (valid)
            {
                records[branch].displacement = (int32_t)offsets[loop] - (int32_t)offsets[branch + 1];
                uint8_t scalar[BUSTER_TIER1_SCHEDULE_FIXTURE_BYTES];
                fixture->sizes[variant] = x86_encode(records, count, fixture->code[variant], BUSTER_TIER1_SCHEDULE_FIXTURE_BYTES, 0, 0);
                const int64_t scalar_size = x86_encode_scalar(records, count, scalar, sizeof(scalar), 0, 0);
                valid = fixture->sizes[variant] > 0 && scalar_size == fixture->sizes[variant] && memcmp(scalar, fixture->code[variant], (uint64_t)scalar_size) == 0 &&
                        mprotect(fixture->code[variant], BUSTER_TIER1_SCHEDULE_FIXTURE_BYTES, PROT_READ | PROT_EXEC) == 0;
            }
        }
    }
    return valid;
}

static bool tier1_schedule_fixture_check(const Tier1ScheduleFixture* fixture)
{
    bool valid = true;
    for (uint64_t seed = 1; valid && seed <= 64; seed += 1)
    {
        const uint64_t first = seed * 11939;
        const uint64_t second = seed * 17 + 3;
        uint64_t expected = first;
        for (uint64_t i = 0; i < seed * BUSTER_TIER1_SCHEDULE_FIXTURE_RECORDS / 4; i += 1)
        {
            expected = ~(expected * second);
        }
        // The fixture has an even number of NOTs on each independent input per loop iteration.
        expected ^= first ^ second;
        valid = tier1_schedule_fixture_execute(fixture->code[0], first, second, seed) == expected && tier1_schedule_fixture_execute(fixture->code[1], first, second, seed) == expected;
    }
    return valid;
}

#endif // BUSTER_TIER1_SCHEDULE_FIXTURE_C
