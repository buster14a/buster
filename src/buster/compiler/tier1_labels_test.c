// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent native fixtures for labels and indirect jumps (#789), included after tier1_divide_test.c: the function of
// tier1_select_test_indirect_function through tier1_emit_function, which gives it to the CFG consumer, by both paths,
// with constants rematerialized and not, with every register and with few. Its labels' first records
// (Tier1EmitResult.label_records) are where the driver defines their symbols: label 0's is a stub after the indirect
// jump, which moves its phi's value on that edge, since its block is entered directly too, and labels 1 and 2 are their
// blocks', which the jump alone enters. Each reference to a label's symbol is resolved to that record, as the driver's
// link resolves it, and the code runs against tier1_select_test_indirect_oracle, which reads only the inputs, in a
// bounded child (tier1_labels_tests): every label is reached through the jump, and label 0 directly too.

#define BUSTER_TIER1_LABELS_SAMPLES 64u

// The code's branches sized and encoded into new pages, each reference to label k's symbol (k + 1) given label k's
// first record's address, and made executable: whether it runs natively here. The caller unmaps `executable`
// (MAP_FAILED where nothing was mapped).
static bool tier1_labels_load(Tier1EmitTest* test, const Tier1EmitResult* emitted, void** executable, uint64_t* bytes)
{
    const Tier1SelectCode* code = &emitted->code;
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    *bytes = (capacity + 4095) & ~UINT64_C(4095);
    *executable = mmap(0, *bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint32_t* offsets = malloc(((uint64_t)code->count + 1) * sizeof(uint32_t));
    uint32_t* ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(emitted->reference_count) * sizeof(uint32_t));
    bool ready = *executable != MAP_FAILED && offsets != 0 && ends != 0;
    tier1_emit_test_expect(test, ready, "labels' native execution buffer");
    if (ready)
    {
        const BranchSizingCheck checked = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
        ready = checked.error == BRANCH_SIZING_OK && size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets).error == BRANCH_SIZING_OK;
        tier1_emit_test_expect(test, ready, "the labels' function's branches size");
    }
    int64_t size = -1;
    if (ready)
    {
        size = x86_target_validate(test->allowed, code->records, code->count) == code->count ? x86_encode(code->records, code->count, *executable, capacity, 0, ends) : -1;
        ready = size > 0;
        tier1_emit_test_expect(test, ready, "the labels' function encodes at the baseline target");
    }
    for (uint32_t r = 0; ready && r < emitted->reference_count; r += 1)
    {
        const LinkReference* reference = &emitted->references[r];
        const uint32_t label = reference->target - 1;
        const uint32_t distance = x86_fixup_distance(code->records[reference->instruction].form, reference->field);
        ready = reference->kind == BUSTER_LINK_FIXUP_PC32 && reference->addend == 0 && reference->target != 0 && label < emitted->label_count && distance == sizeof(int32_t) && ends[r] >= distance &&
                ends[r] <= (uint64_t)size;
        tier1_emit_test_expect(test, ready, "each of the function's references is a label's address from rip");
        if (ready)
        {
            const int32_t relative = (int32_t)((int64_t)offsets[emitted->label_records[label]] - (int64_t)ends[r]);
            memcpy((uint8_t*)*executable + ends[r] - distance, &relative, sizeof(relative));
        }
    }
    if (ready)
    {
        ready = mprotect(*executable, *bytes, PROT_READ | PROT_EXEC) == 0;
        tier1_emit_test_expect(test, ready, "the labels' function made executable");
    }
    free(offsets);
    free(ends);
    return ready && tier1_emit_test_native(test, code->records, code->count);
}

