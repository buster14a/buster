// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #148 rounding-environment fixtures. The generated leaf and real Tier 0 CALL wrappers use both encoders.
// Clang's __builtin_flt_rounds is an independent native oracle. Only an isolated child saves/changes/restores host
// x87 control and MXCSR, exercising all four modes and deliberately divergent controls. No libm link dependency,
// throughput claim or source integration claim is introduced. Guard pages bound record and encoder publication.

#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>

#define BUSTER_CBUILTIN_ROUNDS_TEST_PAGE 4096u

typedef struct CBuiltinRoundsTestTally CBuiltinRoundsTestTally;
struct CBuiltinRoundsTestTally
{
    uint64_t checks;
    uint64_t failures;
};

typedef struct CBuiltinRoundsTestProgram CBuiltinRoundsTestProgram;
struct CBuiltinRoundsTestProgram
{
    uint8_t* mapping;
    uint8_t* entry;
    uint64_t mapping_bytes;
    uint32_t byte_count;
};

static bool cbuiltin_rounds_test_expect(CBuiltinRoundsTestTally* tally, bool condition, const char* check, uint32_t item)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 24)
        {
            fprintf(stderr, "test-builtin-rounds-helper: %s item %u\n", check, item);
        }
        tally->failures += 1;
    }
    return condition;
}

static bool cbuiltin_rounds_test_contract(const CBuiltinRoundsHelper* helper)
{
    const bool result = helper->code.error == TIER0_OK && helper->code.records != NULL && helper->code.count != 0 &&
        helper->code.branches == NULL && helper->code.targets == NULL && helper->code.branch_count == 0 && helper->code.references == NULL &&
        helper->code.reference_count == 0 && helper->code.deferred == 0 && helper->parameter_count == 0 &&
        helper->result_class == BUSTER_TYPED_CLASS_U32 && helper->compiler_barrier && helper->reads_fp_environment;
    return result;
}

