// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's raw-bit scalar math helper consumers. The independent numeric oracle is the host libm loaded only by
// this test, with no compiler link dependency on libm. Both encoders and real ordinary Tier 0 CALL references run
// against float/double bit fixtures under every MXCSR rounding/DAZ/FTZ combination. Old exception flags and control
// bits are checked separately from newly raised exceptions; ceil/floor/trunc may differ in optional new precision.
// Sqrt receives a bounded errno cell with adjacent sentinels. Signal-bounded child processes inspect saved fault
// MXCSR for unmasked precision/invalid, while nearbyint must return without newly raising precision. No source
// dispatch or optimizer acceptance is implied by exercising these raw ABI helpers.

#include <dlfcn.h>
#include <signal.h>
#include <ucontext.h>

#define BUSTER_CBUILTIN_MATH_TEST_HELPERS 14u
#define BUSTER_CBUILTIN_MATH_TEST_RECORDS 256u
#define BUSTER_CBUILTIN_MATH_TEST_PAGE 4096u
#define BUSTER_CBUILTIN_MATH_TEST_STATUS UINT32_C(0x003f)
#define BUSTER_CBUILTIN_MATH_TEST_PRECISION UINT32_C(0x0020)
#define BUSTER_CBUILTIN_MATH_TEST_MASKS UINT32_C(0x1f80)

typedef struct CBuiltinMathTestTally CBuiltinMathTestTally;
struct CBuiltinMathTestTally
{
    uint64_t checks;
    uint64_t failures;
};

typedef struct CBuiltinMathTestProgram CBuiltinMathTestProgram;
struct CBuiltinMathTestProgram
{
    uint8_t* mapping;
    uint8_t* entry;
    uint64_t mapping_bytes;
    uint32_t byte_count;
};

typedef struct CBuiltinMathTestNative CBuiltinMathTestNative;
struct CBuiltinMathTestNative
{
    CBuiltinMathTestProgram direct[2][BUSTER_CBUILTIN_MATH_TEST_HELPERS];
    CBuiltinMathTestProgram callers[2][BUSTER_CBUILTIN_MATH_TEST_HELPERS];
    void* library;
    void* oracles[BUSTER_CBUILTIN_MATH_TEST_HELPERS];
};

typedef struct CBuiltinMathTestErrno CBuiltinMathTestErrno;
struct CBuiltinMathTestErrno
{
    uint32_t before;
    int value;
    uint64_t after;
};

static const char* cbuiltin_math_test_names[BUSTER_CBUILTIN_MATH_TEST_HELPERS] =
{
    "sqrt",
    "sqrtf",
    "copysign",
    "copysignf",
    "ceil",
    "ceilf",
    "floor",
    "floorf",
    "trunc",
    "truncf",
    "nearbyint",
    "nearbyintf",
    "rint",
    "rintf",
};

static bool cbuiltin_math_test_expect(CBuiltinMathTestTally* tally, bool condition, const char* check, uint32_t item)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 24)
        {
            fprintf(stderr, "test-builtin-math-helpers: %s item %u\n", check, item);
        }
        tally->failures += 1;
    }
    return condition;
}

static uint16_t cbuiltin_math_test_control(void)
{
    uint16_t result;
    __asm__ volatile("fnstcw %0" : "=m"(result) : : "memory");
    return result;
}

