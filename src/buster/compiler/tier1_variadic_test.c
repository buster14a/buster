// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent native fixtures for variadic definitions (#789), included after tier1_labels_test.c: the functions of
// tier1_select_test_variadic_function through tier1_emit_function, which gives them to the CFG consumer, by both paths,
// with constants rematerialized and not, with every register and with four, and with and without a kept frame pointer.
// Each is called natively as C calls a variadic function, uint64_t (*)(uint64_t, ...): n and five unnamed integers in
// rdi to r9, two more on the stack and, for shapes 1 and 2, a double in xmm0 with al 1, which the prologue saves in the
// register save area; shapes 0 and 3 are called without the double too, with al 0, which skips the vector saves. Shape
// 3's first unnamed argument is its callee's address, and its values live across the call make its prologue push
// callee-saved registers, which the overflow area's address must count past where the frame keeps no pointer. Every compile
// must save the six argument registers and the eight vector ones in its prologue, those behind al's test, in the
// frame's save area, and take the overflow area's address in one lea past the frame its prologue's records build.
// tier1_select_test_variadic_oracle reads only the arguments; the code runs in a bounded child (tier1_variadic_tests).

#define BUSTER_TIER1_VARIADIC_SAMPLES 32u

// The fixture called as C calls a variadic function, with or without the double, which decides al.
static __attribute__((no_sanitize("function"))) uint64_t tier1_variadic_call(const void* code, const uint64_t* integers, const uint64_t* stacked, double vector, bool with_vector)
{
    uint64_t (*function)(uint64_t, ...);
    memcpy(&function, &code, sizeof(function));
    return with_vector ? function(integers[0], integers[1], integers[2], integers[3], integers[4], integers[5], stacked[0], stacked[1], vector)
                       : function(integers[0], integers[1], integers[2], integers[3], integers[4], integers[5], stacked[0], stacked[1]);
}

// Whether the prologue (the records before the first node's) saves rdi to r9 and, behind al's test, xmm0 to xmm7 at the
// save area's slots: Tier1Frame.variadic_offset from the frame's base, 8 bytes apart, then 16 apart from byte 48.
static bool tier1_variadic_prologue(const Tier1EmitResult* emitted)
{
    const Tier1SelectCode* code = &emitted->code;
    const Tier1Frame* frame = &emitted->frame;
    uint32_t general = 0;
    uint32_t vectors = 0;
    bool tested = false;
    const uint32_t end = code->node_first != 0 ? code->node_first[0] : 0;
    for (uint32_t r = 0; r < end && r < code->count; r += 1)
    {
        const X86Instruction* record = &code->records[r];
        const uint32_t form = record->form & BUSTER_X86_FORM_NUMBER;
        const bool frame_base = record->mode == BUSTER_X86_MEMORY_BASE && record->rm == X86_REGISTER_RSP;
        for (uint32_t k = 0; k < BUSTER_ABI_INTEGER_ARGUMENTS; k += 1)
        {
            general |= form == X86_FORM_MOV_RM64_R64 && frame_base && record->reg == abi_integer_arguments[k] && record->displacement == frame->variadic_offset + (int32_t)(8 * k) ? 1u << k : 0;
        }
        tested |= form == X86_FORM_TEST_RM8_IMM8 && record->rm == X86_REGISTER_RAX;
        for (uint32_t k = 0; tested && k < BUSTER_ABI_VECTOR_ARGUMENTS; k += 1)
        {
            const int32_t slot = frame->variadic_offset + BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET + (int32_t)(BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES * k);
            vectors |= form == X86_FORM_MOVAPS_XM_X && frame_base && record->reg == k && record->displacement == slot ? 1u << k : 0;
        }
    }
    return frame->variadic && general == (1u << BUSTER_ABI_INTEGER_ARGUMENTS) - 1 && vectors == (1u << BUSTER_ABI_VECTOR_ARGUMENTS) - 1;
}

