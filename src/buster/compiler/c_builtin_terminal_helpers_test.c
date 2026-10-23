// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #148 terminal-helper tests. Validate baseline policy, exact records and encodings independently of
// native behavior. Each executable helper runs in its own bounded child and must die with SIGILL, never return.
// Guarded arenas check exact record capacity, alignment padding and unchanged failure state. No timing claim.

#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>

#define BUSTER_CBUILTIN_TERMINAL_TEST_PAGE 4096u

typedef struct CBuiltinTerminalTestTally CBuiltinTerminalTestTally;
struct CBuiltinTerminalTestTally
{
    uint64_t checks;
    uint64_t failures;
};

static bool cbuiltin_terminal_test_expect(CBuiltinTerminalTestTally* tally, bool condition, const char* check, uint32_t item)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 24)
        {
            fprintf(stderr, "test-builtin-terminal-helpers: %s item %u\n", check, item);
        }
        tally->failures += 1;
    }
    return condition;
}

static bool cbuiltin_terminal_test_contract(const CBuiltinTerminalHelper* helper)
{
    const X86Instruction expected = { .form = X86_FORM_UD2 };
    const bool result = helper->code.error == TIER0_OK && helper->code.records != 0 && helper->code.count == 1 &&
        helper->code.branches == 0 && helper->code.targets == 0 && helper->code.branch_count == 0 && helper->code.references == 0 &&
        helper->code.reference_count == 0 && helper->code.deferred == 0 && helper->parameter_count == 0 &&
        helper->result_class == BUSTER_TYPED_CLASS_VOID && helper->terminates && memcmp(helper->code.records, &expected, sizeof(expected)) == 0;
    return result;
}