static bool cbuiltin_math_test_map(CBuiltinMathTestTally* tally, const Tier0Code* helper, const Tier0Code* caller, bool scalar, CBuiltinMathTestProgram* program, uint32_t item)
{
    X86Instruction records[BUSTER_CBUILTIN_MATH_TEST_RECORDS];
    uint32_t offsets[BUSTER_CBUILTIN_MATH_TEST_RECORDS + 1];
    bool result = cbuiltin_math_test_expect(tally, helper->count <= BUSTER_CBUILTIN_MATH_TEST_RECORDS, "bounded helper records", item);
    const uint64_t helper_capacity = BUSTER_X86_ENCODER_CAPACITY(helper->count);
    const uint64_t caller_offset = (helper_capacity + 15) & ~UINT64_C(15);
    const uint64_t capacity = caller == NULL ? helper_capacity : caller_offset + BUSTER_X86_ENCODER_CAPACITY(caller->count);
    const uint64_t pages = (capacity + BUSTER_CBUILTIN_MATH_TEST_PAGE - 1) & ~(uint64_t)(BUSTER_CBUILTIN_MATH_TEST_PAGE - 1);
    if (result)
    {
        program->mapping_bytes = pages + BUSTER_CBUILTIN_MATH_TEST_PAGE;
        program->mapping = mmap(NULL, program->mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        result = cbuiltin_math_test_expect(tally, program->mapping != MAP_FAILED, "native mapping", item);
    }
    if (result)
    {
        memcpy(records, helper->records, helper->count * sizeof(*records));
        const BranchSizing sized = scalar ? size_branches_scalar(records, helper->count, helper->branches, helper->targets, helper->branch_count, offsets)
                                          : size_branches(records, helper->count, helper->branches, helper->targets, helper->branch_count, offsets);
        const int64_t encoded = sized.error != BRANCH_SIZING_OK ? -1 : scalar ? x86_encode_scalar(records, helper->count, program->mapping, helper_capacity, NULL, NULL)
                                                                          : x86_encode(records, helper->count, program->mapping, helper_capacity, NULL, NULL);
        result = cbuiltin_math_test_expect(tally, encoded > 0, "helper sizing and encoding", item);
        program->entry = program->mapping;
        program->byte_count = encoded > 0 ? (uint32_t)encoded : 0;
        if (result && caller != NULL)
        {
            uint32_t ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(1)] = { 0 };
            const int64_t encoded_caller = scalar ? x86_encode_scalar(caller->records, caller->count, program->mapping + caller_offset, capacity - caller_offset, NULL, ends)
                                                  : x86_encode(caller->records, caller->count, program->mapping + caller_offset, capacity - caller_offset, NULL, ends);
            result = cbuiltin_math_test_expect(tally, encoded_caller > 0 && caller->reference_count == 1 && caller->references[0].target == 1 && caller->references[0].kind == BUSTER_LINK_FIXUP_CALL &&
                caller->references[0].field == X86_FIXUP_DISPLACEMENT && caller->references[0].instruction < caller->count && caller->references[0].addend == 0, "ordinary helper CALL reference", item);
            if (result)
            {
                const LinkReference reference = caller->references[0];
                const uint32_t distance = x86_fixup_distance(caller->records[reference.instruction].form, reference.field);
                result = cbuiltin_math_test_expect(tally, distance == 4 && ends[0] >= distance && ends[0] <= (uint64_t)encoded_caller, "bounded helper CALL rel32 patch", item);
                if (result)
                {
                    const int32_t displacement = -(int32_t)(caller_offset + ends[0]);
                    memcpy(program->mapping + caller_offset + ends[0] - distance, &displacement, sizeof(displacement));
                    program->entry = program->mapping + caller_offset;
                    program->byte_count = (uint32_t)encoded_caller;
                }
            }
        }
        result = cbuiltin_math_test_expect(tally, result && mprotect(program->mapping, pages, PROT_READ | PROT_EXEC) == 0 &&
            mprotect(program->mapping + pages, BUSTER_CBUILTIN_MATH_TEST_PAGE, PROT_NONE) == 0, "native code permissions", item);
    }
    return result;
}

