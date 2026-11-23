// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The VEX twins of Tier 1's legacy SSE records (#2830, tier1_emit_vex), run natively against their legacy forms, included
// after tier1_symbol_fold_test.c. The processor is the oracle. For each entry of x86_vex_forms, each of its
// operand kinds (registers, where its ModRM.rm takes one, and memory) and random registers and state, a native function
// loads xmm0 to xmm15, eight general registers, a block of memory and MXCSR (its flags clear) from a state, runs the
// record, and stores them back with the status flags; the legacy record's state and its twin's must agree in every
// register's low 128 bits, every general register, the flags, MXCSR and the memory. A host without AVX skips them. Then
// x86_upper_dirty's records: each form's operands at registers below and past 16.

#define BUSTER_TIER1_VEX_TEST_ROUNDS 24u
#define BUSTER_TIER1_VEX_TEST_GENERAL 8u
#define BUSTER_TIER1_VEX_TEST_MEMORY 64u

typedef struct Tier1VexTestState Tier1VexTestState;
struct Tier1VexTestState
{
    _Alignas(64) uint8_t xmm[16][16];
    uint64_t general[BUSTER_TIER1_VEX_TEST_GENERAL];
    uint64_t flags;
    uint32_t mxcsr;
    uint32_t padding;
    // 16-byte aligned, as a legacy packed form's memory operand must be.
    _Alignas(16) uint8_t memory[BUSTER_TIER1_VEX_TEST_MEMORY];
};

// The general registers the state holds: none callee-saved, nor rdi, the state's address.
static const uint8_t tier1_vex_test_general[BUSTER_TIER1_VEX_TEST_GENERAL] = {
    X86_REGISTER_RAX, X86_REGISTER_RCX, X86_REGISTER_RDX, X86_REGISTER_RSI, X86_REGISTER_R8, X86_REGISTER_R9, X86_REGISTER_R10, X86_REGISTER_R11,
};

static uint64_t tier1_vex_test_random(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

// Runs `record` on `state` natively: loads, the record, stores. False where it doesn't encode or map.
static __attribute__((no_sanitize("function"))) bool tier1_vex_test_run(X86Instruction record, Tier1VexTestState* state)
{
    X86Instruction records[64];
    uint32_t count = 0;
    const int32_t general = (int32_t)offsetof(Tier1VexTestState, general);
    records[count++] = (X86Instruction){ .form = X86_FORM_LDMXCSR_M32, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = (int32_t)offsetof(Tier1VexTestState, mxcsr) };
    for (uint32_t x = 0; x < 16; x += 1)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_VMOVDQU_X_XM128, .reg = (uint8_t)x, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = (int32_t)(16 * x) };
    }
    for (uint32_t g = 0; g < BUSTER_TIER1_VEX_TEST_GENERAL; g += 1)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = tier1_vex_test_general[g], .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = general + (int32_t)(8 * g) };
    }
    records[count++] = record;
    for (uint32_t g = 0; g < BUSTER_TIER1_VEX_TEST_GENERAL; g += 1)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = tier1_vex_test_general[g], .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = general + (int32_t)(8 * g) };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_PUSHFQ };
    records[count++] = (X86Instruction){ .form = X86_FORM_POP_R64, .rm = X86_REGISTER_RAX };
    records[count++] = (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = (int32_t)offsetof(Tier1VexTestState, flags) };
    records[count++] = (X86Instruction){ .form = X86_FORM_STMXCSR_M32, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = (int32_t)offsetof(Tier1VexTestState, mxcsr) };
    for (uint32_t x = 0; x < 16; x += 1)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_VMOVDQU_XM128_X, .reg = (uint8_t)x, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = (int32_t)(16 * x) };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
    uint8_t* code = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool ran = code != MAP_FAILED && x86_validate(records, count) == count;
    const int64_t length = ran ? x86_encode(records, count, code, 4096, 0, 0) : -1;
    ran = ran && length > 0 && mprotect(code, 4096, PROT_READ | PROT_EXEC) == 0;
    if (ran)
    {
        void (*function)(Tier1VexTestState*) = (void (*)(Tier1VexTestState*))(void*)code;
        function(state);
    }
    if (code != MAP_FAILED)
    {
        munmap(code, 4096);
    }
    return ran;
}

