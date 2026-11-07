// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1450: dynamic outgoing call areas larger than a page. Included by tier1_call_test.c. The controlled-stack caller
// from tier1_frame_execution_test.c supplies six integer arguments, eight vector arguments, AL, and callee-save
// sentinels. Only the outgoing-area adjustment comes from the consumer under test. Its release must restore rsp and
// preserve every argument and R10 (a possible indirect callee); only reserved R11 may change. Guard pages distinguish
// intermediate-page and final-partial-page probes from an unprobed subtraction that skips them.

static uint8_t* tier1_call_probe_function(Tier1FrameExecutionTest* test, uint32_t area, bool probe)
{
    X86Instruction records[BUSTER_TIER1_FRAME_EXEC_RECORDS];
    uint32_t branches[32];
    uint32_t targets[32];
    Tier1EmitBuffer buffer = {
        .records = records,
        .capacity = BUSTER_TIER1_FRAME_EXEC_RECORDS,
        .branches = branches,
        .targets = targets,
        .branch_capacity = 32,
    };
    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_PUSH_R64, 0, X86_REGISTER_RBP));
    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_RBP, X86_REGISTER_RSP));
    bool valid = tier1_emit_cfg_adjust(area, false, probe, &buffer) == TIER1_EMIT_OK;
    valid &= tier1_emit_cfg_adjust(area, true, probe, &buffer) == TIER1_EMIT_OK;
    tier1_frame_execution_expect(test, valid, "outgoing page area and release emit without refusal");
    // Probe branches already have their own targets; the checks below share a failure return.
    const uint32_t first_check = buffer.branch_count;
    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_CMP_RM64_R64, X86_REGISTER_RBP, X86_REGISTER_RSP));
    tier1_frame_emit_branch(&buffer, X86_FORM_JNE_REL8, 0);
    tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_CMP_RM64_IMM32, X86_REGISTER_RAX, 8));
    tier1_frame_emit_branch(&buffer, X86_FORM_JNE_REL8, 0);
    tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_R11, (uintptr_t)test->vectors));
    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_CMP_RM64_R64, X86_REGISTER_R11, X86_REGISTER_R10));
    tier1_frame_emit_branch(&buffer, X86_FORM_JNE_REL8, 0);
    for (uint32_t i = 0; i < BUSTER_ABI_INTEGER_ARGUMENTS; i += 1)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_CMP_RM64_IMM32, abi_integer_arguments[i], (int32_t)tier1_frame_execution_argument(i)));
        tier1_frame_emit_branch(&buffer, X86_FORM_JNE_REL8, 0);
    }
    for (uint32_t i = 0; i < BUSTER_ABI_VECTOR_ARGUMENTS; i += 1)
    {
        tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOVUPS_XM_X, i, X86_REGISTER_RSP, -16));
        for (uint32_t half = 0; half < 2; half += 1)
        {
            tier1_frame_emit_add(&buffer, tier1_frame_emit_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RSP, -16 + (int32_t)(8 * half)));
            tier1_frame_emit_add(&buffer, tier1_frame_emit_immediate(X86_FORM_CMP_RM64_IMM32, X86_REGISTER_RAX, (int32_t)test->vectors[2 * i + half]));
            tier1_frame_emit_branch(&buffer, X86_FORM_JNE_REL8, 0);
        }
    }
    tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_RAX, 0));
    const uint32_t done = tier1_frame_emit_branch(&buffer, X86_FORM_JMP_REL8, 0);
    for (uint32_t i = first_check; i < done; i += 1)
    {
        buffer.targets[i] = buffer.count;
    }
    tier1_frame_emit_add(&buffer, tier1_frame_execution_constant(X86_REGISTER_RAX, 1));
    buffer.targets[done] = buffer.count;
    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_RSP, X86_REGISTER_RBP));
    tier1_frame_emit_add(&buffer, tier1_frame_emit_register(X86_FORM_POP_R64, 0, X86_REGISTER_RBP));
    tier1_frame_emit_add(&buffer, (X86Instruction){ .form = X86_FORM_RET });
    return valid ? tier1_frame_execution_load(test, &buffer, 0) : 0;
}

static void tier1_call_probe_capacity(Tier1EmitTest* test, uint32_t area)
{
    X86Instruction records[16];
    X86Instruction before[16];
    uint32_t branches[1];
    uint32_t targets[1];
    for (uint32_t release = 0; release < 2; release += 1)
    {
        const uint32_t needed = area == 0 ? 0 : release != 0 ? 1 : area < 4096 ? 2 : area % 4096 == 0 ? 6 : 8;
        const uint32_t jumps = release == 0 && area >= 4096 ? 1 : 0;
        // One prefix record makes the loop's absolute target observable. An exact buffer must suffice; one missing
        // record or branch must fail before the unchecked frame helper writes any records or branch entries.
        for (uint32_t shortage = 0; shortage < 1u + (needed != 0) + (jumps != 0); shortage += 1)
        {
            memset(records, 0xa5, sizeof(records));
            records[0] = tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_R10, X86_REGISTER_R10);
            memcpy(before, records, sizeof(records));
            branches[0] = UINT32_MAX;
            targets[0] = UINT32_MAX;
            Tier1EmitBuffer buffer = {
                .records = records,
                .count = 1,
                .capacity = 1 + needed - (shortage == 1),
                .branches = branches,
                .targets = targets,
                .branch_capacity = jumps - (shortage == 2),
            };
            const Tier1EmitError error = tier1_emit_cfg_adjust(area, release != 0, true, &buffer);
            const bool intact = buffer.count == 1 && buffer.branch_count == 0 && memcmp(records, before, sizeof(records)) == 0 && branches[0] == UINT32_MAX && targets[0] == UINT32_MAX;
            tier1_emit_test_expect(test, shortage == 0 ? error == TIER1_EMIT_OK && buffer.count == 1 + needed && buffer.branch_count == jumps : error == TIER1_EMIT_MEMORY && intact,
                                   "call probes fit exact capacity and refuse short columns transactionally");
            if (shortage == 0)
            {
                const BranchSizingCheck check = branch_sizing_validate(records, buffer.count, branches, targets, buffer.branch_count);
                tier1_emit_test_expect(test, check.error == BRANCH_SIZING_OK, "call probe loop target includes the preceding records");
            }
        }
    }
}