static bool cbuiltin_math_test_caller(CBuiltinMathTestTally* tally, Arena* arena, const CBuiltinMathHelper* helper, CBuiltinMathTestProgram programs[2], uint32_t item)
{
    const uint64_t mark = arena->length;
    const uint32_t parameters = helper->parameter_count;
    uint8_t kinds[4] = { BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOCAL };
    uint8_t classes[4] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
    uint64_t values[4] = { 0, 1, 0, 0 };
    kinds[parameters] = BUSTER_TYPED_CALL;
    kinds[parameters + 1] = BUSTER_TYPED_RETURN;
    values[parameters] = 0;
    values[parameters + 1] = 0;
    const uint8_t local_classes[2] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
    const uint8_t local_flags[2] = { 0 };
    const AbiLocation locations[2] =
    {
        { .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        { .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
    };
    const AbiReturn returned =
    {
        .kind = BUSTER_ABI_RESULT_REGISTERS,
        .part_bytes = 8,
        .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
    };
    const AbiCall call =
    {
        .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2,
        .integer_registers = (uint8_t)parameters,
        .result = returned,
    };
    const SymbolId target = 1;
    const uint32_t first = 0;
    const uint8_t variadic = 0;
    const Tier0Function function =
    {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = parameters + 2,
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = parameters,
        .parameter_count = parameters,
        .parameters = locations,
        .result = returned,
        .call_targets = &target,
        .call_first = &first,
        .call_argument_counts = &parameters,
        .call_variadic = &variadic,
        .calls = &call,
        .argument_locations = locations,
        .argument_classes = local_classes,
        .call_count = 1,
        .argument_count = parameters,
    };
    const Tier0Check checked = tier0_validate(&function);
    bool result = cbuiltin_math_test_expect(tally, checked.error == TIER0_OK, checked.rule != NULL ? checked.rule : "Tier 0 caller validation", item);
    for (uint32_t path = 0; result && path < 2; path += 1)
    {
        const Tier0Code code = path == 0 ? tier0_compile_scalar(&function, arena) : tier0_compile(&function, arena);
        result = cbuiltin_math_test_expect(tally, code.error == TIER0_OK && code.branch_count == 0 && code.reference_count == 1, "ordinary Tier 0 caller compiled", item);
        if (result)
        {
            result = cbuiltin_math_test_map(tally, &helper->code, &code, path == 0, programs + path, item);
        }
    }
    arena_pop_to(arena, mark);
    return result;
}

static uint64_t cbuiltin_math_test_oracle(const CBuiltinMathTestNative* native, uint32_t helper, uint64_t bits, uint64_t sign, int* error)
{
    const int saved_errno = errno;
    errno = *error;
    uint64_t result = 0;
    if ((helper & 1) != 0)
    {
        const uint32_t input_bits = (uint32_t)bits;
        const uint32_t sign_bits = (uint32_t)sign;
        float input;
        float sign_input;
        memcpy(&input, &input_bits, sizeof(input));
        memcpy(&sign_input, &sign_bits, sizeof(sign_input));
        float output = helper == 3 ? ((float (*)(float, float))native->oracles[helper])(input, sign_input) : ((float (*)(float))native->oracles[helper])(input);
        uint32_t output_bits;
        memcpy(&output_bits, &output, sizeof(output_bits));
        result = output_bits;
    }
    else
    {
        double input;
        double sign_input;
        memcpy(&input, &bits, sizeof(input));
        memcpy(&sign_input, &sign, sizeof(sign_input));
        double output = helper == 2 ? ((double (*)(double, double))native->oracles[helper])(input, sign_input) : ((double (*)(double))native->oracles[helper])(input);
        memcpy(&result, &output, sizeof(result));
    }
    *error = errno;
    errno = saved_errno;
    return result;
}

// Generated entry points have no Clang function-type prefix.
static __attribute__((no_sanitize("function"))) void cbuiltin_math_test_values(CBuiltinMathTestTally* tally, const CBuiltinMathTestNative* native)
{
    static const uint32_t singles[] =
    {
        0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007fffff, 0x807fffff,
        0x00800000, 0x80800000, 0x3e800000, 0xbe800000, 0x3f000000, 0xbf000000,
        0x3f400000, 0xbf400000, 0x3f800000, 0xbf800000, 0x3fc00000, 0xbfc00000,
        0x40200000, 0xc0200000, 0x4affffff, 0xcaffffff, 0x4b000000, 0xcb000000,
        0x4b000001, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000,
        0x7fc12345, 0xffc12345, 0x7f812345, 0xff812345,
    };
    static const uint64_t doubles[] =
    {
        UINT64_C(0x0000000000000000), UINT64_C(0x8000000000000000), UINT64_C(0x0000000000000001), UINT64_C(0x8000000000000001),
        UINT64_C(0x000fffffffffffff), UINT64_C(0x800fffffffffffff), UINT64_C(0x0010000000000000), UINT64_C(0x8010000000000000),
        UINT64_C(0x3fd0000000000000), UINT64_C(0xbfd0000000000000), UINT64_C(0x3fe0000000000000), UINT64_C(0xbfe0000000000000),
        UINT64_C(0x3fe8000000000000), UINT64_C(0xbfe8000000000000), UINT64_C(0x3ff0000000000000), UINT64_C(0xbff0000000000000),
        UINT64_C(0x3ff8000000000000), UINT64_C(0xbff8000000000000), UINT64_C(0x4004000000000000), UINT64_C(0xc004000000000000),
        UINT64_C(0x432fffffffffffff), UINT64_C(0xc32fffffffffffff), UINT64_C(0x4330000000000000), UINT64_C(0xc330000000000000),
        UINT64_C(0x4330000000000001), UINT64_C(0x7fefffffffffffff), UINT64_C(0xffefffffffffffff), UINT64_C(0x7ff0000000000000),
        UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8123456789abc), UINT64_C(0xfff8123456789abc), UINT64_C(0x7ff0123456789abc), UINT64_C(0xfff0123456789abc),
    };
    const uint32_t old_flags[] = { 0, BUSTER_CBUILTIN_MATH_TEST_PRECISION, UINT32_C(0x15), BUSTER_CBUILTIN_MATH_TEST_STATUS };
    const uint32_t original = _mm_getcsr();
    const uint16_t original_control = cbuiltin_math_test_control();
    for (uint32_t rc = 0; rc < 4; rc += 1)
    {
        const uint16_t control = (uint16_t)((original_control & ~UINT16_C(0x0c00)) | (rc << 10) | UINT16_C(0x003f));
        __asm__ volatile("fldcw %0" : : "m"(control) : "memory");
        for (uint32_t mode = 0; mode < 4; mode += 1)
        {
            for (uint32_t flags = 0; flags < BUSTER_ARRAY_LENGTH(old_flags); flags += 1)
            {
                const uint32_t environment = (original & ~(BUSTER_CBUILTIN_MATH_TEST_STATUS | UINT32_C(0x6000) | UINT32_C(0x8040))) | BUSTER_CBUILTIN_MATH_TEST_MASKS |
                    (rc << 13) | ((mode & 1) != 0 ? UINT32_C(0x40) : 0) | ((mode & 2) != 0 ? UINT32_C(0x8000) : 0) | old_flags[flags];
                for (uint32_t h = 0; h < BUSTER_CBUILTIN_MATH_TEST_HELPERS; h += 1)
                {
                    const bool single = (h & 1) != 0;
                    const uint32_t count = single ? BUSTER_ARRAY_LENGTH(singles) : BUSTER_ARRAY_LENGTH(doubles);
                    for (uint32_t p = 0; p < count; p += 1)
                    {
                        const uint64_t bits = single ? UINT64_C(0x5a5aa5a500000000) | singles[p] : doubles[p];
                        const uint64_t sign = single ? UINT64_C(0xa5a55a5a00000000) | singles[(p + 7) % count] : doubles[(p + 7) % count];
                        int expected_errno = 0x1357;
                        _mm_setcsr(environment);
                        const uint64_t expected = cbuiltin_math_test_oracle(native, h, bits, sign, &expected_errno);
                        const uint32_t oracle_state = _mm_getcsr();
                        for (uint32_t path = 0; path < 2; path += 1)
                        {
                            for (uint32_t caller = 0; caller < 2; caller += 1)
                            {
                                CBuiltinMathTestErrno actual_error;
                                CBuiltinMathTestErrno expected_error;
                                memset(&actual_error, 0xa5, sizeof(actual_error));
                                actual_error.value = 0x1357;
                                expected_error = actual_error;
                                expected_error.value = h < 2 ? expected_errno : 0x1357;
                                const uint64_t second = h < 2 ? (uint64_t)(uintptr_t)&actual_error.value : sign;
                                const uint8_t* entry = caller != 0 ? native->callers[path][h].entry : native->direct[path][h].entry;
                                uint64_t (*function)(uint64_t, uint64_t) = (uint64_t (*)(uint64_t, uint64_t))(void*)entry;
                                _mm_setcsr(environment);
                                const uint64_t actual = function(bits, second);
                                const uint32_t state = _mm_getcsr();
                                // Denormal operand is an architecture-specific sixth flag, unlike C's five exceptions.
                                // Integer rounders deliberately avoid the extra DE of an older libm add/sub recipe.
                                const uint32_t status_mask = (BUSTER_CBUILTIN_MATH_TEST_STATUS & ~UINT32_C(0x02)) &
                                    (h >= 4 && h <= 9 ? ~BUSTER_CBUILTIN_MATH_TEST_PRECISION : UINT32_MAX);
                                const bool values = actual == expected && memcmp(&actual_error, &expected_error, sizeof(actual_error)) == 0;
                                const bool controls = (state & ~BUSTER_CBUILTIN_MATH_TEST_STATUS) == (environment & ~BUSTER_CBUILTIN_MATH_TEST_STATUS) && cbuiltin_math_test_control() == control;
                                const bool exceptions = (state & status_mask) == (oracle_state & status_mask) && (state & old_flags[flags]) == old_flags[flags];
                                if ((!values || !controls || !exceptions) && tally->failures < 24)
                                {
                                    fprintf(stderr, "test-builtin-math-helpers: %s path%u caller%u bits=%016" PRIx64 " rc%u daz%u ftz%u old=%02x result=%016" PRIx64 "/%016" PRIx64 " mxcsr=%04x/%04x errno=%d/%d\n",
                                        cbuiltin_math_test_names[h], path, caller, bits, rc, mode & 1, mode >> 1, old_flags[flags], actual, expected, state, oracle_state, actual_error.value, expected_error.value);
                                }
                                cbuiltin_math_test_expect(tally, values, "libm raw result, signed zero/NaN payload and bounded errno cell", h);
                                cbuiltin_math_test_expect(tally, controls && exceptions, "MXCSR controls, old flags and operation exceptions", h);
                                if (h == 10 || h == 11)
                                {
                                    cbuiltin_math_test_expect(tally, (state & BUSTER_CBUILTIN_MATH_TEST_PRECISION) == (environment & BUSTER_CBUILTIN_MATH_TEST_PRECISION), "nearbyint never adds or clears precision", h);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // The raw-bit ABI is governed by MXCSR even when x87 independently requests the opposite direction.
    const uint16_t opposite = (uint16_t)((original_control & ~UINT16_C(0x0c00)) | UINT16_C(0x0400) | UINT16_C(0x003f));
    const uint32_t upward = (original & ~(BUSTER_CBUILTIN_MATH_TEST_STATUS | UINT32_C(0x6000) | UINT32_C(0x8040))) | BUSTER_CBUILTIN_MATH_TEST_MASKS | UINT32_C(0x4000);
    __asm__ volatile("fldcw %0" : : "m"(opposite) : "memory");
    for (uint32_t h = 10; h < 14; h += 1)
    {
        const bool single = (h & 1) != 0;
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t caller = 0; caller < 2; caller += 1)
            {
                const uint8_t* entry = caller != 0 ? native->callers[path][h].entry : native->direct[path][h].entry;
                uint64_t (*function)(uint64_t) = (uint64_t (*)(uint64_t))(void*)entry;
                _mm_setcsr(upward);
                const uint64_t result = function(single ? UINT64_C(0x3fa00000) : UINT64_C(0x3ff4000000000000));
                const uint32_t state = _mm_getcsr();
                cbuiltin_math_test_expect(tally, result == (single ? UINT64_C(0x40000000) : UINT64_C(0x4000000000000000)) && cbuiltin_math_test_control() == opposite &&
                    (state & ~BUSTER_CBUILTIN_MATH_TEST_STATUS) == (upward & ~BUSTER_CBUILTIN_MATH_TEST_STATUS) &&
                    (state & BUSTER_CBUILTIN_MATH_TEST_STATUS) == (h >= 12 ? BUSTER_CBUILTIN_MATH_TEST_PRECISION : 0), "MXCSR up overrides preserved x87 down", h);
            }
        }
    }
    __asm__ volatile("fldcw %0" : : "m"(original_control) : "memory");
    _mm_setcsr(original);
}

typedef struct CBuiltinMathTestTrap CBuiltinMathTestTrap;
struct CBuiltinMathTestTrap
{
    volatile sig_atomic_t signal;
    volatile sig_atomic_t mxcsr;
    volatile sig_atomic_t completed;
    uint64_t result;
};

static CBuiltinMathTestTrap* cbuiltin_math_test_trap_output;

static void cbuiltin_math_test_signal(int signal, siginfo_t* info, void* context)
{
    (void)info;
    const ucontext_t* saved = context;
    cbuiltin_math_test_trap_output->signal = signal;
    if (saved->uc_mcontext.fpregs != NULL)
    {
        cbuiltin_math_test_trap_output->mxcsr = (sig_atomic_t)saved->uc_mcontext.fpregs->mxcsr;
    }
    _exit(0);
}

// Generated entry points have no Clang function-type prefix.
static __attribute__((no_sanitize("function"))) void cbuiltin_math_test_traps(CBuiltinMathTestTally* tally, const CBuiltinMathTestNative* native)
{
    CBuiltinMathTestTrap* shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (cbuiltin_math_test_expect(tally, shared != MAP_FAILED, "trap result mapping", 0))
    {
        const uint32_t original = _mm_getcsr();
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t single = 0; single < 2; single += 1)
            {
                for (uint32_t kind = 0; kind < 3; kind += 1)
                {
                    for (uint32_t old_precision = 0; old_precision < 2; old_precision += 1)
                    {
                        memset(shared, 0, sizeof(*shared));
                        const uint32_t environment = ((original & ~(BUSTER_CBUILTIN_MATH_TEST_STATUS | UINT32_C(0x6000) | UINT32_C(0x8040))) | BUSTER_CBUILTIN_MATH_TEST_MASKS |
                            (2u << 13) | UINT32_C(0x05) | (old_precision != 0 ? BUSTER_CBUILTIN_MATH_TEST_PRECISION : 0)) & ~(kind == 2 ? UINT32_C(0x80) : UINT32_C(0x1000));
                        const uint32_t helper = (kind == 1 ? 12 : 10) + single;
                        const uint64_t bits = kind == 2 ? (single ? UINT64_C(0x7f812345) : UINT64_C(0x7ff0123456789abc)) :
                            single ? UINT64_C(0x3fa00000) : UINT64_C(0x3ff4000000000000);
                        const pid_t child = fork();
                        if (child == 0)
                        {
                            alarm(3);
                            cbuiltin_math_test_trap_output = shared;
                            struct sigaction action;
                            memset(&action, 0, sizeof(action));
                            sigemptyset(&action.sa_mask);
                            action.sa_sigaction = cbuiltin_math_test_signal;
                            action.sa_flags = SA_SIGINFO;
                            if (sigaction(SIGFPE, &action, NULL) != 0)
                            {
                                _exit(2);
                            }
                            uint64_t (*function)(uint64_t) = (uint64_t (*)(uint64_t))(void*)native->callers[path][helper].entry;
                            _mm_setcsr(environment);
                            shared->result = function(bits);
                            shared->mxcsr = (sig_atomic_t)_mm_getcsr();
                            shared->completed = 1;
                            _exit(0);
                        }
                        int status = 0;
                        const bool waited = child > 0 && waitpid(child, &status, 0) == child;
                        const bool controls = ((uint32_t)shared->mxcsr & ~BUSTER_CBUILTIN_MATH_TEST_STATUS) == (environment & ~BUSTER_CBUILTIN_MATH_TEST_STATUS);
                        const uint64_t rounded = single ? UINT64_C(0x40000000) : UINT64_C(0x4000000000000000);
                        const bool expected = kind == 0 ? shared->signal == 0 && shared->completed != 0 && shared->result == rounded &&
                            ((uint32_t)shared->mxcsr & BUSTER_CBUILTIN_MATH_TEST_PRECISION) == (environment & BUSTER_CBUILTIN_MATH_TEST_PRECISION) :
                            shared->signal == SIGFPE && shared->completed == 0 && ((uint32_t)shared->mxcsr & (kind == 1 ? BUSTER_CBUILTIN_MATH_TEST_PRECISION : 1)) != 0;
                        cbuiltin_math_test_expect(tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0 && expected && controls, "unmasked precision/invalid and original trap controls", helper);
                    }
                }
            }
        }
        _mm_setcsr(original);
        munmap(shared, sizeof(*shared));
    }
}

static bool cbuiltin_math_test_empty(const CBuiltinMathHelper* helper, Tier0Error error)
{
    return helper->code.error == error && helper->code.records == NULL && helper->code.count == 0 && helper->code.branches == NULL && helper->code.targets == NULL &&
        helper->code.branch_count == 0 && helper->code.references == NULL && helper->code.reference_count == 0 && helper->code.deferred == 0 && helper->code.node == 0 &&
        helper->parameter_count == 0 && helper->result_class == 0 && helper->parameter_classes[0] == 0 && helper->parameter_classes[1] == 0 && !helper->compiler_barrier &&
        !helper->reads_fp_environment && !helper->may_raise_fp_exceptions && !helper->may_raise_inexact && !helper->requires_errno_address;
}

static void cbuiltin_math_test_refusals(CBuiltinMathTestTally* tally)
{
    uint8_t storage[256] __attribute__((aligned(64)));
    uint8_t snapshot[sizeof(storage)];
    const CBuiltinId unsupported[] = { CBUILTIN_NONE, CBUILTIN_ATOMIC_LOAD_N, (CBuiltinId)((uint32_t)CBUILTIN_SQRT - 1), (CBuiltinId)((uint32_t)CBUILTIN_RINTF + 1), (CBuiltinId)UINT32_MAX };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(unsupported); i += 1)
    {
        memset(storage, 0xa5, sizeof(storage));
        memcpy(snapshot, storage, sizeof(storage));
        Arena arena = { .base = storage, .length = 16, .capacity = sizeof(storage) };
        const CBuiltinMathHelper helper = cbuiltin_math_helper(unsupported[i], &arena);
        cbuiltin_math_test_expect(tally, cbuiltin_math_test_empty(&helper, TIER0_ERROR_UNSUPPORTED) && arena.length == 16 && memcmp(storage, snapshot, sizeof(storage)) == 0,
            "unsupported ID has no output or success metadata", i);
    }
    const CBuiltinMathHelper missing = cbuiltin_math_helper(CBUILTIN_SQRT, NULL);
    cbuiltin_math_test_expect(tally, cbuiltin_math_test_empty(&missing, TIER0_ERROR_INVALID), "missing arena is invalid", 0);
    for (uint32_t i = 0; i < 3; i += 1)
    {
        memset(storage, 0xa5, sizeof(storage));
        memcpy(snapshot, storage, sizeof(storage));
        Arena arena = { .base = i == 0 ? NULL : storage, .length = i == 1 ? sizeof(storage) + 1 : 16, .capacity = sizeof(storage) };
        if (i == 2)
        {
            arena.capacity = 16 + sizeof(X86Instruction) - 1;
        }
        const uint64_t mark = arena.length;
        const CBuiltinMathHelper helper = cbuiltin_math_helper(CBUILTIN_SQRT, &arena);
        cbuiltin_math_test_expect(tally, cbuiltin_math_test_empty(&helper, i == 2 ? TIER0_ERROR_MEMORY : TIER0_ERROR_INVALID) && arena.length == mark &&
            memcmp(storage, snapshot, sizeof(storage)) == 0, "malformed or exhausted arena leaves bytes and length unchanged", i);
    }
}

// Generated entry points have no Clang function-type prefix.
static __attribute__((no_sanitize("function"))) void cbuiltin_math_test_errno_tail(CBuiltinMathTestTally* tally, const CBuiltinMathTestNative* native)
{
    uint8_t* storage = mmap(NULL, 2 * BUSTER_CBUILTIN_MATH_TEST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (cbuiltin_math_test_expect(tally, storage != MAP_FAILED, "errno tail mapping", 0))
    {
        const bool guarded = mprotect(storage + BUSTER_CBUILTIN_MATH_TEST_PAGE, BUSTER_CBUILTIN_MATH_TEST_PAGE, PROT_NONE) == 0;
        if (cbuiltin_math_test_expect(tally, guarded, "errno tail guard", 0))
        {
            const uint32_t original = _mm_getcsr();
            const uint32_t environment = (original & ~(BUSTER_CBUILTIN_MATH_TEST_STATUS | UINT32_C(0x8040))) | BUSTER_CBUILTIN_MATH_TEST_MASKS;
            int* cell = (int*)(storage + BUSTER_CBUILTIN_MATH_TEST_PAGE - sizeof(int));
            for (uint32_t path = 0; path < 2; path += 1)
            {
                for (uint32_t single = 0; single < 2; single += 1)
                {
                    for (uint32_t caller = 0; caller < 2; caller += 1)
                    {
                        const uint8_t* entry = caller != 0 ? native->callers[path][single].entry : native->direct[path][single].entry;
                        uint64_t (*function)(uint64_t, uint64_t) = (uint64_t (*)(uint64_t, uint64_t))(void*)entry;
                        for (uint32_t variant = 0; variant < 3; variant += 1)
                        {
                            memset(storage, 0xa5, BUSTER_CBUILTIN_MATH_TEST_PAGE);
                            *cell = 0x1357;
                            const uint64_t bits = variant == 0 ? (single ? UINT64_C(0xbf800000) : UINT64_C(0xbff0000000000000)) :
                                variant == 1 ? (single ? UINT64_C(0x40800000) : UINT64_C(0x4010000000000000)) :
                                single ? UINT64_C(0x80000001) : UINT64_C(0x8000000000000001);
                            _mm_setcsr(environment | (variant == 2 ? UINT32_C(0x40) : 0));
                            const uint64_t result = function(bits, (uint64_t)(uintptr_t)cell);
                            bool prefix = true;
                            for (uint32_t b = 0; b < BUSTER_CBUILTIN_MATH_TEST_PAGE - sizeof(int); b += 1)
                            {
                                prefix &= storage[b] == 0xa5;
                            }
                            const uint64_t magnitude = result & (single ? UINT64_C(0x7fffffff) : UINT64_C(0x7fffffffffffffff));
                            const bool value = variant == 0 ? magnitude > (single ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000)) :
                                result == (variant == 1 ? (single ? UINT64_C(0x40000000) : UINT64_C(0x4000000000000000)) :
                                    single ? UINT64_C(0x80000000) : UINT64_C(0x8000000000000000));
                            cbuiltin_math_test_expect(tally, *cell == (variant == 0 ? EDOM : 0x1357) && prefix && value,
                                "sqrt page-tail errno: negative, positive and DAZ negative subnormal", single);
                        }
                    }
                }
            }
            _mm_setcsr(original);
        }
        munmap(storage, 2 * BUSTER_CBUILTIN_MATH_TEST_PAGE);
    }
}

static bool cbuiltin_math_test_tail(CBuiltinMathTestTally* tally, CBuiltinId id, const CBuiltinMathHelper* original, uint32_t item)
{
    const uint64_t bytes = (uint64_t)original->code.count * sizeof(X86Instruction) + (uint64_t)original->code.branch_count * 2 * sizeof(uint32_t);
    uint8_t* storage = mmap(NULL, 3 * BUSTER_CBUILTIN_MATH_TEST_PAGE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool result = cbuiltin_math_test_expect(tally, storage != MAP_FAILED && bytes <= BUSTER_CBUILTIN_MATH_TEST_PAGE, "exact helper output mapping", item);
    if (result)
    {
        uint8_t* middle = storage + BUSTER_CBUILTIN_MATH_TEST_PAGE;
        result = cbuiltin_math_test_expect(tally, mprotect(middle, BUSTER_CBUILTIN_MATH_TEST_PAGE, PROT_READ | PROT_WRITE) == 0, "exact helper output guard", item);
        if (result)
        {
            memset(middle, 0xa5, BUSTER_CBUILTIN_MATH_TEST_PAGE);
            Arena arena = { .base = middle, .length = BUSTER_CBUILTIN_MATH_TEST_PAGE - bytes, .capacity = BUSTER_CBUILTIN_MATH_TEST_PAGE };
            const uint64_t mark = arena.length;
            const CBuiltinMathHelper helper = cbuiltin_math_helper(id, &arena);
            result = cbuiltin_math_test_expect(tally, helper.code.error == TIER0_OK && arena.length == arena.capacity && helper.code.count == original->code.count &&
                helper.code.branch_count == original->code.branch_count && memcmp(helper.code.records, original->code.records, (size_t)bytes) == 0,
                "exact record and branch-column capacity succeeds", item);
            bool prefix = true;
            for (uint32_t b = 0; b < mark; b += 1)
            {
                prefix &= middle[b] == 0xa5;
            }
            result = cbuiltin_math_test_expect(tally, prefix, "exact-capacity prefix unchanged", item) && result;
            if (result)
            {
                CBuiltinMathTestProgram programs[2] = { 0 };
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    result = cbuiltin_math_test_map(tally, &helper.code, NULL, path == 0, programs + path, item) && result;
                    if (programs[path].mapping != NULL && programs[path].mapping != MAP_FAILED)
                    {
                        munmap(programs[path].mapping, programs[path].mapping_bytes);
                    }
                }
            }
            memset(middle, 0xa5, BUSTER_CBUILTIN_MATH_TEST_PAGE);
            arena.length = mark;
            arena.capacity -= 1;
            const CBuiltinMathHelper rejected = cbuiltin_math_helper(id, &arena);
            bool untouched = true;
            for (uint32_t b = 0; b < BUSTER_CBUILTIN_MATH_TEST_PAGE; b += 1)
            {
                untouched &= middle[b] == 0xa5;
            }
            result = cbuiltin_math_test_expect(tally, cbuiltin_math_test_empty(&rejected, TIER0_ERROR_MEMORY) && arena.length == mark && untouched,
                "one byte short is transactional", item) && result;
        }
    }
    if (storage != MAP_FAILED)
    {
        munmap(storage, 3 * BUSTER_CBUILTIN_MATH_TEST_PAGE);
    }
    return result;
}

static int run_builtin_math_helper_tests(int argc, char** argv)
{
    (void)argv;
    CBuiltinMathTestTally tally = { 0 };
    CBuiltinMathTestNative native = { 0 };
    bool valid = cbuiltin_math_test_expect(&tally, argc == 0, "usage: test-builtin-math-helpers", 0);
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    if (valid)
    {
        valid = cbuiltin_math_test_expect(&tally, arena.base != NULL && x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize(), "memory and tables", 0);
    }
    X86Target target;
    uint8_t allowed[X86_FORM_COUNT];
    if (valid)
    {
        valid = cbuiltin_math_test_expect(&tally, x86_target_parse("x86-64", 6, &target), "baseline target", 0);
        if (valid)
        {
            x86_target_forms(&target, allowed);
            cbuiltin_math_test_refusals(&tally);
            native.library = dlopen("libm.so.6", RTLD_NOW | RTLD_LOCAL);
            valid = cbuiltin_math_test_expect(&tally, native.library != NULL, "independent host libm oracle", 0);
        }
    }
    for (uint32_t h = 0; valid && h < BUSTER_CBUILTIN_MATH_TEST_HELPERS; h += 1)
    {
        native.oracles[h] = dlsym(native.library, cbuiltin_math_test_names[h]);
        valid = cbuiltin_math_test_expect(&tally, native.oracles[h] != NULL, "typed libm symbol", h);
        const CBuiltinId id = (CBuiltinId)((uint32_t)CBUILTIN_SQRT + h);
        const CBuiltinMathHelper helper = cbuiltin_math_helper(id, &arena);
        const uint32_t parameters = h < 4 ? 2 : 1;
        valid = cbuiltin_math_test_expect(&tally, helper.code.error == TIER0_OK && helper.code.count != 0 && helper.code.count <= BUSTER_CBUILTIN_MATH_TEST_RECORDS &&
            helper.code.reference_count == 0 && helper.code.deferred == 0 && helper.parameter_count == parameters && helper.result_class == BUSTER_TYPED_CLASS_U64 &&
            helper.parameter_classes[0] == BUSTER_TYPED_CLASS_U64 && helper.parameter_classes[1] == (parameters == 2 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VOID) &&
            helper.compiler_barrier && helper.reads_fp_environment == (h != 2 && h != 3) && helper.may_raise_fp_exceptions == (h != 2 && h != 3) &&
            helper.may_raise_inexact == (h < 2 || h >= 12) && helper.requires_errno_address == (h < 2), "raw ABI and effect metadata", h) && valid;
        if (valid)
        {
            const BranchSizingCheck check = branch_sizing_validate(helper.code.records, helper.code.count, helper.code.branches, helper.code.targets, helper.code.branch_count);
            valid = cbuiltin_math_test_expect(&tally, check.error == BRANCH_SIZING_OK && x86_validate(helper.code.records, helper.code.count) == helper.code.count &&
                x86_target_validate(allowed, helper.code.records, helper.code.count) == helper.code.count, "branch, record and portable x86-64 target contracts", h);
        }
        for (uint32_t path = 0; valid && path < 2; path += 1)
        {
            valid = cbuiltin_math_test_map(&tally, &helper.code, NULL, path == 0, &native.direct[path][h], h);
        }
        if (valid)
        {
            valid = cbuiltin_math_test_expect(&tally, native.direct[0][h].byte_count == native.direct[1][h].byte_count &&
                memcmp(native.direct[0][h].entry, native.direct[1][h].entry, native.direct[0][h].byte_count) == 0, "both encoders produce identical helper bytes", h);
        }
        if (valid)
        {
            CBuiltinMathTestProgram programs[2] = { 0 };
            valid = cbuiltin_math_test_caller(&tally, &arena, &helper, programs, h);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                native.callers[path][h] = programs[path];
            }
            if (valid)
            {
                valid = cbuiltin_math_test_expect(&tally, programs[0].byte_count == programs[1].byte_count && memcmp(programs[0].entry, programs[1].entry, programs[0].byte_count) == 0,
                    "both ordinary Tier 0 callers and CALL fixups agree", h);
            }
        }
        if (valid)
        {
            valid = cbuiltin_math_test_tail(&tally, id, &helper, h);
        }
    }
    CBuiltinMathTestTally* shared = MAP_FAILED;
    if (valid)
    {
        shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        valid = cbuiltin_math_test_expect(&tally, shared != MAP_FAILED, "native result mapping", 0);
    }
    if (valid)
    {
        const pid_t child = fork();
        if (child == 0)
        {
            alarm(30);
            cbuiltin_math_test_values(shared, &native);
            cbuiltin_math_test_errno_tail(shared, &native);
            cbuiltin_math_test_traps(shared, &native);
            _exit(shared->failures == 0 ? 0 : 1);
        }
        int status = 0;
        const bool waited = child > 0 && waitpid(child, &status, 0) == child;
        cbuiltin_math_test_expect(&tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0, "native child completed", (uint32_t)status);
    }
    if (shared != MAP_FAILED)
    {
        tally.checks += shared->checks;
        tally.failures += shared->failures;
        munmap(shared, sizeof(*shared));
    }
    for (uint32_t path = 0; path < 2; path += 1)
    {
        for (uint32_t h = 0; h < BUSTER_CBUILTIN_MATH_TEST_HELPERS; h += 1)
        {
            if (native.direct[path][h].mapping != NULL && native.direct[path][h].mapping != MAP_FAILED)
            {
                munmap(native.direct[path][h].mapping, native.direct[path][h].mapping_bytes);
            }
            if (native.callers[path][h].mapping != NULL && native.callers[path][h].mapping != MAP_FAILED)
            {
                munmap(native.callers[path][h].mapping, native.callers[path][h].mapping_bytes);
            }
        }
    }
    if (native.library != NULL)
    {
        dlclose(native.library);
    }
    arena_release(&arena);
    printf("test-builtin-math-helpers: %" PRIu64 " checks, %" PRIu64 " failures\n", tally.checks, tally.failures);
    return tally.failures != 0;
}