static bool cbuiltin_rounds_test_map(CBuiltinRoundsTestTally* tally, const CBuiltinRoundsHelper* helper, Tier0Code* caller, uint32_t path, CBuiltinRoundsTestProgram* program)
{
    const uint64_t helper_capacity = BUSTER_X86_ENCODER_CAPACITY(helper->code.count);
    const uint64_t caller_offset = (helper_capacity + 15) & ~UINT64_C(15);
    const uint64_t capacity = caller == NULL ? helper_capacity : caller_offset + BUSTER_X86_ENCODER_CAPACITY(caller->count);
    const uint64_t pages = (capacity + BUSTER_CBUILTIN_ROUNDS_TEST_PAGE - 1) & ~(uint64_t)(BUSTER_CBUILTIN_ROUNDS_TEST_PAGE - 1);
    program->mapping_bytes = pages + BUSTER_CBUILTIN_ROUNDS_TEST_PAGE;
    program->mapping = mmap(0, program->mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool result = cbuiltin_rounds_test_expect(tally, program->mapping != MAP_FAILED, "native mapping", path);
    if (result)
    {
        const int64_t encoded = path == 0 ? x86_encode_scalar(helper->code.records, helper->code.count, program->mapping, helper_capacity, NULL, NULL)
                                          : x86_encode(helper->code.records, helper->code.count, program->mapping, helper_capacity, NULL, NULL);
        result = cbuiltin_rounds_test_expect(tally, encoded > 0, "helper encoding", path);
        program->entry = program->mapping;
        program->byte_count = encoded > 0 ? (uint32_t)encoded : 0;
        if (result && caller == NULL)
        {
            X86Instruction records[BUSTER_ARRAY_LENGTH(cbuiltin_rounds_helper_records)];
            uint32_t offsets[BUSTER_ARRAY_LENGTH(records)];
            memcpy(records, helper->code.records, sizeof(records));
            const BranchSizing sized = path == 0 ? size_branches_scalar(records, helper->code.count, NULL, NULL, 0, offsets) :
                                                  size_branches(records, helper->code.count, NULL, NULL, 0, offsets);
            Tier0Code sized_code = helper->code;
            sized_code.records = records;
            UnwindFunction published = { 0 };
            UnwindFunction scalar_frame = { 0 };
            UnwindFunction vector_frame = { 0 };
            const UnwindError described = unwind_tier0_frame(&sized_code, offsets, program->byte_count, &published);
            const UnwindError scalar_proof = unwind_tier0_scalar(&sized_code, offsets, program->byte_count, &scalar_frame);
            const UnwindError vector_proof = unwind_tier0(&sized_code, offsets, program->byte_count, &vector_frame);
            result = cbuiltin_rounds_test_expect(tally, sized.error == BRANCH_SIZING_OK && described == UNWIND_OK &&
                scalar_proof == UNWIND_OK && vector_proof == UNWIND_OK && memcmp(&published, &scalar_frame, sizeof(published)) == 0 &&
                memcmp(&published, &vector_frame, sizeof(published)) == 0 && published.frame_size == 8 && published.event_count == 2 &&
                published.saved == 0 && published.pointer == 0 && published.offsets[0] == 4 && published.offsets[1] == program->byte_count - 1,
                "rounding-mode helper CFA matches both independent unwind proofs", path);
            sized_code.frame_size = 0;
            UnwindFunction wrong = { 0 };
            const UnwindError wrong_frame = unwind_tier0_frame(&sized_code, offsets, program->byte_count, &wrong);
            result = cbuiltin_rounds_test_expect(tally, wrong_frame != UNWIND_OK || memcmp(&wrong, &scalar_frame, sizeof(wrong)) != 0,
                "missing rounding-mode reservation disagrees with the record proof", path) && result;
        }
        if (result && caller != NULL)
        {
            uint32_t ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(1)] = { 0 };
            const int64_t caller_bytes = path == 0 ? x86_encode_scalar(caller->records, caller->count, program->mapping + caller_offset, capacity - caller_offset, NULL, ends)
                                                  : x86_encode(caller->records, caller->count, program->mapping + caller_offset, capacity - caller_offset, NULL, ends);
            result = cbuiltin_rounds_test_expect(tally, caller_bytes > 0 && caller->reference_count == 1 && caller->references[0].target == 1 &&
                caller->references[0].field == X86_FIXUP_DISPLACEMENT && caller->references[0].kind == BUSTER_LINK_FIXUP_CALL &&
                caller->references[0].instruction < caller->count && caller->references[0].addend == 0, "real Tier 0 helper CALL reference", path);
            if (result)
            {
                const LinkReference reference = caller->references[0];
                const uint32_t distance = x86_fixup_distance(caller->records[reference.instruction].form, reference.field);
                result = cbuiltin_rounds_test_expect(tally, distance == 4 && ends[0] >= distance && ends[0] <= (uint64_t)caller_bytes &&
                    caller_offset + ends[0] <= INT32_MAX, "bounded relative helper displacement", path);
                if (result)
                {
                    const int32_t displacement = -(int32_t)(caller_offset + ends[0]);
                    memcpy(program->mapping + caller_offset + ends[0] - distance, &displacement, sizeof(displacement));
                    program->entry = program->mapping + caller_offset;
                    program->byte_count = (uint32_t)caller_bytes;
                }
            }
        }
        result = cbuiltin_rounds_test_expect(tally, result && mprotect(program->mapping, pages, PROT_READ | PROT_EXEC) == 0 &&
            mprotect(program->mapping + pages, BUSTER_CBUILTIN_ROUNDS_TEST_PAGE, PROT_NONE) == 0, "native permissions", path);
    }
    return result;
}

