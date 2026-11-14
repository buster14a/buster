// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier1-wide's private x87 transport checks (#1387). Include after that suite's helpers. The shared planner's
// mutations test its independent symbolic proof; source IR fixtures exercise complete use counts and real frame homes.

static X86Instruction tier1_x87_transfer_memory(uint32_t form, uint32_t base, int32_t offset)
{
    return (X86Instruction){ .form = (uint16_t)form, .rm = (uint8_t)base, .mode = BUSTER_X86_MEMORY_BASE, .displacement = offset };
}

static void tier1_x87_transfer_proof(Tier1WideTest* test)
{
    X86Instruction original[24] = {
        { .form = X86_FORM_FLD_M80, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
        { .form = X86_FORM_FLD_M80, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE },
        { .form = X86_FORM_FMULP_ST_ST0, .rm = 1, .mode = BUSTER_X86_REGISTER_DIRECT },
        { .form = X86_FORM_FSTP_M80, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 32 },
        { .form = X86_FORM_FLD_M80, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 32 },
        { .form = X86_FORM_FSTP_M80, .rm = X86_REGISTER_RDX, .mode = BUSTER_X86_MEMORY_BASE },
    };
    uint32_t tokens[24];
    uint8_t eligible[24] = { 0 };
    uint8_t boundaries[25] = { 0 };
    memset(tokens, 0xff, sizeof(tokens));
    tokens[3] = 3;
    tokens[4] = 3;
    eligible[3] = 1;
    boundaries[0] = BUSTER_X87_TRANSFER_BOUNDARY;
    boundaries[4] = BUSTER_X87_TRANSFER_BOUNDARY;
    boundaries[6] = BUSTER_X87_TRANSFER_BOUNDARY;
    X86Instruction chosen[24];
    X86Instruction twin[24];
    memcpy(chosen, original, sizeof(original));
    memcpy(twin, original, sizeof(original));
    const X87TransferStats vector = x87_transfer_records(chosen, 6, tokens, eligible, boundaries, true);
    const X87TransferStats scalar = x87_transfer_records(twin, 6, tokens, eligible, boundaries, false);
    tier1_wide_test_expect(test, vector.pairs == 1 && scalar.pairs == 1 && memcmp(chosen, twin, sizeof(chosen)) == 0, "both transport paths replace the single private round trip alike");
    tier1_wide_test_expect(test, chosen[3].form == X86_FORM_FLD_ST && chosen[4].form == X86_FORM_FSTP_ST && x87_transfer_verify(original, chosen, tokens, eligible, 0, 6), "the duplicate and pop preserve both waiting points and pass independent replay");
    for (uint32_t mutant = 0; mutant < 9; mutant += 1)
    {
        memcpy(twin, chosen, sizeof(twin));
        if (mutant == 0)
        {
            twin[3].rm = 1;
        }
        else if (mutant == 1)
        {
            twin[4].rm = 1;
        }
        else if (mutant == 2)
        {
            twin[2].form = X86_FORM_FDIVP_ST_ST0;
        }
        else if (mutant == 3)
        {
            twin[0].displacement = 16;
        }
        else if (mutant == 4)
        {
            twin[5] = (X86Instruction){ 0 };
        }
        else if (mutant == 5)
        {
            eligible[3] = 0;
        }
        else if (mutant == 6)
        {
            tokens[4] = 2;
        }
        else if (mutant == 7)
        {
            twin[4] = original[4];
        }
        else
        {
            twin[3] = (X86Instruction){ 0 };
        }
        tier1_wide_test_expect(test, !x87_transfer_verify(original, twin, tokens, eligible, 0, 6), "the transport proof rejects a changed value, operation, access, stack action or eligibility certificate");
        eligible[3] = 1;
        tokens[4] = 3;
    }
    // Extra baseline stack entries sit above the retained duplicate pair: depth six fits exactly, seven/eight do not.
    for (uint32_t pressure = 6; pressure <= 8; pressure += 1)
    {
        const uint32_t load = 4 + pressure * 2;
        const uint32_t count = load + 2;
        memset(tokens, 0xff, sizeof(tokens));
        memset(boundaries, 0, sizeof(boundaries));
        tokens[3] = 3;
        tokens[load] = 3;
        boundaries[0] = BUSTER_X87_TRANSFER_BOUNDARY;
        boundaries[4] = BUSTER_X87_TRANSFER_BOUNDARY;
        boundaries[load] = BUSTER_X87_TRANSFER_BOUNDARY;
        boundaries[count] = BUSTER_X87_TRANSFER_BOUNDARY;
        for (uint32_t k = 0; k < pressure; k += 1)
        {
            original[4 + k] = tier1_x87_transfer_memory(X86_FORM_FLD_M80, X86_REGISTER_RAX, (int32_t)(16 * k));
            original[4 + pressure + k] = tier1_x87_transfer_memory(X86_FORM_FSTP_M80, X86_REGISTER_RDX, (int32_t)(16 * k));
        }
        original[load] = tier1_x87_transfer_memory(X86_FORM_FLD_M80, X86_REGISTER_RSP, 32);
        original[load + 1] = tier1_x87_transfer_memory(X86_FORM_FSTP_M80, X86_REGISTER_RDX, 0);
        memcpy(chosen, original, sizeof(original));
        const X87TransferStats stats = x87_transfer_records(chosen, count, tokens, eligible, boundaries, true);
        tier1_wide_test_expect(test, stats.pairs == (pressure == 6) && (pressure == 6 || memcmp(chosen, original, sizeof(original)) == 0), "the complete physical depth includes both retained copies and rejects overflow transactionally");
        // A hidden overlapping read cannot be waved through by giving it no token.
        if (pressure == 6)
        {
            original[4] = tier1_x87_transfer_memory(X86_FORM_FLD_M80, X86_REGISTER_RSP, 32);
            memcpy(chosen, original, sizeof(original));
            const X87TransferStats overlap = x87_transfer_records(chosen, count, tokens, eligible, boundaries, true);
            tier1_wide_test_expect(test, overlap.pairs == 0 && memcmp(chosen, original, sizeof(original)) == 0, "the independent replay rejects an unannotated private-home reader");
        }
    }
    // A forced control/effect boundary between the producer and consumer must retain their memory home.
    original[4] = tier1_x87_transfer_memory(X86_FORM_FLD_M80, X86_REGISTER_RSP, 32);
    original[5] = tier1_x87_transfer_memory(X86_FORM_FSTP_M80, X86_REGISTER_RDX, 0);
    memset(tokens, 0xff, sizeof(tokens));
    memset(boundaries, 0, sizeof(boundaries));
    tokens[3] = 3;
    tokens[4] = 3;
    boundaries[0] = BUSTER_X87_TRANSFER_BOUNDARY;
    boundaries[4] = BUSTER_X87_TRANSFER_BOUNDARY | BUSTER_X87_TRANSFER_CUT;
    boundaries[6] = BUSTER_X87_TRANSFER_BOUNDARY;
    memcpy(chosen, original, sizeof(original));
    const X87TransferStats cut = x87_transfer_records(chosen, 6, tokens, eligible, boundaries, true);
    tier1_wide_test_expect(test, cut.pairs == 0 && memcmp(chosen, original, sizeof(original)) == 0, "the transport never crosses an adapter's control or environment cut");
}

static void tier1_x87_transfer_candidate_bounds(Tier1WideTest* test)
{
    const long page_value = sysconf(_SC_PAGESIZE);
    const uint64_t page = page_value > 0 ? (uint64_t)page_value : 4096;
    uint8_t* records_memory = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* tokens_memory = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = records_memory != MAP_FAILED && tokens_memory != MAP_FAILED && mprotect(records_memory + page, page, PROT_NONE) == 0 && mprotect(tokens_memory + page, page, PROT_NONE) == 0;
    if (tier1_wide_test_expect(test, ready, "the transport classifier's guarded columns"))
    {
        uint8_t eligible[BUSTER_X87_TRANSFER_RECORDS];
        memset(eligible, 1, sizeof(eligible));
        for (uint32_t count = 0; count <= BUSTER_X87_TRANSFER_RECORDS; count += 1)
        {
            X86Instruction* records = (X86Instruction*)(records_memory + page - (uint64_t)count * sizeof(X86Instruction));
            uint32_t* tokens = (uint32_t*)(tokens_memory + page - (uint64_t)count * 4);
            for (uint32_t r = 0; r < count; r += 1)
            {
                records[r] = tier1_x87_transfer_memory(X86_FORM_FSTP_M80, X86_REGISTER_RSP, (int32_t)(16 * r));
                tokens[r] = r;
            }
            const uint64_t expected = count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1;
            const uint64_t vector = x87_transfer_candidates(records, tokens, eligible, 0, count, true);
            const uint64_t scalar = x87_transfer_candidates(records, tokens, eligible, 0, count, false);
            tier1_wide_test_expect(test, vector == expected && scalar == expected, "the transport classifier's scalar/vector tails end exactly at a protected page");
        }
    }
    if (records_memory != MAP_FAILED)
    {
        munmap(records_memory, page * 2);
    }
    if (tokens_memory != MAP_FAILED)
    {
        munmap(tokens_memory, page * 2);
    }
}

static void tier1_x87_transfer_emission(Tier1WideTest* test)
{
    static const char* const operations[] = {
        "  %5 = fmul.f80 %1, %3\n",
        "  %5 = fmul.f80 %1, %3\n  %6 = fsub.f80 %5, %1\n  %7 = fdiv.f80 %6, %3\n",
        "  %5 = fmul.f80 %1, %3\n  %6 = fadd.f80 %5, %5\n",
        "  %5 = fmul.f80 %1, %3\n  %6 = fsub.f80 %1, %5\n",
        "  %5 = fmul.f80 %1, %3\n",
    };
    static const uint32_t values[] = { 5, 7, 6, 6, 5 };
    static const uint32_t pairs[] = { 1, 3, 1, 1, 0 };
    static const uint32_t stores[] = { 3, 3, 4, 4, 4 };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(operations); fixture += 1)
    {
        char text[1024];
        snprintf(text, sizeof(text), "function (ptr) -> void {\nb0:\n  %%0 = param.ptr 0\n  %%1 = load.f80 %%0 align=16\n  %%2 = address.ptr %%0 offset=16\n  %%3 = load.f80 %%2 align=16\n  %%4 = address.ptr %%0 offset=256\n%s  %%%u = store.void %%4, %%%u align=16%s\n  %%%u = ret.void\n}\n",
                 operations[fixture], values[fixture] + 1, values[fixture], fixture == 4 ? " volatile" : "", values[fixture] + 2);
        const uint64_t mark = test->output.length;
        IrFunction function = { 0 };
        bool ok = tier1_wide_test_parse(test, "canonical x87 transport", text, &function);
        IrDominance dominance = { 0 };
        ok = ok && tier1_wide_test_dominance(test, &function, &dominance);
        for (uint32_t path = 0; ok && path < 2; path += 1)
        {
            const uint64_t path_mark = test->output.length;
            Tier1SelectPlan plan = { 0 };
            uint32_t failed = BUSTER_IR_NONE;
            const Tier1SelectError selected = tier1_select_plan(&function, &dominance, &test->output, &test->scratch, path == 0, &plan, &failed);
            ok = tier1_wide_test_expect(test, selected == TIER1_SELECT_OK, "select the canonical-result transport fixture");
            const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .vector = path == 0, .rematerialize = true, .certify = true };
            Tier1EmitResult emitted = { 0 };
            const Tier1EmitError error = ok ? tier1_emit_function(&plan, &options, test->baseline, &test->output, &test->scratch, &emitted, &failed) : TIER1_EMIT_INVALID;
            ok = ok && tier1_wide_test_expect(test, error == TIER1_EMIT_OK, "emit the canonical-result transport fixture");
            uint32_t duplicates = 0;
            uint32_t pops = 0;
            uint32_t memory_stores = 0;
            for (uint32_t r = 0; ok && r < emitted.code.count; r += 1)
            {
                const uint32_t form = emitted.code.records[r].form & BUSTER_X86_FORM_NUMBER;
                duplicates += form == X86_FORM_FLD_ST;
                pops += form == X86_FORM_FSTP_ST;
                memory_stores += form == X86_FORM_FSTP_M80;
            }
            tier1_wide_test_expect(test, ok && duplicates == pairs[fixture] && pops == pairs[fixture] && memory_stores == stores[fixture], "only sole canonical left-consumer/store pairs lose their private memory transfers");
            arena_pop_to(&test->output, path_mark);
        }
        arena_pop_to(&test->output, mark);
        tier1_wide_test_expect(test, test->scratch.length == 0, "transport emission returns its optional planning scratch");
    }
}

static void tier1_x87_transfer_tests(Tier1WideTest* test)
{
    tier1_x87_transfer_proof(test);
    tier1_x87_transfer_candidate_bounds(test);
    tier1_x87_transfer_emission(test);
}