// Whether the code takes the overflow area's address in one lea at the first stack argument, read off the prologue's
// records rather than the frame's plan: rbp + 16 where the prologue sets rbp from rsp, and otherwise rsp + 8 per
// register it pushes, plus its reservation, plus 8 for the return address; these shapes have no named stack arguments.
static bool tier1_variadic_overflow(const Tier1EmitResult* emitted)
{
    const Tier1SelectCode* code = &emitted->code;
    const uint32_t end = code->node_first != 0 ? code->node_first[0] : 0;
    int64_t pushes = 0;
    int64_t reservation = 0;
    bool pointer = false;
    for (uint32_t r = 0; r < end && r < code->count; r += 1)
    {
        const X86Instruction* record = &code->records[r];
        const uint32_t form = record->form & BUSTER_X86_FORM_NUMBER;
        pushes += form == X86_FORM_PUSH_R64;
        pointer |= form == X86_FORM_MOV_RM64_R64 && (record->mode & BUSTER_X86_MODE_MEMORY) == 0 && record->rm == X86_REGISTER_RBP && record->reg == X86_REGISTER_RSP;
        reservation += (form == X86_FORM_SUB_RM64_IMM8 || form == X86_FORM_SUB_RM64_IMM32) && (record->mode & BUSTER_X86_MODE_MEMORY) == 0 && record->rm == X86_REGISTER_RSP ? record->immediate : 0;
    }
    const int64_t displacement = pointer ? 16 : 8 * pushes + reservation + 8;
    const uint8_t anchor = pointer ? X86_REGISTER_RBP : X86_REGISTER_RSP;
    uint32_t found = 0;
    for (uint32_t r = end; r < code->count; r += 1)
    {
        const X86Instruction* record = &code->records[r];
        found += (record->form & BUSTER_X86_FORM_NUMBER) == X86_FORM_LEA_R64_M && record->mode == BUSTER_X86_MEMORY_BASE && record->rm == anchor && record->displacement == displacement;
    }
    return found == 1;
}

