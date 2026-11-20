// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-ir-vectorize [--seed N] [--print NAME] (#2743): the loop vectorizer's fixtures, tests/ir/vectorize/*.ir, one
// function each. Each is parsed by both parser paths and vectorized at three targets of the IR's every operation, 64, 32
// and 16 bytes wide (IrVectorizeTarget.moves), each result and its counts folded into a frozen digest (#2924); a
// fixture expects how many loops each width vectorizes, or the refusal it gives. Then the oracle, on every
// fixture a target changed: the input, the result and the result after the ordinary optimizer (whose final
// verification checks dominance, past if-conversion's stubs) run in the shared interpreter on one buffer, its pointer
// parameters placed apart, at one address, a byte, an element and a vector apart either way, at lengths around zero,
// one vector, two and their tails, and they must leave the same bytes, return the same value and end with the same
// status. Every mutant (BUSTER_IR_VECTORIZE_MUTATE_*) must be rejected by that oracle on some fixture. --print prints a
// fixture's result at 64 bytes. Include after ir_optimize_text_test.c (its file reader and structural comparison) and
// ir_interpreter.c.

#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)

#define BUSTER_IR_VECTORIZE_TEST_BUFFER 16384u
#define BUSTER_IR_VECTORIZE_TEST_SYMBOL 1u
#define BUSTER_IR_VECTORIZE_TEST_ENTRY 7u
// At most 64, 32 and 16 bytes wide with masked moves, then 64 bytes without them, whose tails are the scalar loop's,
// and 64 bytes with them but no select of 32- or 64-bit lanes, which a masked tail's accumulators need (#2807).
#define BUSTER_IR_VECTORIZE_TEST_TARGETS 5u

typedef struct IrVectorizeTest IrVectorizeTest;
struct IrVectorizeTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
    // Per mutant, whether the oracle rejected it on some fixture.
    bool killed[BUSTER_IR_VECTORIZE_MUTATIONS];
    const char* print;
    // An FNV-1a digest of every fixture's result and counts at every target, in order (#2924): frozen where the pass's
    // vector path and its scalar twin agreed. The fixtures don't depend on the seed, which picks execution inputs alone.
    uint64_t digest;
};

#define BUSTER_IR_VECTORIZE_TEST_DIGEST UINT64_C(0xefe73177faebaa65)

typedef struct IrVectorizeTestFixture IrVectorizeTestFixture;
struct IrVectorizeTestFixture
{
    const char* name;
    // A role per parameter: 'p' a pointer into the shared buffer, 'n' the loop's bound, 'm' a signed bound (negative too),
    // 'r' a count of rows (0 to 4), 's' a start below the bound, 'o' a small offset, 'k' any scalar.
    const char* roles;
    // The loops vectorized at 64, 32 and 16 bytes (and at 64 without masked moves).
    uint32_t vectorized[3];
    // The refusal a fixture that vectorizes nothing gives, or IR_VECTORIZE_REFUSALS.
    uint32_t refusal;
    // Whether its loop computes floating values, whose tail stays the scalar loop's (#2807): a masked one's inactive
    // lanes would raise flags.
    bool floating;
};

