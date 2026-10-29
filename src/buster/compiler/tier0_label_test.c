// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier0's labels check (#133's labels as values): JUMP_INDIRECT and Tier0Function.label_entries. First the stream's
// rules, each break refused at its entry; then a function that dispatches on its argument's base-4 digits through a table
// of its labels' addresses, as an interpreter's loop does (GNU C's goto *table[op]), compiled by both paths, which must
// give the same records and labels' records, encoded by both encoders, and run natively in a child process on inputs
// whose results this file computes. The test fills the table from the labels' records (Tier0Code.label_records) and the
// records' offsets after branch sizing, as a front end defines each label's symbol there. tier0_test.c includes this file
// before its command, whose tally and helpers it uses.

#define BUSTER_TIER0_TEST_LABEL_INPUTS 12
#define BUSTER_TIER0_TEST_LABEL_COUNT 5

// The function: f(n, table) with n (local 0), the table (local 1), the result (local 2) and the next target (local 3).
// While n isn't 0, it takes its low digit d, shifts it out and goes to table[d]: 0 makes the result r * 5 + 1, 1 makes it
// r * 7 + 2, 2 makes it r ^ 0x55 and goes on to table[4], which makes it r * 3, and 3 adds n (shifted).
#define TIER0_TEST_LABEL_LOOP 2
static const Tier0TestEntry tier0_test_label_entries[] = {
    // 0: r = 0.
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 2 },
    // 2: if (n == 0) return r.
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0 },
    { BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 8 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2 }, { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0 },
    // 8: target = table[n & 3]; n >>= 2; goto *target.
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 3 },
    { BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 8 }, { BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 3 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 2 },
    { BUSTER_TYPED_SHR, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 3 },
    { BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, 0 },
    // 21, label 0: r = r * 5 + 1.
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 5 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 2 },
    { BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, TIER0_TEST_LABEL_LOOP },
    // 28, label 1: r = r * 7 + 2.
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 7 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 2 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 2 },
    { BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, TIER0_TEST_LABEL_LOOP },
    // 35, label 2: r ^= 0x55; goto *table[4].
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0x55 }, { BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 2 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 4 },
    { BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 8 }, { BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, 0 },
    // 44, label 3: r += n.
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2 }, { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 2 }, { BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, TIER0_TEST_LABEL_LOOP },
    // 49, label 4: r *= 3.
    { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 2 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 3 }, { BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0 },
    { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, 2 }, { BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, TIER0_TEST_LABEL_LOOP },
};
static const uint32_t tier0_test_label_targets[BUSTER_TIER0_TEST_LABEL_COUNT] = { 21, 28, 35, 44, 49 };

// What f(n) returns, computed here.
static uint64_t tier0_test_label_expected(uint64_t n)
{
    uint64_t result = 0;
    while (n != 0)
    {
        const uint64_t digit = n & 3;
        n >>= 2;
        if (digit == 0)
        {
            result = result * 5 + 1;
        }
        else if (digit == 1)
        {
            result = result * 7 + 2;
        }
        else if (digit == 2)
        {
            result = (result ^ 0x55) * 3;
        }
        else
        {
            result += n;
        }
    }
    return result;
}

// What the native run shares with the test: each input's result.
typedef struct Tier0TestLabelShared Tier0TestLabelShared;
struct Tier0TestLabelShared
{
    uint64_t results[BUSTER_TIER0_TEST_LABEL_INPUTS];
};