// A state of random registers and memory: some lanes NaNs, infinities, zeros and small integers' floats, MXCSR's default.
static void tier1_vex_test_state(uint64_t* random, Tier1VexTestState* state)
{
    static const uint32_t singles[] = { 0x7fc00000u, 0x7f800001u, 0xff800000u, 0x00000000u, 0x80000000u, 0x3f800000u, 0x4b000000u, 0x00000001u };
    static const uint64_t doubles[] = { UINT64_C(0x7ff8000000000000), UINT64_C(0x7ff0000000000001), UINT64_C(0xfff0000000000000), 0, UINT64_C(0x8000000000000000),
                                        UINT64_C(0x3ff0000000000000), UINT64_C(0x4330000000000000), 1 };
    memset(state, 0, sizeof(*state));
    for (uint32_t x = 0; x < 16; x += 1)
    {
        uint64_t words[2] = { tier1_vex_test_random(random), tier1_vex_test_random(random) };
        const uint64_t pick = tier1_vex_test_random(random);
        words[0] = (pick & 3) == 0 ? doubles[(pick >> 2) & 7] : (pick & 3) == 1 ? (words[0] & ~UINT64_C(0xffffffff)) | singles[(pick >> 2) & 7] : words[0];
        memcpy(state->xmm[x], words, sizeof(words));
    }
    for (uint32_t g = 0; g < BUSTER_TIER1_VEX_TEST_GENERAL; g += 1)
    {
        const uint64_t value = tier1_vex_test_random(random);
        state->general[g] = (value & 1) != 0 ? value >> (value & 63) : value;
    }
    for (uint32_t k = 0; k < BUSTER_TIER1_VEX_TEST_MEMORY; k += 8)
    {
        const uint64_t pick = tier1_vex_test_random(random);
        const uint64_t value = (pick & 3) == 0 ? doubles[(pick >> 2) & 7] : (pick & 3) == 1 ? singles[(pick >> 2) & 7] : tier1_vex_test_random(random);
        memcpy(state->memory + k, &value, sizeof(value));
    }
    state->mxcsr = 0x1f80;
}

static void tier1_vex_test_pairs(Tier1EmitTest* test)
{
    uint64_t random = UINT64_C(0x2830);
    uint32_t pairs = 0;
    uint32_t runs = 0;
    for (uint32_t form = 1; form < X86_FORM_COUNT; form += 1)
    {
        const uint32_t entry = x86_vex_forms[form];
        const X86FormDescription* description = entry != 0 ? &x86_form_descriptions[form] : 0;
        for (uint32_t memory = 0; description != 0 && memory < 2; memory += 1)
        {
            const bool takes = memory != 0 ? (description->rm & BUSTER_X86_OPERAND_MEMORY) != 0 : (description->rm & BUSTER_X86_OPERAND_REGISTER) != 0;
            pairs += takes ? 1 : 0;
            for (uint32_t round = 0; takes && round < BUSTER_TIER1_VEX_TEST_ROUNDS; round += 1)
            {
                const uint8_t reg_class = (uint8_t)(description->reg & BUSTER_X86_OPERAND_CLASS);
                const uint8_t rm_class = (uint8_t)(description->rm & BUSTER_X86_OPERAND_CLASS);
                const uint64_t pick = tier1_vex_test_random(&random);
                const uint8_t reg = reg_class == BUSTER_X86_CLASS_XMM ? (uint8_t)(pick & 15) : tier1_vex_test_general[pick & 7];
                // An xmm source equal to the destination in some rounds.
                const uint8_t rm = memory != 0 ? X86_REGISTER_RDI : rm_class == BUSTER_X86_CLASS_XMM ? ((pick >> 8) % 4 == 0 ? reg : (uint8_t)((pick >> 4) & 15)) : tier1_vex_test_general[(pick >> 4) & 7];
                const X86Instruction legacy = {
                    .form = (uint16_t)form,
                    .reg = reg,
                    .rm = rm,
                    .mode = memory != 0 ? BUSTER_X86_MEMORY_BASE : BUSTER_X86_REGISTER_DIRECT,
                    .displacement = memory != 0 ? (int32_t)(offsetof(Tier1VexTestState, memory) + 16 * ((pick >> 12) & 3)) : 0,
                    .immediate = description->immediate_bytes != 0 ? (int32_t)((pick >> 16) & 7) : 0,
                };
                const X86Instruction twin = x86_vex_record(legacy, entry, 0);
                Tier1VexTestState before = { 0 };
                tier1_vex_test_state(&random, &before);
                Tier1VexTestState legacy_state = before;
                Tier1VexTestState twin_state = before;
                const bool ran = tier1_vex_test_run(legacy, &legacy_state) && tier1_vex_test_run(twin, &twin_state);
                tier1_emit_test_expect(test, ran, "a legacy SSE record and its VEX twin run natively");
                legacy_state.flags &= 0x8d5;
                twin_state.flags &= 0x8d5;
                const bool same = ran && memcmp(&legacy_state, &twin_state, sizeof(legacy_state)) == 0;
                if (ran && !same)
                {
                    fprintf(stderr, "test-tier1-emit: %s's VEX twin differs (reg %u, rm %u, %s)\n", description->mnemonic, reg, rm, memory != 0 ? "memory" : "registers");
                }
                tier1_emit_test_expect(test, same, "a VEX twin computes its legacy record's registers, flags, MXCSR and memory");
                runs += ran ? 1 : 0;
            }
        }
    }
    tier1_emit_test_expect(test, pairs >= 80, "every legacy form of the table runs by each operand kind it takes");
    printf("tier1 VEX twins: %u legacy forms' operand kinds, %u pairs run natively against their legacy records\n", pairs, runs);
}

