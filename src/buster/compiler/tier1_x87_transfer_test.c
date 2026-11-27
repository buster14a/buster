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
    uint8_t annotations[24] = { 0 };
    uint8_t boundaries[25] = { 0 };
    memset(tokens, 0xff, sizeof(tokens));
    tokens[3] = 3;
    tokens[4] = 3;
    annotations[4] = BUSTER_X87_TRANSFER_RELOAD;
    annotations[3] = 1;
    boundaries[0] = BUSTER_X87_TRANSFER_BOUNDARY;
    boundaries[4] = BUSTER_X87_TRANSFER_BOUNDARY;
    boundaries[6] = BUSTER_X87_TRANSFER_BOUNDARY;
    X86Instruction chosen[24];
    X86Instruction twin[24];
    memcpy(chosen, original, sizeof(original));
    memcpy(twin, original, sizeof(original));
    const X87TransferStats transfer = x87_transfer_records(chosen, 6, tokens, annotations, boundaries);
    tier1_wide_test_expect(test, transfer.pairs == 1 && transfer.regions == 1, "one private round trip in one region");
    tier1_wide_test_expect(test, chosen[3].form == X86_FORM_FST_ST && chosen[4].form == X86_FORM_FXCH_ST && x87_transfer_verify(original, chosen, tokens, annotations, 0, 6), "the register store and exchange preserve both waiting points and pass independent replay");
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
            annotations[3] = 0;
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
        tier1_wide_test_expect(test, !x87_transfer_verify(original, twin, tokens, annotations, 0, 6), "the transport proof rejects a changed value, operation, access, stack action or eligibility certificate");
        annotations[3] = 1;
        tokens[4] = 3;
        annotations[4] = BUSTER_X87_TRANSFER_RELOAD;
    }
    for (uint32_t variant = 0; variant < 5; variant += 1)
    {
        const uint32_t first = 7;
        memcpy(chosen + first, original, 6 * sizeof(*original));
        uint32_t shifted_tokens[24];
        uint8_t shifted_annotations[24] = { 0 };
        memset(shifted_tokens, 0xff, sizeof(shifted_tokens));
        shifted_tokens[first + 3] = first + 3 + (variant == 1);
        shifted_tokens[first + 4] = first + 3;
        shifted_annotations[first + 3] = variant == 2 ? 0 : BUSTER_X87_TRANSFER_STORE;
        shifted_annotations[first + 4] = BUSTER_X87_TRANSFER_RELOAD;
        if (variant == 3)
        {
            chosen[first + 3].form = X86_FORM_FSTP_M80 + 1;
        }
        if (variant == 4)
        {
            chosen[first + 5] = chosen[first + 4];
            shifted_tokens[first + 5] = first + 3;
            shifted_annotations[first + 5] = BUSTER_X87_TRANSFER_RELOAD;
        }
        memcpy(twin + first, chosen + first, 6 * sizeof(*chosen));
        const uint32_t pairs = x87_transfer_region(chosen, shifted_tokens, shifted_annotations, first, first + 6);
        const bool expected = variant == 0;
        tier1_wide_test_expect(test, pairs == (uint32_t)expected && (expected
                              ? chosen[first + 3].form == X86_FORM_FST_ST && chosen[first + 4].form == X86_FORM_FXCH_ST
                              : memcmp(chosen + first, twin + first, 6 * sizeof(*chosen)) == 0),
                              "a shifted private home needs its own FSTP token and exactly one certified reload");
    }
    // Omitting a repeated reload's tag must not let a stale private memory slot escape the independent replay.
    memcpy(chosen, original, 6 * sizeof(*chosen));
    chosen[6] = original[4];
    chosen[7] = original[5];
    memcpy(twin, chosen, 8 * sizeof(*chosen));
    tokens[6] = 3;
    const uint32_t missing_tag = x87_transfer_region(chosen, tokens, annotations, 0, 8);
    tier1_wide_test_expect(test, missing_tag == 0 && memcmp(chosen, twin, 8 * sizeof(*chosen)) == 0,
                           "a second read of an already transported definition still rejects the plan");
    chosen[3] = (X86Instruction){ .form = X86_FORM_FST_ST, .mode = BUSTER_X86_REGISTER_DIRECT };
    chosen[4] = (X86Instruction){ .form = X86_FORM_FXCH_ST, .mode = BUSTER_X86_REGISTER_DIRECT };
    tier1_wide_test_expect(test, !x87_transfer_verify(twin, chosen, tokens, annotations, 0, 8),
                           "independent replay rejects a stale home read after its first register reload");
    tokens[6] = BUSTER_X87_TRANSFER_NONE;
    // Vary the actual region's position across a cache line of boundary bytes, including the last boundary.
    for (uint32_t first = 0; first <= 65; first += 1)
    {
        X86Instruction shifted[72] = { 0 };
        uint32_t names[72];
        uint8_t tags[72] = { 0 };
        uint8_t cuts[73] = { 0 };
        memset(names, 0xff, sizeof(names));
        shifted[0].form = X86_FORM_FCHS;
        memcpy(shifted + first, original, 6 * sizeof(*original));
        names[first + 3] = first + 3;
        names[first + 4] = first + 3;
        tags[first + 3] = BUSTER_X87_TRANSFER_STORE;
        tags[first + 4] = BUSTER_X87_TRANSFER_RELOAD;
        cuts[0] = BUSTER_X87_TRANSFER_BOUNDARY;
        cuts[first] = BUSTER_X87_TRANSFER_BOUNDARY | BUSTER_X87_TRANSFER_CUT;
        cuts[first + 4] = BUSTER_X87_TRANSFER_BOUNDARY;
        cuts[first + 6] = BUSTER_X87_TRANSFER_BOUNDARY;
        const X87TransferStats result = x87_transfer_records(shifted, first + 6, names, tags, cuts);
        tier1_wide_test_expect(test, result.pairs == 1 && result.regions == 1 &&
                               shifted[first + 3].form == X86_FORM_FST_ST && shifted[first + 4].form == X86_FORM_FXCH_ST,
                               "shifted and final boundaries retain exactly one proven pair");
    }
    // The integer staging store before FILD may touch the edge of the private ten-byte slot.
    original[6] = original[5];
    original[5] = original[4];
    tokens[4] = BUSTER_X87_TRANSFER_NONE;
    tokens[5] = 3;
    annotations[5] = BUSTER_X87_TRANSFER_RELOAD;
    boundaries[6] = 0;
    boundaries[7] = BUSTER_X87_TRANSFER_BOUNDARY;
    for (uint32_t variant = 0; variant < 3; variant += 1)
    {
        const uint32_t base = variant == 2 ? X86_REGISTER_RBP : X86_REGISTER_RSP;
        original[4] = tier1_x87_transfer_memory(X86_FORM_MOV_RM32_R32, base, variant == 0 ? 41 : 42);
        memcpy(chosen, original, sizeof(original));
        const X87TransferStats result = x87_transfer_records(chosen, 7, tokens, annotations, boundaries);
        tier1_wide_test_expect(test, result.pairs == (variant == 1), "only a provably disjoint integer store can cross the private x87 transfer");
    }
    // One retained register leaves seven available for the intervening sequence.
    for (uint32_t pressure = 6; pressure <= 8; pressure += 1)
    {
        const uint32_t load = 4 + pressure * 2;
        const uint32_t count = load + 2;
        memset(tokens, 0xff, sizeof(tokens));
        memset(boundaries, 0, sizeof(boundaries));
        tokens[3] = 3;
        tokens[load] = 3;
        annotations[load] = BUSTER_X87_TRANSFER_RELOAD;
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
        const X87TransferStats stats = x87_transfer_records(chosen, count, tokens, annotations, boundaries);
        tier1_wide_test_expect(test, stats.pairs == (pressure <= 7) && (pressure <= 7 || memcmp(chosen, original, sizeof(original)) == 0), "the complete physical depth includes the retained register and rejects overflow transactionally");
        // A hidden overlapping read cannot be waved through by giving it no token.
        if (pressure == 6)
        {
            original[4] = tier1_x87_transfer_memory(X86_FORM_FLD_M80, X86_REGISTER_RSP, 32);
            memcpy(chosen, original, sizeof(original));
            const X87TransferStats overlap = x87_transfer_records(chosen, count, tokens, annotations, boundaries);
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
    annotations[4] = BUSTER_X87_TRANSFER_RELOAD;
    boundaries[0] = BUSTER_X87_TRANSFER_BOUNDARY;
    boundaries[4] = BUSTER_X87_TRANSFER_BOUNDARY | BUSTER_X87_TRANSFER_CUT;
    boundaries[6] = BUSTER_X87_TRANSFER_BOUNDARY;
    memcpy(chosen, original, sizeof(original));
    const X87TransferStats cut = x87_transfer_records(chosen, 6, tokens, annotations, boundaries);
    tier1_wide_test_expect(test, cut.pairs == 0 && memcmp(chosen, original, sizeof(original)) == 0, "the transport never crosses an adapter's control or environment cut");
}

static void tier1_x87_transfer_candidate_bounds(Tier1WideTest* test)
{
    const long page_value = sysconf(_SC_PAGESIZE);
    const uint64_t page = page_value > 0 ? (uint64_t)page_value : 4096;
    uint8_t* memory = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = memory != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0;
    if (tier1_wide_test_expect(test, ready, "guard the end of the x87 store certificates"))
    {
        for (uint32_t count = 0; count <= BUSTER_X87_TRANSFER_RECORDS; count += 1)
        {
            const uint32_t first = 7;
            uint8_t* annotations = memory + page - count - first;
            for (uint32_t lane = 0; lane <= count; lane += 1)
            {
                memset(annotations, 0, first + count);
                annotations[first - 1] = 1;
                if (lane < count)
                {
                    annotations[first + lane] = lane & 1 ? BUSTER_X87_TRANSFER_RELOAD : BUSTER_X87_TRANSFER_STORE;
                }
                const uint64_t expected = lane < count ? UINT64_C(1) << lane : 0;
                const __m512i tags = x87_transfer_annotations(annotations, first, count);
                const uint64_t stores = _mm512_test_epi8_mask(tags, _mm512_set1_epi8(BUSTER_X87_TRANSFER_STORE));
                const uint64_t loads = _mm512_test_epi8_mask(tags, _mm512_set1_epi8(BUSTER_X87_TRANSFER_RELOAD));
                tier1_wide_test_expect(test, stores == (lane & 1 ? 0 : expected) && loads == (lane & 1 ? expected : 0), "only the certified lane in the guarded window is set");
            }
        }
    }
    if (memory != MAP_FAILED)
    {
        munmap(memory, page * 2);
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
    static const uint32_t pairs[] = { 3, 3, 3, 3, 2, 2 };
    static const uint32_t stores[] = { 1, 3, 2, 2, 2, 3 };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(pairs); fixture += 1)
    {
        char text[1024];
        if (fixture == BUSTER_ARRAY_LENGTH(operations))
        {
            strcpy(text, "function (ptr, i64) -> void {\nb0:\n  %0 = param.ptr 0\n  %1 = param.i64 1\n  %2 = const.i64 0x0\n"
                         "  %3 = icmp.i1 %1, %2 ne\n  %4 = cbr.void %3, b1, b2\nb1:\n  %5 = load.f80 %0 align=16\n  %6 = br.void b3\n"
                         "b2:\n  %7 = address.ptr %0 offset=16\n  %8 = load.f80 %7 align=16\n  %9 = br.void b3\nb3:\n"
                         "  %10 = phi.f80 [%5, b1], [%8, b2]\n  %11 = store.void %0, %10 align=16\n  %12 = ret.void\n}\n");
        }
        else
        {
            snprintf(text, sizeof(text), "function (ptr) -> void {\nb0:\n  %%0 = param.ptr 0\n  %%1 = load.f80 %%0 align=16\n  %%2 = address.ptr %%0 offset=16\n  %%3 = load.f80 %%2 align=16\n  %%4 = address.ptr %%0 offset=256\n%s  %%%u = store.void %%4, %%%u align=16%s\n  %%%u = ret.void\n}\n",
                     operations[fixture], values[fixture] + 1, values[fixture], fixture == 4 ? " volatile" : "", values[fixture] + 2);
        }
        const uint64_t mark = test->output.length;
        IrFunction function = { 0 };
        bool ok = tier1_wide_test_parse(test, "canonical x87 transport", text, &function);
        IrDominance dominance = { 0 };
        ok = ok && tier1_wide_test_dominance(test, &function, &dominance);
        if (ok)
        {
            const uint64_t path_mark = test->output.length;
            Tier1SelectPlan plan = { 0 };
            uint32_t failed = BUSTER_IR_NONE;
            const Tier1SelectError selected = tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &failed);
            ok = tier1_wide_test_expect(test, selected == TIER1_SELECT_OK, "select the canonical-result transport fixture");
            const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .rematerialize = true, .certify = true };
            Tier1EmitResult emitted = { 0 };
            const Tier1EmitError error = ok ? tier1_emit_function(&plan, &options, test->baseline, &test->output, &test->scratch, &emitted, &failed) : TIER1_EMIT_INVALID;
            ok = ok && tier1_wide_test_expect(test, error == TIER1_EMIT_OK, "emit the canonical-result transport fixture");
            uint32_t retained = 0;
            uint32_t exchanges = 0;
            uint32_t memory_stores = 0;
            for (uint32_t r = 0; ok && r < emitted.code.count; r += 1)
            {
                const uint32_t form = emitted.code.records[r].form & BUSTER_X86_FORM_NUMBER;
                retained += form == X86_FORM_FST_ST;
                exchanges += form == X86_FORM_FXCH_ST;
                memory_stores += form == X86_FORM_FSTP_M80;
            }
            tier1_wide_test_expect(test, ok && retained == pairs[fixture] && exchanges == pairs[fixture] && memory_stores == stores[fixture], "single-use operands stay in x87 registers in their original order");
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