// One compile by one path and one set of options: the CFG consumer's, label 0's symbol on the stub after the indirect jump
// and the others' at their blocks, and the code run on the samples. Returns whether label 0's edge had its stub.
static bool tier1_labels_compile(Tier1EmitTest* test, bool vector, bool rematerialize, uint32_t register_mask)
{
    IrFunction function;
    const bool created = tier1_select_test_indirect_function(&test->input, &function);
    IrDominance dominance;
    const bool dominated = created && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the labels' fixture selects");
    const Tier1EmitOptions options = {
        .register_mask = register_mask,
        .vector = vector,
        .rematerialize = rematerialize,
        .symbol_count = function.symbol_count,
    };
    Tier1EmitResult emitted;
    const uint64_t scratch_mark = test->scratch.length;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    tier1_emit_test_expect(test, error == TIER1_EMIT_OK && test->scratch.length == scratch_mark, "the labels' fixture emits through the CFG consumer");
    if (error != TIER1_EMIT_OK && planned && test->failures < 20)
    {
        fprintf(stderr, "  labels emission error=%u node=%u vector=%u remat=%u mask=%x\n", error, node, vector, rematerialize, register_mask);
    }
    bool stub = false;
    if (error == TIER1_EMIT_OK)
    {
        // The indirect jump's record, the dispatch node's last.
        const Tier1SelectCode* code = &emitted.code;
        uint32_t jump = UINT32_MAX;
        for (uint32_t r = code->node_first[27]; r < code->node_first[28]; r += 1)
        {
            jump = (code->records[r].form & BUSTER_X86_FORM_NUMBER) == X86_FORM_JMP_RM64 ? r : jump;
        }
        bool placed = emitted.general && emitted.label_count == 3 && jump != UINT32_MAX;
        for (uint32_t k = 0; placed && k < 3; k += 1)
        {
            placed = emitted.label_records[k] < code->count;
        }
        tier1_emit_test_expect(test, placed, "each label's first record is the function's, after one indirect jump");
        stub = placed && emitted.label_records[0] > jump;
        tier1_emit_test_expect(test, !placed || (emitted.label_records[1] < jump && emitted.label_records[2] < jump && emitted.label_records[1] <= code->node_first[22] &&
                                                 emitted.label_records[2] <= code->node_first[24]),
                               "the labels the jump alone enters start at their blocks, laid out before the dispatch block");
        void* executable = MAP_FAILED;
        uint64_t bytes = 0;
        if (placed && tier1_labels_load(test, &emitted, &executable, &bytes))
        {
            uint64_t random = UINT64_C(0x5ad1e7c30b9f2468) ^ ((uint64_t)register_mask << 8) ^ (vector ? 1 : 0) ^ (rematerialize ? 2 : 0);
            for (uint32_t sample = 0; sample < BUSTER_TIER1_LABELS_SAMPLES; sample += 1)
            {
                // Each label in turn, the first directly (s == 0) and through the jump (s == 1).
                const uint64_t x = (tier1_emit_test_random(&random) & ~UINT64_C(3)) | (sample & 3);
                const uint64_t y = tier1_emit_test_random(&random);
                const uint64_t expected = tier1_select_test_indirect_oracle(x, y);
                const uint64_t actual = tier1_emit_test_call(executable, x, y);
                if (actual != expected && test->failures < 20)
                {
                    fprintf(stderr, "  labels vector=%u remat=%u mask=%x x=%016" PRIx64 " y=%016" PRIx64 " actual=%016" PRIx64 " expected=%016" PRIx64 "\n", vector, rematerialize, register_mask, x, y, actual,
                            expected);
                }
                tier1_emit_test_expect(test, actual == expected, "an indirect jump reaches its labels natively, with their edges' moves");
            }
        }
        if (executable != MAP_FAILED)
        {
            munmap(executable, bytes);
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->input, 0);
    return stub;
}

// #1016's review, one compile of the chain's function (`fixture` 0) or of the shared labels' with phis (1) or without (2)
// by one path and one set of options: its emission's error, and where the chain's or the shared labels' without phis
// emits, its code run on the samples against its oracle. The arenas are as they were after a refusal.
static Tier1EmitError tier1_labels_review_compile(Tier1EmitTest* test, uint32_t fixture, bool vector, bool rematerialize, uint32_t register_mask)
{
    IrFunction function;
    const bool created = fixture == 0 ? tier1_select_test_indirect_chain_function(&test->input, &function) : tier1_select_test_shared_labels_function(&test->input, fixture == 1, &function);
    IrDominance dominance;
    const bool dominated = created && tier1_terminal_dominance(test, &function, vector, &dominance);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the review's indirect jumps' fixtures select");
    const Tier1EmitOptions options = {
        .register_mask = register_mask,
        .vector = vector,
        .rematerialize = rematerialize,
        .symbol_count = function.symbol_count,
    };
    Tier1EmitResult emitted;
    const uint64_t output_mark = test->output.length;
    const uint64_t scratch_mark = test->scratch.length;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    tier1_emit_test_expect(test, test->scratch.length == scratch_mark && (error == TIER1_EMIT_OK || test->output.length == output_mark), "the review's fixtures leave the arenas as they were");
    void* executable = MAP_FAILED;
    uint64_t bytes = 0;
    if (error == TIER1_EMIT_OK && fixture != 1 && emitted.label_count == 2 && tier1_labels_load(test, &emitted, &executable, &bytes))
    {
        uint64_t random = UINT64_C(0x3c5e7a91b2d4f608) ^ ((uint64_t)register_mask << 8) ^ (vector ? 1 : 0) ^ (rematerialize ? 2 : 0) ^ ((uint64_t)fixture << 32);
        for (uint32_t sample = 0; sample < BUSTER_TIER1_LABELS_SAMPLES; sample += 1)
        {
            // Each path in turn: both labels, and in the shared labels' function both jumps.
            const uint64_t x = (tier1_emit_test_random(&random) & ~UINT64_C(3)) | (sample & 3);
            const uint64_t y = tier1_emit_test_random(&random);
            const uint64_t expected = fixture == 0 ? tier1_select_test_indirect_chain_oracle(x, y) : tier1_select_test_shared_labels_oracle(x);
            const uint64_t actual = tier1_emit_test_call(executable, x, y);
            if (actual != expected && test->failures < 20)
            {
                fprintf(stderr, "  labels review fixture=%u vector=%u remat=%u mask=%x x=%016" PRIx64 " y=%016" PRIx64 " actual=%016" PRIx64 " expected=%016" PRIx64 "\n", fixture, vector, rematerialize,
                        register_mask, x, y, actual, expected);
            }
            tier1_emit_test_expect(test, actual == expected, "the review's indirect jumps reach their labels natively");
        }
    }
    if (executable != MAP_FAILED)
    {
        munmap(executable, bytes);
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->input, 0);
    return error;
}

// #1016's review: the chain of a later block that only earlier ones read emits and runs, every compile; two jumps sharing
// labels whose edges have moves are refused, TIER1_EMIT_UNSUPPORTED, since one label's symbol can't be both jumps'
// stubs; and two sharing labels whose edges have none emit and run, each label's symbol its block's.
static void tier1_labels_review(Tier1EmitTest* test)
{
    const uint32_t all = tier1_liveness_register_mask();
    const uint32_t few = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDX);
    for (uint32_t variant = 0; variant < 8; variant += 1)
    {
        const bool vector = (variant & 1) != 0;
        const bool rematerialize = (variant & 2) != 0;
        const uint32_t mask = (variant & 4) != 0 ? few : all;
        tier1_emit_test_expect(test, tier1_labels_review_compile(test, 0, vector, rematerialize, mask) == TIER1_EMIT_OK, "a later block's chain that only earlier blocks read emits");
        tier1_emit_test_expect(test, tier1_labels_review_compile(test, 1, vector, rematerialize, mask) == TIER1_EMIT_UNSUPPORTED, "two jumps sharing a label whose edges have stubs are refused");
        tier1_emit_test_expect(test, tier1_labels_review_compile(test, 2, vector, rematerialize, mask) == TIER1_EMIT_OK, "two jumps sharing labels whose edges have no moves emit");
    }
}