// x86_upper_dirty on single records and on runs past 16: a ymm or zmm register below 16 in each operand dirties,
// one at 16 and past doesn't, nor an xmm register, nor a ymm memory operand's base.
static void tier1_vex_test_dirty(Tier1EmitTest* test)
{
    const X86Instruction clean[] = {
        { .form = X86_FORM_VADDPS_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 3 },
        { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 16, .vvvv = 17, .rm = 31 },
        { .form = X86_FORM_VMOVDQU32_Y_YM256, .reg = 20, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
        { .form = X86_FORM_ADD_R64_RM64, .reg = 1, .rm = 2 },
        { .form = X86_FORM_RET },
    };
    const X86Instruction dirty[] = {
        { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 0, .vvvv = 17, .rm = 18 },
        { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 16, .vvvv = 15, .rm = 18 },
        { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 16, .vvvv = 17, .rm = 3 },
        { .form = X86_FORM_VMOVDQU32_YM256_Y, .reg = 4, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
        { .form = (uint16_t)(X86_FORM_VMOVDQU64_Z_ZM512 + BUSTER_X86_FORM_FS), .reg = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
    };
    X86Instruction run[40];
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(run); k += 1)
    {
        run[k] = clean[k % BUSTER_ARRAY_LENGTH(clean)];
    }
    tier1_emit_test_expect(test, !x86_upper_dirty(clean, BUSTER_ARRAY_LENGTH(clean)) && !x86_upper_dirty(run, BUSTER_ARRAY_LENGTH(run)) && !x86_upper_dirty(run, 0),
                           "records of xmm registers, registers past 15 and memory leave the upper state clean");
    for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(dirty); d += 1)
    {
        for (uint32_t at = 0; at < BUSTER_ARRAY_LENGTH(run); at += 13)
        {
            X86Instruction placed[40];
            memcpy(placed, run, sizeof(placed));
            placed[at] = dirty[d];
            tier1_emit_test_expect(test, x86_upper_dirty(placed, BUSTER_ARRAY_LENGTH(placed)) && !x86_upper_dirty(placed, at), "a ymm or zmm register below 16 dirties the upper state where it is");
        }
    }
}

static void tier1_vex_tests(Tier1EmitTest* test)
{
    tier1_vex_test_dirty(test);
    if (__builtin_cpu_supports("avx"))
    {
        tier1_vex_test_pairs(test);
    }
}