// Runs the function's code (`length` bytes) in a child, which must exit with status 0: the code mapped executable at a
// page of its own, the table its labels' addresses there (`offsets` of their records), and f called from C on each input.
// Generated entry points have no Clang function-type prefix.
static __attribute__((no_sanitize("function"))) bool tier0_test_label_native(const uint8_t* code, uint32_t length, const uint32_t* offsets, const uint32_t* records, const uint64_t* inputs,
                                    Tier0TestLabelShared* shared)
{
    const pid_t child = fork();
    if (child == 0)
    {
        alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
        uint8_t* page = mmap(0, 4 * BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED)
        {
            _exit(3);
        }
        memcpy(page, code, length);
        if (mprotect(page, 4 * BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_EXEC) != 0)
        {
            _exit(4);
        }
        const void* table[BUSTER_TIER0_TEST_LABEL_COUNT];
        for (uint32_t k = 0; k < BUSTER_TIER0_TEST_LABEL_COUNT; k += 1)
        {
            table[k] = page + offsets[records[k]];
        }
        typedef uint64_t (*Body)(uint64_t, const void* const*);
        Body body = 0;
        const void* entry = page;
        memcpy(&body, &entry, sizeof(body));
        for (uint32_t r = 0; r < BUSTER_TIER0_TEST_LABEL_INPUTS; r += 1)
        {
            shared->results[r] = body(inputs[r], table);
        }
        _exit(0);
    }
    int status = 0;
    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
    return waited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// The rules, then the function: compiled, encoded and run.
static void tier0_test_labels(Tier0TestTally* tally)
{
    static const uint8_t local_classes[4] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
    static const uint8_t local_flags[4];
    uint8_t kinds[BUSTER_ARRAY_LENGTH(tier0_test_label_entries)];
    uint8_t classes[BUSTER_ARRAY_LENGTH(tier0_test_label_entries)];
    uint64_t values[BUSTER_ARRAY_LENGTH(tier0_test_label_entries)];
    // The rules: each break refused at its entry (`node`), then the valid stream. A stream ends at its first NONE.
    typedef struct Tier0TestLabelBreak Tier0TestLabelBreak;
    struct Tier0TestLabelBreak
    {
        const char* name;
        Tier0TestEntry entries[4];
        uint32_t label;
        uint32_t label_count;
        bool without_entries;
        uint32_t node;
    };
    static const Tier0TestLabelBreak breaks[] = {
        { "an indirect jump to an I64", { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 0 }, { BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, 0 },
                                          { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } }, 2, 1, false, 1 },
        { "an indirect jump of class I64", { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_I64, 0 },
                                             { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } }, 2, 1, false, 1 },
        { "an indirect jump with a value", { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, 5 },
                                             { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } }, 2, 1, false, 1 },
        { "an indirect jump with a value left", { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 },
                                                  { BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } },
          3, 1, false, 2 },
        { "an indirect jump without labels", { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, 0 },
                                               { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } }, 2, 0, false, 1 },
        { "a label past the end", { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, 0 },
                                    { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } }, 4, 1, false, 0 },
        { "the labels' count without their entries", { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, 0 },
                                                       { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } }, 2, 1, true, 0 },
        { "a label reached with a value", { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 7 }, { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 },
                                            { BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } },
          1, 1, false, 1 },
        { "valid", { { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0 }, { BUSTER_TYPED_JUMP_INDIRECT, BUSTER_TYPED_CLASS_U64, 0 },
                     { BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0 } }, 2, 1, false, UINT32_MAX },
    };
    for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(breaks); b += 1)
    {
        uint32_t count = 0;
        while (count < 4 && breaks[b].entries[count].kind != BUSTER_TYPED_NONE)
        {
            count += 1;
        }
        Tier0Function function = tier0_test_stack_function(breaks[b].entries, count, kinds, classes, values, local_classes, local_flags, 0);
        function.label_entries = breaks[b].without_entries ? 0 : &breaks[b].label;
        function.label_count = breaks[b].label_count;
        const Tier0Check check = tier0_validate(&function);
        const bool valid = breaks[b].node == UINT32_MAX;
        tier0_test_expect(tally, valid ? check.error == TIER0_OK : check.error == TIER0_ERROR_INVALID && check.node == breaks[b].node, "labels",
                          "%s: tier0_validate says %s at %u (%s)", breaks[b].name, check.error == TIER0_OK ? "valid" : "invalid", check.node,
                          check.rule != 0 ? check.rule : "");
    }
    // The function, its parameters in rdi and rsi.
    const uint32_t count = BUSTER_ARRAY_LENGTH(tier0_test_label_entries);
    static const AbiLocation parameters[2] = {
        { .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        { .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
    };
    Tier0Function function = tier0_test_stack_function(tier0_test_label_entries, count, kinds, classes, values, local_classes, local_flags, 4);
    function.parameter_count = 2;
    function.parameters = parameters;
    function.label_entries = tier0_test_label_targets;
    function.label_count = BUSTER_TIER0_TEST_LABEL_COUNT;
    Arena arena = arena_reserve(1ull << 26);
    uint8_t* image = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, 4 * BUSTER_TIER0_TEST_PAGE);
    uint8_t* twin = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, 4 * BUSTER_TIER0_TEST_PAGE);
    const Tier0Check check = tier0_validate(&function);
    const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
    const Tier0Code vector = tier0_compile(&function, &arena);
    bool ready = tier0_test_expect(tally, image != 0 && twin != 0 && check.error == TIER0_OK && scalar.error == TIER0_OK && tier0_test_same_code(&scalar, &vector),
                                   "labels", "the function: rule \"%s\" at %u, errors %d and %d, or the paths differ", check.rule != 0 ? check.rule : "", check.node,
                                   scalar.error, vector.error);
    // Each label's record is its first entry's: the one the jump to the loop's start, and a branch to a label, would go to.
    for (uint32_t k = 0; ready && k < BUSTER_TIER0_TEST_LABEL_COUNT; k += 1)
    {
        ready = tier0_test_expect(tally, scalar.label_count == BUSTER_TIER0_TEST_LABEL_COUNT && scalar.label_records[k] < scalar.count &&
                                             (k == 0 || scalar.label_records[k] > scalar.label_records[k - 1]),
                                  "labels", "label %u: record %u of %u", k, scalar.label_count != 0 ? scalar.label_records[k] : 0, scalar.count);
    }
    ready = ready && tier0_test_expect(tally, x86_validate(scalar.records, scalar.count) == scalar.count && scalar.reference_count == 0 &&
                                                  branch_sizing_validate(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count).error ==
                                                      BRANCH_SIZING_OK,
                                       "labels", "an invalid record, a reference or an invalid branch column");
    uint32_t* offsets = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, (uint64_t)scalar.count + 1) : 0;
    const BranchSizing sizing = offsets != 0 ? size_branches(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count, offsets) : (BranchSizing){ 0 };
    ready = ready && tier0_test_expect(tally, offsets != 0 && sizing.error == BRANCH_SIZING_OK, "labels", "the branches can't be sized");
    const uint64_t room = BUSTER_X86_ENCODER_CAPACITY(scalar.count);
    const int64_t length = ready && room <= 4 * BUSTER_TIER0_TEST_PAGE ? x86_encode(scalar.records, scalar.count, image, room, 0, 0) : -1;
    const int64_t twin_length = ready && room <= 4 * BUSTER_TIER0_TEST_PAGE ? x86_encode_scalar(scalar.records, scalar.count, twin, room, 0, 0) : -1;
    ready = ready && tier0_test_expect(tally, length > 0 && length == twin_length && memcmp(image, twin, (size_t)length) == 0, "labels",
                                       "the encoders differ, or fail (%" PRId64 ", %" PRId64 ")", length, twin_length);
    Tier0TestLabelShared* shared = mmap(0, sizeof(Tier0TestLabelShared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ready = ready && tier0_test_expect(tally, shared != MAP_FAILED, "labels", "no shared memory");
    if (ready)
    {
        uint64_t inputs[BUSTER_TIER0_TEST_LABEL_INPUTS] = { 0, 1, 2, 3, 4, 0x1b, 0xe4, 0xaaaa, 0xffff, UINT64_MAX, UINT64_C(0x0123456789abcdef), UINT64_C(0x8000000000000000) };
        const bool ran = tier0_test_label_native(image, (uint32_t)length, offsets, scalar.label_records, inputs, shared);
        tier0_test_expect(tally, ran, "labels", "the function didn't run to its end");
        for (uint32_t r = 0; ran && r < BUSTER_TIER0_TEST_LABEL_INPUTS; r += 1)
        {
            const uint64_t expected = tier0_test_label_expected(inputs[r]);
            tier0_test_expect(tally, shared->results[r] == expected, "labels", "f(%016" PRIx64 ") gave %016" PRIx64 ", %016" PRIx64 " expected", inputs[r],
                              shared->results[r], expected);
        }
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(Tier0TestLabelShared));
    }
    arena_release(&arena);
}