static void cbuiltin_terminal_test_guards(CBuiltinTerminalTestTally* tally)
{
    uint8_t* page = mmap(0, BUSTER_CBUILTIN_TERMINAL_TEST_PAGE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = page != MAP_FAILED;
    const bool protected = mapped && mprotect(page + BUSTER_CBUILTIN_TERMINAL_TEST_PAGE, BUSTER_CBUILTIN_TERMINAL_TEST_PAGE, PROT_NONE) == 0;
    if (cbuiltin_terminal_test_expect(tally, mapped && protected, "guard allocation", 0))
    {
        for (uint32_t offset = 0; offset < 16; offset += 1)
        {
            for (uint32_t padding = 0; padding < _Alignof(X86Instruction); padding += 1)
            {
                for (uint32_t variant = 0; variant < 4; variant += 1)
                {
                    memset(page, 0xa5, BUSTER_CBUILTIN_TERMINAL_TEST_PAGE);
                    const uint64_t capacity = 64 + offset;
                    Arena arena =
                    {
                        .base = page + BUSTER_CBUILTIN_TERMINAL_TEST_PAGE - capacity,
                        .length = capacity - sizeof(X86Instruction) - padding,
                        .capacity = capacity - (variant == 1),
                    };
                    if (variant == 3)
                    {
                        arena.length = arena.capacity + 1;
                    }
                    const Arena before = arena;
                    uint8_t snapshot[BUSTER_CBUILTIN_TERMINAL_TEST_PAGE];
                    memcpy(snapshot, page, sizeof(snapshot));
                    const CBuiltinId id = variant == 2 ? CBUILTIN_MEMCPY : (offset & 1) != 0 ? CBUILTIN_UNREACHABLE : CBUILTIN_TRAP;
                    const CBuiltinTerminalHelper helper = cbuiltin_terminal_helper(id, &arena);
                    if (variant == 0)
                    {
                        bool valid = cbuiltin_terminal_test_contract(&helper) && arena.length == capacity && arena.capacity == before.capacity && arena.base == before.base;
                        valid &= (uint8_t*)helper.code.records == page + BUSTER_CBUILTIN_TERMINAL_TEST_PAGE - sizeof(X86Instruction);
                        valid &= memcmp(page, snapshot, BUSTER_CBUILTIN_TERMINAL_TEST_PAGE - sizeof(X86Instruction)) == 0;
                        cbuiltin_terminal_test_expect(tally, valid, "one exact record after alignment padding", offset * 100 + padding);
                    }
                    else
                    {
                        const Tier0Error error = variant == 1 ? TIER0_ERROR_MEMORY : variant == 2 ? TIER0_ERROR_UNSUPPORTED : TIER0_ERROR_INVALID;
                        const bool untouched = helper.code.error == error && helper.code.records == 0 && helper.code.count == 0 && !helper.terminates &&
                            memcmp(&arena, &before, sizeof(arena)) == 0 && memcmp(page, snapshot, sizeof(snapshot)) == 0;
                        cbuiltin_terminal_test_expect(tally, untouched, "failed request is transactional", offset * 1000 + padding * 10 + variant);
                    }
                }
            }
        }
        Arena arena = { .base = page, .capacity = BUSTER_CBUILTIN_TERMINAL_TEST_PAGE };
        const Arena before = arena;
        const CBuiltinTerminalHelper unsupported = cbuiltin_terminal_helper((CBuiltinId)UINT32_MAX, &arena);
        const CBuiltinTerminalHelper null_arena = cbuiltin_terminal_helper(CBUILTIN_TRAP, 0);
        Arena empty = { 0 };
        const CBuiltinTerminalHelper no_space = cbuiltin_terminal_helper(CBUILTIN_TRAP, &empty);
        Arena missing_base = { .capacity = sizeof(X86Instruction) };
        const Arena missing_before = missing_base;
        const CBuiltinTerminalHelper malformed = cbuiltin_terminal_helper(CBUILTIN_TRAP, &missing_base);
        cbuiltin_terminal_test_expect(tally, unsupported.code.error == TIER0_ERROR_UNSUPPORTED && memcmp(&arena, &before, sizeof(arena)) == 0 &&
            null_arena.code.error == TIER0_ERROR_INVALID && no_space.code.error == TIER0_ERROR_MEMORY && empty.length == 0 &&
            malformed.code.error == TIER0_ERROR_INVALID && memcmp(&missing_base, &missing_before, sizeof(missing_base)) == 0, "unknown IDs and invalid arenas", 0);
    }
    if (mapped)
    {
        munmap(page, BUSTER_CBUILTIN_TERMINAL_TEST_PAGE * 2);
    }
}

static void cbuiltin_terminal_test_native(CBuiltinTerminalTestTally* tally, const CBuiltinTerminalHelper* helper, uint32_t item)
{
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(helper->code.count);
    uint8_t* mapping = mmap(0, BUSTER_CBUILTIN_TERMINAL_TEST_PAGE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = mapping != MAP_FAILED;
    bool ready = mapped && capacity < BUSTER_CBUILTIN_TERMINAL_TEST_PAGE;
    uint8_t* entry = ready ? mapping + BUSTER_CBUILTIN_TERMINAL_TEST_PAGE - capacity : 0;
    uint8_t scalar[BUSTER_X86_ENCODER_CAPACITY(1)];
    int64_t vector_size = -1;
    int64_t scalar_size = -1;
    if (ready)
    {
        memset(mapping, 0xa5, BUSTER_CBUILTIN_TERMINAL_TEST_PAGE);
        ready = mprotect(mapping + BUSTER_CBUILTIN_TERMINAL_TEST_PAGE, BUSTER_CBUILTIN_TERMINAL_TEST_PAGE, PROT_NONE) == 0;
    }
    if (ready)
    {
        vector_size = x86_encode(helper->code.records, helper->code.count, entry, capacity, 0, 0);
        scalar_size = x86_encode_scalar(helper->code.records, helper->code.count, scalar, sizeof(scalar), 0, 0);
        ready = vector_size == 2 && scalar_size == 2 && entry[0] == 0x0f && entry[1] == 0x0b && memcmp(entry, scalar, 2) == 0;
        ready &= mprotect(mapping, BUSTER_CBUILTIN_TERMINAL_TEST_PAGE, PROT_READ | PROT_EXEC) == 0;
    }
    if (cbuiltin_terminal_test_expect(tally, ready, "both encoders emit exact UD2 with bounded output", item))
    {
        fflush(0);
        const pid_t child = fork();
        if (child == 0)
        {
            const struct rlimit no_core = { 0 };
            const bool contained = signal(SIGILL, SIG_DFL) != SIG_ERR && signal(SIGALRM, SIG_DFL) != SIG_ERR && setrlimit(RLIMIT_CORE, &no_core) == 0;
            if (!contained)
            {
                _exit(98);
            }
            alarm(2);
            void (*invoke)(void) = (void (*)(void))(void*)entry;
            invoke();
            _exit(99);
        }
        int status = 0;
        const bool waited = child > 0 && waitpid(child, &status, 0) == child;
        cbuiltin_terminal_test_expect(tally, waited && WIFSIGNALED(status) && WTERMSIG(status) == SIGILL, "terminal helper faults with SIGILL and never returns", item);
    }
    if (mapped)
    {
        munmap(mapping, BUSTER_CBUILTIN_TERMINAL_TEST_PAGE * 2);
    }
}

static int run_builtin_terminal_helper_tests(int argc, char** argv)
{
    (void)argv;
    int result = 1;
    if (argc != 0)
    {
        fprintf(stderr, "usage: build/compiler test-builtin-terminal-helpers\n");
    }
    else if (!(x86_encoder_initialize() && x86_semantics_initialize()))
    {
        fprintf(stderr, "test-builtin-terminal-helpers: table initialization failed\n");
    }
    else
    {
        CBuiltinTerminalTestTally tally = { 0 };
        cbuiltin_terminal_test_guards(&tally);
        Arena arena = arena_reserve(BUSTER_CBUILTIN_TERMINAL_TEST_PAGE);
        const X86Target baseline = { 0 };
        uint8_t allowed[X86_FORM_COUNT];
        uint8_t allowed_scalar[X86_FORM_COUNT];
        x86_target_forms(&baseline, allowed);
        x86_target_forms_scalar(&baseline, allowed_scalar);
        cbuiltin_terminal_test_expect(&tally, memcmp(allowed, allowed_scalar, sizeof(allowed)) == 0 && x86_target_form_allowed(allowed, X86_FORM_UD2), "UD2 is explicitly eligible at baseline", 0);
        const X86Effects* effects = x86_effects(X86_FORM_UD2);
        cbuiltin_terminal_test_expect(&tally, effects->operation == X86_OPERATION_TRAP && x86_effects_control(effects) == BUSTER_X86_CONTROL_TRAP &&
            (effects->properties & BUSTER_X86_EFFECT_FAULT) != 0, "shared semantics retain faulting terminal control", 0);
        for (uint32_t i = 0; i < 2; i += 1)
        {
            const CBuiltinTerminalHelper helper = cbuiltin_terminal_helper(i == 0 ? CBUILTIN_TRAP : CBUILTIN_UNREACHABLE, &arena);
            const bool valid = cbuiltin_terminal_test_contract(&helper);
            if (cbuiltin_terminal_test_expect(&tally, valid, "terminal ABI and code contract", i))
            {
                const bool eligible = x86_validate(helper.code.records, helper.code.count) == helper.code.count &&
                    x86_target_validate(allowed, helper.code.records, helper.code.count) == helper.code.count;
                if (cbuiltin_terminal_test_expect(&tally, eligible, "record validity and selected baseline policy", i))
                {
                    cbuiltin_terminal_test_native(&tally, &helper, i);
                }
            }
        }
        arena_release(&arena);
        printf("test-builtin-terminal-helpers: %" PRIu64 " checks, %" PRIu64 " failed\n", tally.checks, tally.failures);
        result = tally.failures == 0 ? 0 : 1;
    }
    return result;
}