static bool cbuiltin_rounds_test_callers(CBuiltinRoundsTestTally* tally, Arena* arena, const CBuiltinRoundsHelper* helper, CBuiltinRoundsTestProgram programs[2])
{
    const uint64_t mark = arena->length;
    const uint8_t kinds[] = { BUSTER_TYPED_CALL, BUSTER_TYPED_RETURN };
    const uint8_t classes[] = { BUSTER_TYPED_CLASS_U32, BUSTER_TYPED_CLASS_U32 };
    const uint64_t values[] = { 0, 0 };
    const SymbolId target = 1;
    const uint32_t zero = 0;
    const uint8_t variadic = 0;
    const AbiReturn returned =
    {
        .kind = BUSTER_ABI_RESULT_REGISTERS,
        .part_bytes = 4,
        .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
    };
    const AbiCall call =
    {
        .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2,
        .result = returned,
    };
    const Tier0Function function =
    {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .result = returned,
        .call_targets = &target,
        .call_first = &zero,
        .call_argument_counts = &zero,
        .call_variadic = &variadic,
        .calls = &call,
        .call_count = 1,
    };
    const Tier0Check checked = tier0_validate(&function);
    bool result = cbuiltin_rounds_test_expect(tally, checked.error == TIER0_OK, checked.rule != NULL ? checked.rule : "Tier 0 caller validation", 0);
    for (uint32_t path = 0; result && path < 2; path += 1)
    {
        Tier0Code code = path == 0 ? tier0_compile_scalar(&function, arena) : tier0_compile(&function, arena);
        result = cbuiltin_rounds_test_expect(tally, code.error == TIER0_OK && code.branch_count == 0 && code.reference_count == 1 &&
            x86_validate(code.records, code.count) == code.count, "Tier 0 caller compiled to valid records", path);
        if (result)
        {
            result = cbuiltin_rounds_test_map(tally, helper, &code, path, programs + path);
        }
    }
    arena_pop_to(arena, mark);
    return result;
}

static uint16_t cbuiltin_rounds_test_control(void)
{
    uint16_t control;
    __asm__ volatile("fnstcw %0" : "=m"(control) : : "memory");
    return control;
}

static __attribute__((noinline)) int cbuiltin_rounds_test_clang(void)
{
    __asm__ volatile("" : : : "memory");
    const int result = __builtin_flt_rounds();
    return result;
}

// Generated entry points have no Clang function-type prefix.
static __attribute__((no_sanitize("function"))) void cbuiltin_rounds_test_native(CBuiltinRoundsTestTally* tally, CBuiltinRoundsTestProgram direct[2], CBuiltinRoundsTestProgram callers[2])
{
    const uint16_t original_control = cbuiltin_rounds_test_control();
    const uint32_t original_mxcsr = _mm_getcsr();
    static const uint32_t expected[] = { 1, 3, 2, 0 };
    for (uint32_t x87 = 0; x87 < 4; x87 += 1)
    {
        const uint16_t control = (uint16_t)((original_control & ~UINT16_C(0x0c00)) | (x87 << 10));
        for (uint32_t sse = 0; sse < 4; sse += 1)
        {
            const uint32_t mxcsr = (original_mxcsr & ~UINT32_C(0x6000)) | (sse << 13);
            __asm__ volatile("fldcw %0" : : "m"(control) : "memory");
            _mm_setcsr(mxcsr);
            const int oracle = cbuiltin_rounds_test_clang();
            cbuiltin_rounds_test_expect(tally, oracle == (int)expected[x87], "Clang uses x87 rounding when controls diverge", x87 * 4 + sse);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                uint32_t (*leaf)(void) = (uint32_t (*)(void))(void*)direct[path].entry;
                uint32_t (*call)(void) = (uint32_t (*)(void))(void*)callers[path].entry;
                const uint32_t raw = leaf();
                const uint32_t called = call();
                cbuiltin_rounds_test_expect(tally, raw == expected[x87] && called == expected[x87] && raw == (uint32_t)oracle,
                    "native leaf and ordinary helper CALL match Clang", x87 * 8 + sse * 2 + path);
                cbuiltin_rounds_test_expect(tally, cbuiltin_rounds_test_control() == control && _mm_getcsr() == mxcsr,
                    "helper leaves x87 control and MXCSR unchanged", x87 * 8 + sse * 2 + path);
            }
        }
    }
    __asm__ volatile("fldcw %0" : : "m"(original_control) : "memory");
    _mm_setcsr(original_mxcsr);
    cbuiltin_rounds_test_expect(tally, cbuiltin_rounds_test_control() == original_control && _mm_getcsr() == original_mxcsr,
        "isolated child restores original controls", 0);
}