// rsp's alignment in a dynamic frame is its calls' (tier1_emit_cfg_stack_alignment): a frame realigned to a megabyte for
// an object rounds neither its allocations nor its calls' areas to that, but still to a 64-byte vector argument's.
static void tier1_call_probe_alignment(Tier1EmitTest* test)
{
    static const uint8_t calls[] = { 0, 4, 5, 6 };
    static const uint32_t frames[] = { 16, 4096, 1u << 20 };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(frames); f += 1)
    {
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(calls); c += 1)
        {
            Tier1EmitResult emitted = { 0 };
            emitted.frame.alignment = frames[f];
            emitted.frame.dynamic_outgoing = true;
            emitted.prepared.call_abi.stack_alignment_log2 = calls[c];
            const uint32_t expected = calls[c] > 4 ? 1u << calls[c] : 16;
            tier1_emit_test_expect(test, tier1_emit_cfg_stack_alignment(&emitted) == (uint8_t)__builtin_ctz(expected) &&
                                   tier1_emit_cfg_call_area(&emitted, 24) == ((24 + expected - 1) & ~(expected - 1)) &&
                                   tier1_emit_cfg_call_area(&emitted, 0) == 0,
                                   "a dynamic frame keeps rsp at its calls' alignment, not its objects'");
        }
    }
}

static void tier1_call_probe_tests(Tier1EmitTest* test)
{
    tier1_call_probe_alignment(test);
    Tier1FrameExecutionTest native = {
        .allowed = test->allowed,
        .code = mmap(0, 2 * BUSTER_TIER1_FRAME_EXEC_CODE_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0),
        .stack = mmap(0, BUSTER_TIER1_FRAME_EXEC_STACK_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0),
    };
    const bool mapped = native.code != MAP_FAILED && native.stack != MAP_FAILED;
    tier1_emit_test_expect(test, mapped, "map controlled stack and outgoing-call probe code");
    for (uint32_t i = 0; i < 16; i += 1)
    {
        native.vectors[i] = 0x654300u + i * 267;
    }
    static const uint32_t areas[] = { 0, 16, 4096, 4112, 12288, 12304 };
    for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(areas); a += 1)
    {
        const uint32_t area = areas[a];
        tier1_call_probe_capacity(test, area);
        // The caller's return address and callee's saved rbp leave rsp at this page boundary. At 4112 the guard
        // catches only the final partial step; at 12304 it catches an intermediate page. Argument checks stay above it.
        const uintptr_t base = ((uintptr_t)native.stack + 256 * 1024) & ~(uintptr_t)4095;
        const uintptr_t guard = base - 8192;
        for (uint32_t protect = 0; mapped && protect < 1u + (area == 4112 || area == 12304); protect += 1)
        {
            for (uint32_t probe = 0; probe < 2; probe += 1)
            {
                uint8_t* function = tier1_call_probe_function(&native, area, probe != 0);
                uint8_t* caller = function != 0 ? tier1_frame_execution_caller(&native, function, base + 16, 8) : 0;
                const bool protection = mprotect((void*)guard, 4096, protect != 0 ? PROT_NONE : PROT_READ | PROT_WRITE) == 0;
                tier1_frame_execution_expect(&native, protection, "install outgoing-area guard page");
                if (caller != 0 && protection)
                {
                    fflush(0);
                    const pid_t child = fork();
                    if (child == 0)
                    {
                        signal(SIGSEGV, SIG_DFL);
                        signal(SIGBUS, SIG_DFL);
                        alarm(5);
                        const struct rlimit no_core = {0};
                        setrlimit(RLIMIT_CORE, &no_core);
                        _exit(tier1_frame_execution_invoke(caller) != 0);
                    }
                    int status = 0;
                    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
                    const bool fault = waited && WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS);
                    const bool passed = protect != 0 && probe != 0 ? fault : waited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
                    tier1_frame_execution_expect(&native, passed, "outgoing probes preserve arguments and fault only when the guard is protected");
                }
                tier1_frame_execution_expect(&native, mprotect((void*)guard, 4096, PROT_READ | PROT_WRITE) == 0, "restore outgoing-area guard page");
            }
        }
    }
    test->checks += native.checks;
    test->failures += native.failures;
    if (native.stack != MAP_FAILED)
    {
        munmap(native.stack, BUSTER_TIER1_FRAME_EXEC_STACK_BYTES);
    }
    if (native.code != MAP_FAILED)
    {
        munmap(native.code, 2 * BUSTER_TIER1_FRAME_EXEC_CODE_BYTES);
    }
}