// One compile by one path and one set of options, its prologue checked and its code run on the samples.
static void tier1_variadic_compile(Tier1EmitTest* test, uint32_t shape, bool vector, bool rematerialize, uint32_t register_mask, bool keep_pointer)
{
    IrFunction function;
    const bool created = tier1_select_test_variadic_function(&test->input, shape, false, &function);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = created && tier1_select_plan(&function, 0, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the variadic fixture selects");
    const Tier1EmitOptions options = {
        .register_mask = register_mask,
        .vector = vector,
        .rematerialize = rematerialize,
        .frame_flags = keep_pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
        .symbol_count = function.symbol_count,
    };
    Tier1EmitResult emitted;
    const uint64_t scratch_mark = test->scratch.length;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    tier1_emit_test_expect(test, error == TIER1_EMIT_OK && test->scratch.length == scratch_mark, "the variadic fixture emits through the CFG consumer");
    if (error != TIER1_EMIT_OK && planned && test->failures < 20)
    {
        fprintf(stderr, "  variadic emission error=%u node=%u shape=%u vector=%u remat=%u mask=%x pointer=%u\n", error, node, shape, vector, rematerialize, register_mask, keep_pointer);
    }
    if (error == TIER1_EMIT_OK)
    {
        tier1_emit_test_expect(test, emitted.general && tier1_variadic_prologue(&emitted), "the prologue saves the argument registers, and the vector ones behind al's test, in the frame's save area");
        tier1_emit_test_expect(test, (shape == 2 || keep_pointer) == emitted.frame.frame_pointer, "a realigned frame or a kept frame pointer gives the overflow area's address its base");
        tier1_emit_test_expect(test, shape != 3 || emitted.frame.saved_registers != 0, "the values live across shape 3's call are in callee-saved registers, which its prologue pushes");
        tier1_emit_test_expect(test, tier1_variadic_overflow(&emitted), "the overflow area's address is one lea at rbp + 16, or past rsp's pushes, its reservation and the return address");
        void* executable = MAP_FAILED;
        uint64_t bytes = 0;
        if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
        {
            uint64_t random = UINT64_C(0x7a3c19e5d2b84f60) ^ ((uint64_t)register_mask << 8) ^ (vector ? 1 : 0) ^ (rematerialize ? 2 : 0) ^ (keep_pointer ? 4 : 0) ^ ((uint64_t)shape << 40);
            for (uint32_t sample = 0; sample < BUSTER_TIER1_VARIADIC_SAMPLES; sample += 1)
            {
                uint64_t integers[BUSTER_ABI_INTEGER_ARGUMENTS];
                uint64_t stacked[2];
                for (uint32_t k = 0; k < BUSTER_ABI_INTEGER_ARGUMENTS; k += 1)
                {
                    integers[k] = sample < 2 ? (sample == 0 ? k + 1 : UINT64_MAX - k) : tier1_emit_test_random(&random);
                }
                integers[1] = shape == 3 ? (uint64_t)(uintptr_t)tier1_select_test_variadic_callee : integers[1];
                stacked[0] = tier1_emit_test_random(&random);
                stacked[1] = tier1_emit_test_random(&random);
                // A double whose bits the oracle reads as xmm0's low eightbyte: any finite value's.
                const double value = (double)(int64_t)tier1_emit_test_random(&random) / 3.0;
                uint64_t bits = 0;
                memcpy(&bits, &value, sizeof(bits));
                // Shapes 0 and 3 read no vector slot: called with the double (al 1) and without it (al 0) in turn.
                const bool with_vector = shape == 1 || shape == 2 || (sample & 1) != 0;
                const uint64_t expected = tier1_select_test_variadic_oracle(shape, integers, stacked, bits);
                const uint64_t actual = tier1_variadic_call(executable, integers, stacked, value, with_vector);
                if (actual != expected && test->failures < 20)
                {
                    fprintf(stderr, "  variadic shape=%u vector=%u remat=%u mask=%x pointer=%u al=%u actual=%016" PRIx64 " expected=%016" PRIx64 "\n", shape, vector, rematerialize, register_mask, keep_pointer,
                            with_vector, actual, expected);
                }
                tier1_emit_test_expect(test, actual == expected, "a variadic definition reads its arguments from its save area and its overflow area natively");
            }
        }
        if (executable != MAP_FAILED)
        {
            munmap(executable, bytes);
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->input, 0);
}

static void tier1_variadic_cases(Tier1EmitTest* test)
{
    const uint32_t all = tier1_liveness_register_mask();
    const uint32_t few = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI) | (1u << X86_REGISTER_RDX);
    test->terms = 0;
    test->width = 8;
    test->selects = false;
    for (uint32_t shape = 0; shape < BUSTER_TIER1_SELECT_TEST_VARIADIC_SHAPES; shape += 1)
    {
        for (uint32_t variant = 0; variant < 16; variant += 1)
        {
            tier1_variadic_compile(test, shape, (variant & 1) != 0, (variant & 2) != 0, (variant & 4) != 0 ? few : all, (variant & 8) != 0);
        }
    }
}

// The cases in a bounded child, as tier1_labels_tests runs its own: a frame that misplaces the save area can fault or
// loop, which must fail this suite, not stop it. The counter page survives a normal exit.
static void tier1_variadic_tests(Tier1EmitTest* test)
{
    uint64_t* report = mmap(0, 3 * sizeof(uint64_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    const pid_t child = report != MAP_FAILED ? fork() : -1;
    if (child == 0)
    {
        signal(SIGALRM, SIG_DFL);
        alarm(60);
        const uint64_t checks = test->checks;
        const uint64_t failures = test->failures;
        tier1_variadic_cases(test);
        report[0] = test->checks - checks;
        report[1] = test->failures - failures;
        report[2] = UINT64_C(0x7a91ad1cf4a3e0b5);
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
    const bool finished = child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) <= 1 && report[2] == UINT64_C(0x7a91ad1cf4a3e0b5);
    tier1_emit_test_expect(test, finished, "bounded variadic child completes without signal or timeout");
    if (finished)
    {
        test->checks += report[0];
        test->failures += report[1];
        tier1_emit_test_expect(test, WEXITSTATUS(status) == (report[1] != 0), "variadic child status agrees with shared test counters");
    }
    else
    {
        fprintf(stderr, "tier1 variadic child failed (status=%d)\n", status);
    }
    if (report != MAP_FAILED)
    {
        munmap(report, 3 * sizeof(uint64_t));
    }
}