static void cbuiltin_rounds_test_guards(CBuiltinRoundsTestTally* tally)
{
    uint8_t* page = mmap(0, BUSTER_CBUILTIN_ROUNDS_TEST_PAGE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = page != MAP_FAILED;
    const bool protected = mapped && mprotect(page + BUSTER_CBUILTIN_ROUNDS_TEST_PAGE, BUSTER_CBUILTIN_ROUNDS_TEST_PAGE, PROT_NONE) == 0;
    if (cbuiltin_rounds_test_expect(tally, mapped && protected, "publication guard allocation", 0))
    {
        Arena probe = { .base = page, .capacity = BUSTER_CBUILTIN_ROUNDS_TEST_PAGE };
        const CBuiltinRoundsHelper measured = cbuiltin_rounds_helper(CBUILTIN_FLT_ROUNDS, &probe);
        const uint64_t bytes = (uint64_t)measured.code.count * sizeof(X86Instruction);
        const bool bounded = cbuiltin_rounds_test_contract(&measured) && bytes + 64 < BUSTER_CBUILTIN_ROUNDS_TEST_PAGE;
        if (cbuiltin_rounds_test_expect(tally, bounded, "bounded fixed recipe", 0))
        {
            for (uint32_t offset = 0; offset < 16; offset += 1)
            {
                for (uint32_t padding = 0; padding < _Alignof(X86Instruction); padding += 1)
                {
                    for (uint32_t variant = 0; variant < 4; variant += 1)
                    {
                        memset(page, 0xa5, BUSTER_CBUILTIN_ROUNDS_TEST_PAGE);
                        const uint64_t capacity = bytes + 32 + offset;
                        Arena arena =
                        {
                            .base = page + BUSTER_CBUILTIN_ROUNDS_TEST_PAGE - capacity,
                            .length = capacity - bytes - padding,
                            .capacity = capacity - (variant == 1),
                        };
                        if (variant == 3)
                        {
                            arena.length = arena.capacity + 1;
                        }
                        const Arena before = arena;
                        uint8_t snapshot[BUSTER_CBUILTIN_ROUNDS_TEST_PAGE];
                        memcpy(snapshot, page, sizeof(snapshot));
                        const CBuiltinRoundsHelper helper = cbuiltin_rounds_helper(variant == 2 ? CBUILTIN_PREFETCH : CBUILTIN_FLT_ROUNDS, &arena);
                        if (variant == 0)
                        {
                            const bool valid = cbuiltin_rounds_test_contract(&helper) && helper.code.count == measured.code.count &&
                                arena.base == before.base && arena.capacity == before.capacity && arena.length == capacity &&
                                (uint8_t*)helper.code.records == page + BUSTER_CBUILTIN_ROUNDS_TEST_PAGE - bytes &&
                                memcmp(page, snapshot, BUSTER_CBUILTIN_ROUNDS_TEST_PAGE - bytes) == 0;
                            cbuiltin_rounds_test_expect(tally, valid, "exact recipe tail after alignment padding", offset * 100 + padding);
                        }
                        else
                        {
                            const Tier0Error error = variant == 1 ? TIER0_ERROR_MEMORY : variant == 2 ? TIER0_ERROR_UNSUPPORTED : TIER0_ERROR_INVALID;
                            const bool untouched = helper.code.error == error && helper.code.records == NULL && helper.code.count == 0 &&
                                !helper.compiler_barrier && !helper.reads_fp_environment && memcmp(&arena, &before, sizeof(arena)) == 0 &&
                                memcmp(page, snapshot, sizeof(snapshot)) == 0;
                            cbuiltin_rounds_test_expect(tally, untouched, "failed request publishes nothing", offset * 1000 + padding * 10 + variant);
                        }
                    }
                }
            }
        }
        Arena empty = { 0 };
        const CBuiltinRoundsHelper absent = cbuiltin_rounds_helper(CBUILTIN_FLT_ROUNDS, NULL);
        const CBuiltinRoundsHelper exhausted = cbuiltin_rounds_helper(CBUILTIN_FLT_ROUNDS, &empty);
        cbuiltin_rounds_test_expect(tally, absent.code.error == TIER0_ERROR_INVALID && exhausted.code.error == TIER0_ERROR_MEMORY && empty.length == 0,
            "null and exhausted arenas remain bounded", 0);
    }
    if (mapped)
    {
        munmap(page, BUSTER_CBUILTIN_ROUNDS_TEST_PAGE * 2);
    }
}

static int run_builtin_rounds_helper_tests(int argc, char** argv)
{
    (void)argv;
    int result = 1;
    if (argc != 0)
    {
        fprintf(stderr, "usage: build/compiler test-builtin-rounds-helper\n");
    }
    else if (!(x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize()))
    {
        fprintf(stderr, "test-builtin-rounds-helper: table initialization failed\n");
    }
    else
    {
        CBuiltinRoundsTestTally tally = { 0 };
        cbuiltin_rounds_test_guards(&tally);
        Arena arena = arena_reserve(UINT64_C(1) << 24);
        const CBuiltinRoundsHelper helper = cbuiltin_rounds_helper(CBUILTIN_FLT_ROUNDS, &arena);
        CBuiltinRoundsTestProgram direct[2] = { 0 };
        CBuiltinRoundsTestProgram callers[2] = { 0 };
        const X86Target baseline = { 0 };
        uint8_t allowed[X86_FORM_COUNT];
        x86_target_forms(&baseline, allowed);
        bool valid = cbuiltin_rounds_test_expect(&tally, cbuiltin_rounds_test_contract(&helper) && x86_validate(helper.code.records, helper.code.count) == helper.code.count &&
            x86_target_validate(allowed, helper.code.records, helper.code.count) == helper.code.count, "baseline record and ABI contracts", 0);
        for (uint32_t path = 0; valid && path < 2; path += 1)
        {
            valid = cbuiltin_rounds_test_map(&tally, &helper, NULL, path, direct + path);
        }
        valid = valid && cbuiltin_rounds_test_callers(&tally, &arena, &helper, callers);
        if (valid)
        {
            valid = cbuiltin_rounds_test_expect(&tally, direct[0].byte_count == direct[1].byte_count && callers[0].byte_count == callers[1].byte_count &&
                memcmp(direct[0].entry, direct[1].entry, direct[0].byte_count) == 0 && memcmp(callers[0].entry, callers[1].entry, callers[0].byte_count) == 0,
                "scalar/vector helper and patched caller bytes", 0);
        }
        CBuiltinRoundsTestTally* shared = MAP_FAILED;
        if (valid)
        {
            shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
            valid = cbuiltin_rounds_test_expect(&tally, shared != MAP_FAILED, "native result mapping", 0);
        }
        if (valid)
        {
            fflush(NULL);
            const pid_t child = fork();
            if (child == 0)
            {
                const struct rlimit no_core = { 0 };
                if (signal(SIGALRM, SIG_DFL) == SIG_ERR || setrlimit(RLIMIT_CORE, &no_core) != 0)
                {
                    _exit(98);
                }
                alarm(5);
                cbuiltin_rounds_test_native(shared, direct, callers);
                _exit(shared->failures == 0 ? 0 : 1);
            }
            int status = 0;
            const bool waited = child > 0 && waitpid(child, &status, 0) == child;
            cbuiltin_rounds_test_expect(&tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0, "bounded native environment child", (uint32_t)status);
        }
        if (shared != MAP_FAILED)
        {
            tally.checks += shared->checks;
            tally.failures += shared->failures;
            munmap(shared, sizeof(*shared));
        }
        for (uint32_t path = 0; path < 2; path += 1)
        {
            if (direct[path].mapping != NULL && direct[path].mapping != MAP_FAILED)
            {
                munmap(direct[path].mapping, direct[path].mapping_bytes);
            }
            if (callers[path].mapping != NULL && callers[path].mapping != MAP_FAILED)
            {
                munmap(callers[path].mapping, callers[path].mapping_bytes);
            }
        }
        arena_release(&arena);
        printf("test-builtin-rounds-helper: %" PRIu64 " checks, %" PRIu64 " failed\n", tally.checks, tally.failures);
        result = tally.failures == 0 ? 0 : 1;
    }
    return result;
}