static void tier1_labels_cases(Tier1EmitTest* test)
{
    const uint32_t all = tier1_liveness_register_mask();
    const uint32_t few = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDX);
    uint32_t stubs = 0;
    test->terms = 0;
    test->width = 8;
    test->selects = false;
    for (uint32_t variant = 0; variant < 8; variant += 1)
    {
        stubs += tier1_labels_compile(test, (variant & 1) != 0, (variant & 2) != 0, (variant & 4) != 0 ? few : all);
    }
    tier1_emit_test_expect(test, stubs == 8, "label 0's edge from the indirect jump has its stub, where its symbol goes, in every compile");
    tier1_labels_review(test);
}

// The cases in a bounded child, as tier1_divide_tests runs its own: a jump to a wrong record can loop, which must fail
// this suite, not hang it. The counter page survives a normal exit.
static void tier1_labels_tests(Tier1EmitTest* test)
{
    uint64_t* report = mmap(0, 3 * sizeof(uint64_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    const pid_t child = report != MAP_FAILED ? fork() : -1;
    if (child == 0)
    {
        signal(SIGALRM, SIG_DFL);
        alarm(60);
        const uint64_t checks = test->checks;
        const uint64_t failures = test->failures;
        tier1_labels_cases(test);
        report[0] = test->checks - checks;
        report[1] = test->failures - failures;
        report[2] = UINT64_C(0x1abe15c0de0fa11d);
        fflush(0);
        _exit(report[1] != 0);
    }
    int status = 0;
    pid_t waited = -1;
    if (child > 0)
    {
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
    }
    const bool finished = child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) <= 1 && report[2] == UINT64_C(0x1abe15c0de0fa11d);
    tier1_emit_test_expect(test, finished, "bounded labels child completes without signal or timeout");
    if (finished)
    {
        test->checks += report[0];
        test->failures += report[1];
        tier1_emit_test_expect(test, WEXITSTATUS(status) == (report[1] != 0), "labels child status agrees with shared test counters");
    }
    else
    {
        fprintf(stderr, "tier1 labels child failed (status=%d)\n", status);
    }
    if (report != MAP_FAILED)
    {
        munmap(report, 3 * sizeof(uint64_t));
    }
}