static const IrVectorizeTestFixture ir_vectorize_test_fixtures[] = {
    { .name = "add-i32", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "add-u8", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "sub-i16", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "mul-i64", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "xor-i32", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "fadd-f64", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .floating = true },
    { .name = "fmul-f32", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .floating = true },
    { .name = "fill-u32", .roles = "pkn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "copy-u8", .roles = "ppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    // 4- and 8-byte lanes: 8 lanes at 64 bytes, 4 at 32; at 16 bytes the doublewords would be 8 bytes.
    { .name = "sum-i32", .roles = "pn", .vectorized = { 1, 1, 0 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "sum-u8", .roles = "pnk", .vectorized = { 1, 0, 0 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "dot-i16", .roles = "ppn", .vectorized = { 1, 1, 0 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "reductions-i32", .roles = "pn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "cond-sum-i32", .roles = "pnk", .vectorized = { 1, 1, 0 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "clamp-i32", .roles = "ppkkm", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "scale-u8", .roles = "ppkn", .vectorized = { 1, 0, 0 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "popcount-u64", .roles = "pn", .vectorized = { 1, 1, 0 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "stencil-u8", .roles = "ppn", .vectorized = { 1, 0, 0 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "axpy-f32", .roles = "ppkn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .floating = true },
    { .name = "start-offset", .roles = "ppsno", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "uniform-load", .roles = "ppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "two-loops", .roles = "ppm", .vectorized = { 2, 2, 2 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "nested", .roles = "pprn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "refuse-prefix-sum", .roles = "pn", .refusal = IR_VECTORIZE_REFUSE_DEPENDENCE },
    { .name = "refuse-histogram", .roles = "ppn", .refusal = IR_VECTORIZE_REFUSE_ADDRESS },
    { .name = "refuse-strided", .roles = "ppn", .refusal = IR_VECTORIZE_REFUSE_ADDRESS },
    { .name = "refuse-iv-data", .roles = "pn", .refusal = IR_VECTORIZE_REFUSE_USE },
    { .name = "refuse-call", .roles = "pn", .refusal = IR_VECTORIZE_REFUSE_OPERATION },
    { .name = "refuse-volatile", .roles = "ppn", .refusal = IR_VECTORIZE_REFUSE_OPERATION },
    { .name = "refuse-division", .roles = "ppnk", .refusal = IR_VECTORIZE_REFUSE_OPERATION },
    { .name = "refuse-recurrence", .roles = "pn", .refusal = IR_VECTORIZE_REFUSE_PHI },
    { .name = "refuse-float-sum", .roles = "pn", .refusal = IR_VECTORIZE_REFUSE_PHI },
    { .name = "refuse-width", .roles = "pnk", .refusal = IR_VECTORIZE_REFUSE_WIDTH },
    { .name = "refuse-guards", .roles = "ppppppppppn", .refusal = IR_VECTORIZE_REFUSE_DEPENDENCE },
    { .name = "refuse-partial", .roles = "ppn", .refusal = IR_VECTORIZE_REFUSE_USE },
    { .name = "refuse-search", .roles = "pnk", .refusal = IR_VECTORIZE_REFUSE_SHAPE },
    { .name = "refuse-step", .roles = "pn", .refusal = IR_VECTORIZE_REFUSE_INDUCTION },
};

static void ir_vectorize_test_expect(IrVectorizeTest* test, bool condition, const char* name, const char* what)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        fprintf(stderr, "ir-vectorize: %s: %s\n", name, what);
    }
}

static uint64_t ir_vectorize_test_next(IrVectorizeTest* test)
{
    uint64_t x = test->random;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    test->random = x;
    return x;
}

// The five targets: every operation of the IR, its moves at most 64, 32 or 16 bytes wide; then at most 64 bytes without
// masked moves (an AVX2 or SSE2 target's tail at AVX-512's widths); then at most 64 bytes with no select of 32- or
// 64-bit lanes.
static void ir_vectorize_test_target(uint32_t index, IrVectorizeTarget* target)
{
    ir_vectorize_target_ir(target);
    target->moves = (uint8_t)(index == 1 ? 3 : index == 2 ? 1 : 7);
    for (uint32_t size = 0; index == 3 && size < 4; size += 1)
    {
        target->masked[size] = 0;
    }
    if (index == 4)
    {
        target->select[IR_SIMD_LANE_I32] = 0;
        target->select[IR_SIMD_LANE_I64] = 0;
    }
}

// A target's bytes at most: 64, 32, 16, then 64 twice.
static uint32_t ir_vectorize_test_bytes(uint32_t index)
{
    return index >= 3 ? 64u : 64u >> index;
}

// One run of `function` in the interpreter on `buffer` (its first `bytes` bytes the binding), the arguments placed by
// the fixture's roles.
static InterpreterResult ir_vectorize_test_run(const IrFunction* function, uint8_t* buffer, const InterpreterValue* arguments, uint32_t count, Arena* execution)
{
    const InterpreterBinding binding = { .symbol = BUSTER_IR_VECTORIZE_TEST_SYMBOL, .bytes = buffer, .size = BUSTER_IR_VECTORIZE_TEST_BUFFER, .writable = true };
    const IrInterpreterFunction entry = { .symbol = BUSTER_IR_VECTORIZE_TEST_ENTRY, .function = function };
    const IrInterpreterProgram program = { .functions = &entry, .function_count = 1, .bindings = &binding, .binding_count = 1 };
    InterpreterOptions options = interpreter_default_options();
    options.max_steps = 20000000ull;
    const uint64_t mark = execution->length;
    const InterpreterResult result = ir_interpret(&program, BUSTER_IR_VECTORIZE_TEST_ENTRY, arguments, count, &options, execution);
    arena_pop_to(execution, mark);
    return result;
}

// The arguments of one trial: pointers into `buffer`, the first at a random place in its middle and each next one apart
// from it by none, one byte, an element (of 1 to 8 bytes) or a vector either way, or far; the bound one of the lengths
// around the vectors' boundaries (a signed one negative too); a start and an offset below 40; rows below 5; scalars
// random. A 32-bit parameter takes the low bits.
static uint32_t ir_vectorize_test_arguments(IrVectorizeTest* test, const IrFunction* function, const char* roles, uint8_t* buffer, uint32_t trial, InterpreterValue* arguments)
{
    static const int64_t deltas[] = { 0, 1, -1, 2, -2, 4, -4, 8, -8, 16, -16, 63, -63, 64, -64, 128, -128, 4096 };
    static const int64_t lengths[] = { 0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100, 129, 255, 300 };
    static const int64_t signed_lengths[] = { -1, -17, INT32_MIN, 5 };
    const uint32_t count = function->parameter_count;
    uint64_t first = 0;
    const int64_t length = lengths[trial % BUSTER_ARRAY_LENGTH(lengths)];
    for (uint32_t k = 0; k < count && roles[k] != 0; k += 1)
    {
        const uint32_t type = function->parameter_types[k];
        uint64_t value = 0;
        if (roles[k] == 'p')
        {
            const uint64_t place = first == 0 ? 3072 + ir_vectorize_test_next(test) % 4096 : first + (uint64_t)deltas[ir_vectorize_test_next(test) % BUSTER_ARRAY_LENGTH(deltas)];
            first = first == 0 ? place : first;
            value = (uint64_t)(uintptr_t)(buffer + place);
        }
        else if (roles[k] == 'n')
        {
            value = (uint64_t)length;
        }
        else if (roles[k] == 'm')
        {
            value = trial % 7 == 6 ? (uint64_t)signed_lengths[(trial / 7) % BUSTER_ARRAY_LENGTH(signed_lengths)] : (uint64_t)length;
        }
        else if (roles[k] == 'r')
        {
            value = trial % 5;
        }
        else if (roles[k] == 's' || roles[k] == 'o')
        {
            value = ir_vectorize_test_next(test) % 40;
        }
        else
        {
            value = ir_vectorize_test_next(test);
        }
        arguments[k] = (InterpreterValue){ .low = value & ir_vectorize_type_mask(type) };
    }
    return count;
}

// The oracle: `before` and `after` on the same random buffers and arguments, many trials. Returns whether they agreed
// on every one.
static bool ir_vectorize_test_agree(IrVectorizeTest* test, const IrVectorizeTestFixture* fixture, const IrFunction* before, const IrFunction* after, Arena* execution, uint8_t* buffers)
{
    uint8_t* initial = buffers;
    uint8_t* left = buffers + BUSTER_IR_VECTORIZE_TEST_BUFFER;
    uint8_t* right = buffers + 2 * BUSTER_IR_VECTORIZE_TEST_BUFFER;
    bool agree = true;
    for (uint32_t trial = 0; agree && trial < 132; trial += 1)
    {
        for (uint32_t k = 0; k < BUSTER_IR_VECTORIZE_TEST_BUFFER; k += 8)
        {
            const uint64_t word = ir_vectorize_test_next(test);
            memcpy(initial + k, &word, 8);
        }
        InterpreterValue arguments[16] = { 0 };
        InterpreterValue moved[16] = { 0 };
        const uint32_t count = ir_vectorize_test_arguments(test, before, fixture->roles, left, trial, arguments);
        // The same places in the other buffer.
        for (uint32_t k = 0; k < count; k += 1)
        {
            const bool pointer = fixture->roles[k] == 'p';
            moved[k] = arguments[k];
            moved[k].low = pointer ? arguments[k].low - (uint64_t)(uintptr_t)left + (uint64_t)(uintptr_t)right : arguments[k].low;
        }
        memcpy(left, initial, BUSTER_IR_VECTORIZE_TEST_BUFFER);
        memcpy(right, initial, BUSTER_IR_VECTORIZE_TEST_BUFFER);
        const InterpreterResult a = ir_vectorize_test_run(before, left, arguments, count, execution);
        const InterpreterResult b = ir_vectorize_test_run(after, right, moved, count, execution);
        // A returned pointer would differ by the buffers' distance: no fixture returns one.
        agree = a.status == INTERPRETER_OK && a.status == b.status && a.value.low == b.value.low && a.value.high == b.value.high && memcmp(left, right, BUSTER_IR_VECTORIZE_TEST_BUFFER) == 0;
    }
    return agree;
}

// One fixture: both paths at each target, the counts, and the oracle on each changed result and its optimized form;
// then the mutants at 64 bytes.
static void ir_vectorize_test_fixture(IrVectorizeTest* test, const IrVectorizeTestFixture* fixture, Arena* source, Arena* input, Arena* vector, Arena* cleanup, Arena* scratch, Arena* execution, uint8_t* buffers)
{
    char path[256];
    snprintf(path, sizeof(path), "tests/ir/vectorize/%s.ir", fixture->name);
    String8 text = { 0 };
    IrFunction function = { 0 };
    IrFunction twin = { 0 };
    const bool parsed = ir_optimize_text_read(path, source, &text) && ir_optimize_text_parse(text, input, execution, scratch, &function, &twin);
    // The twin was compared; only the first is kept.
    arena_pop_to(execution, 0);
    ir_vectorize_test_expect(test, parsed, fixture->name, "the fixture is read and parsed alike by both parser paths");
    for (uint32_t t = 0; parsed && t < BUSTER_IR_VECTORIZE_TEST_TARGETS; t += 1)
    {
        IrVectorizeTarget target = { 0 };
        ir_vectorize_test_target(t, &target);
        const uint64_t vm = vector->length;
        const uint64_t cm = cleanup->length;
        const uint64_t tm = scratch->length;
        const IrVectorizeOptions options = { .target = &target };
        IrVectorizeResult a = { 0 };
        const IrCheck ac = ir_vectorize_loops(&function, &options, vector, scratch, &a);
        char what[256];
        const uint32_t bytes = ir_vectorize_test_bytes(t);
        snprintf(what, sizeof(what), "the pass accepts it and restores scratch at %u bytes%s", bytes, t == 3 ? " without masked moves" : "");
        ir_vectorize_test_expect(test, ac.error == IR_OK && scratch->length == tm, fixture->name, what);
        if (ac.error == IR_OK)
        {
            ir_optimize_test_fold_function(&test->digest, &a.function);
            const uint64_t counts[] = {
                a.changed,
                a.loops,
                a.vectorized,
                a.reductions,
                a.guards,
                a.masked,
            };
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(counts); k += 1)
            {
                ir_optimize_test_fold(&test->digest, counts[k]);
            }
            for (uint32_t k = 0; k < BUSTER_IR_VECTORIZE_WIDTHS; k += 1)
            {
                ir_optimize_test_fold(&test->digest, a.widths[k]);
            }
            for (uint32_t k = 0; k < IR_VECTORIZE_REFUSALS; k += 1)
            {
                ir_optimize_test_fold(&test->digest, a.refused[k]);
            }
        }
        // Without selects of 32- and 64-bit lanes (t == 4) a loop may vectorize or not: its tail is the scalar loop's.
        const uint32_t expected = fixture->refusal == IR_VECTORIZE_REFUSALS ? fixture->vectorized[t >= 3 ? 0 : t] : 0;
        snprintf(what, sizeof(what), "%u loops vectorized at %u bytes, %u expected", a.vectorized, bytes, expected);
        ir_vectorize_test_expect(test, ac.error == IR_OK && (t == 4 ? a.vectorized <= expected : a.vectorized == expected && a.changed == (expected != 0)), fixture->name, what);
        // #2807: every integer loop's tail is masked where the target has masked moves, and none otherwise; without
        // selects of 32- and 64-bit lanes, a loop's with no reduction alone, whose accumulators need one (no fixture
        // has two loops with reductions: each reduction is then one loop's).
        const uint32_t reducing = a.reductions < a.vectorized ? a.reductions : a.vectorized;
        const uint32_t masked = t == 3 || fixture->floating ? 0 : t == 4 ? a.vectorized - reducing : expected;
        snprintf(what, sizeof(what), "%u masked tails at %u bytes, %u expected", a.masked, bytes, masked);
        ir_vectorize_test_expect(test, ac.error == IR_OK && a.masked == masked, fixture->name, what);
        if (fixture->refusal != IR_VECTORIZE_REFUSALS && t == 0)
        {
            snprintf(what, sizeof(what), "refused for its reason %u", fixture->refusal);
            ir_vectorize_test_expect(test, ac.error == IR_OK && a.loops != 0 && a.refused[fixture->refusal] != 0 && !a.changed, fixture->name, what);
        }
        if (ac.error == IR_OK && a.changed)
        {
            if (t == 0 && test->print != NULL && strcmp(test->print, fixture->name) == 0)
            {
                ir_print(&a.function, scratch, stdout);
            }
            // The ordinary optimizer after it: SCCP, dead code, CFG cleanup and its merges, and the final verification with
            // dominance (the pass's own is structural: the input may hold if-conversion's unreachable stubs).
            IrOptimizeOptions optimize = ir_optimize_options();
            optimize.passes |= BUSTER_IR_OPTIMIZE_MERGE;
            IrOptimizeResult optimized = { 0 };
            const IrCheck oc = ir_optimize(&a.function, cleanup, scratch, optimize, &optimized);
            snprintf(what, sizeof(what), "the optimizer takes the result at %u bytes and verifies it with dominance (error %u)", bytes, oc.error);
            ir_vectorize_test_expect(test, oc.error == IR_OK, fixture->name, what);
            snprintf(what, sizeof(what), "the result runs as the input does at %u bytes", bytes);
            ir_vectorize_test_expect(test, ir_vectorize_test_agree(test, fixture, &function, &a.function, execution, buffers), fixture->name, what);
            snprintf(what, sizeof(what), "the optimized result runs as the input does at %u bytes", bytes);
            ir_vectorize_test_expect(test, oc.error == IR_OK && ir_vectorize_test_agree(test, fixture, &function, &optimized.function, execution, buffers), fixture->name, what);
        }
        arena_pop_to(vector, vm);
        arena_pop_to(cleanup, cm);
    }
    // The mutants, at 64 bytes: a mutant the verifier refuses, or whose function runs otherwise, is killed.
    for (uint32_t mutation = 1; parsed && fixture->refusal == IR_VECTORIZE_REFUSALS && mutation < BUSTER_IR_VECTORIZE_MUTATIONS; mutation += 1)
    {
        IrVectorizeTarget target = { 0 };
        ir_vectorize_test_target(0, &target);
        const uint64_t vm = vector->length;
        const IrVectorizeOptions options = { .target = &target, .mutation = mutation };
        IrVectorizeResult mutant = { 0 };
        const IrCheck check = ir_vectorize_loops(&function, &options, vector, scratch, &mutant);
        const bool killed = check.error != IR_OK || (mutant.changed && !ir_vectorize_test_agree(test, fixture, &function, &mutant.function, execution, buffers));
        test->killed[mutation] = test->killed[mutation] || killed;
        arena_pop_to(vector, vm);
    }
    arena_pop_to(input, 0);
    arena_pop_to(source, 0);
}

// #2924: the rows' admission kinds and the refusing kinds' scan against their definitions, on random columns: every length
// to 200 (the kinds) and every range of up to 130 rows at several places (the scan).
static void ir_vectorize_test_scans(IrVectorizeTest* test)
{
    IrOptimizeTest columns = { .random = test->random };
    IrOptimizeTestScan scan;
    uint8_t kinds[BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS];
    bool classified = true;
    bool forbidden = true;
    uint32_t refusals = 0;
    for (uint32_t round = 0; round < 32; round += 1)
    {
        ir_optimize_test_scan_fill(&columns, &scan);
        IrFunction f = scan.function;
        for (uint32_t length = round % 7; length <= BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS; length += 11)
        {
            f.instruction_count = length;
            memset(kinds, 0xa5, sizeof(kinds));
            ir_vectorize_kinds(&f, kinds);
            bool same = true;
            for (uint32_t i = 0; i < BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS; i += 1)
            {
                const uint32_t kind = ir_vectorize_kind_table[f.op[i]];
                const uint32_t base = kind & 7;
                const bool flagged = (base == BUSTER_IR_VECTORIZE_KIND_LOAD || base == BUSTER_IR_VECTORIZE_KIND_STORE) && (f.flags[i] & BUSTER_IR_ACCESS_FLAGS) != 0;
                const uint32_t expected = i >= length ? 0xa5 : flagged ? BUSTER_IR_VECTORIZE_KIND_FORBID : kind;
                same = same && kinds[i] == expected;
            }
            classified &= same;
        }
        f.instruction_count = BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS;
        ir_vectorize_kinds(&f, kinds);
        for (uint32_t first = round % 5; first < BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS; first += 23)
        {
            for (uint32_t end = first; end <= BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS && end - first <= 130; end += 1)
            {
                bool expected = false;
                for (uint32_t at = first; at < end; at += 1)
                {
                    expected = expected || (kinds[at] & 7) == BUSTER_IR_VECTORIZE_KIND_FORBID;
                }
                forbidden &= ir_vectorize_forbidden(kinds, first, end) == expected;
                refusals += expected;
            }
        }
    }
    ir_vectorize_test_expect(test, classified, "scans", "the rows' admission kinds, against their definition");
    ir_vectorize_test_expect(test, forbidden && refusals != 0, "scans", "the refusing kinds' scan, against its definition");
}

static __attribute__((unused)) int run_ir_vectorize_tests(int argc, char** argv)
{
    IrVectorizeTest test = {
        .random = 2743,
        .digest = BUSTER_IR_OPTIMIZE_TEST_DIGEST_BASIS,
    };
    bool valid = true;
    for (int i = 1; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            char* end = 0;
            errno = 0;
            test.random = strtoull(argv[++i], &end, 0);
            valid = valid && errno == 0 && *end == 0 && test.random != 0;
        }
        else if (strcmp(argv[i], "--print") == 0 && i + 1 < argc)
        {
            test.print = argv[++i];
        }
        else
        {
            valid = false;
        }
    }
    int result = 2;
    if (valid)
    {
        Arena source = arena_reserve(UINT64_C(1) << 24);
        Arena input = arena_reserve(UINT64_C(1) << 26);
        Arena vector = arena_reserve(UINT64_C(1) << 26);
        Arena cleanup = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 28);
        Arena execution = arena_reserve(UINT64_C(1) << 28);
        uint8_t* buffers = calloc(3, BUSTER_IR_VECTORIZE_TEST_BUFFER);
        const bool ready = source.base != 0 && input.base != 0 && vector.base != 0 && cleanup.base != 0 && scratch.base != 0 && execution.base != 0 && buffers != NULL;
        ir_vectorize_test_expect(&test, ready, "setup", "the arenas and buffers are reserved");
        for (uint32_t k = 0; ready && k < BUSTER_ARRAY_LENGTH(ir_vectorize_test_fixtures); k += 1)
        {
            ir_vectorize_test_fixture(&test, &ir_vectorize_test_fixtures[k], &source, &input, &vector, &cleanup, &scratch, &execution, buffers);
        }
        ir_vectorize_test_expect(&test, test.digest == BUSTER_IR_VECTORIZE_TEST_DIGEST, "fixtures", "every result and its counts have their frozen digest");
        if (test.digest != BUSTER_IR_VECTORIZE_TEST_DIGEST)
        {
            fprintf(stderr, "ir-vectorize: digest %#018" PRIx64 "\n", test.digest);
        }
        ir_vectorize_test_scans(&test);
        static const char* const mutants[BUSTER_IR_VECTORIZE_MUTATIONS] = { "none", "no overlap test", "an unrounded tail", "a reduction's identity", "the lanes' width", "a masked tail's lane more" };
        for (uint32_t mutation = 1; ready && mutation < BUSTER_IR_VECTORIZE_MUTATIONS; mutation += 1)
        {
            char what[128];
            snprintf(what, sizeof(what), "the oracle rejects the mutant of %s on some fixture", mutants[mutation]);
            ir_vectorize_test_expect(&test, test.killed[mutation], "mutants", what);
        }
        free(buffers);
        arena_release(&source);
        arena_release(&input);
        arena_release(&vector);
        arena_release(&cleanup);
        arena_release(&scratch);
        arena_release(&execution);
        printf("ir-vectorize: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
        result = test.failures == 0 ? 0 : 1;
    }
    else
    {
        fprintf(stderr, "usage: test-ir-vectorize [--seed N] [--print NAME]\n");
    }
    return result;
}

#endif
