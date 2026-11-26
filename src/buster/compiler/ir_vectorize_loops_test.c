// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-ir-vectorize [--seed N] [--print NAME] (#2743): the loop vectorizer's fixtures, tests/ir/vectorize/*.ir, one
// function each. Each is parsed by both parser paths and vectorized at six targets: three of the IR's every operation,
// 64, 32 and 16 bytes wide (IrVectorizeTarget.moves), then three at 64 bytes without masked moves, without selects of
// 32- and 64-bit lanes, and without masks of 8 lanes or fewer (#2807); each result and its counts folded into a frozen
// digest (#2924). A fixture expects how many loops each width vectorizes, or the refusal it gives, and how many of them
// have a masked tail. Then the oracle, on every
// fixture a target changed: the input, the result and the result after the ordinary optimizer (whose final
// verification checks dominance, past if-conversion's stubs) run in the shared interpreter on one buffer, its pointer
// parameters placed apart, at one address, a byte, an element and a vector apart either way, at lengths around zero,
// one vector, two and their tails, and they must leave the same bytes, return the same value and end with the same
// status. Every mutant (BUSTER_IR_VECTORIZE_MUTATE_*) must be rejected by that oracle on some fixture. --print prints a
// fixture's result at 64 bytes. Include after ir_optimize_text_test.c (its file reader and structural comparison) and
// ir_interpreter.c.

#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)

#define BUSTER_IR_VECTORIZE_TEST_BUFFER 16384u
#define BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER 131072u
#define BUSTER_IR_VECTORIZE_TEST_SYMBOL 1u
#define BUSTER_IR_VECTORIZE_TEST_ENTRY 7u
// At most 64, 32 and 16 bytes wide with masked moves, then 64 bytes without them, whose tails are the scalar loop's,
// 64 bytes with them but no select of 32- or 64-bit lanes, which a masked tail's accumulators need, and 64 bytes with
// them but no mask of 8 lanes or fewer from a general register (an AVX512F target without DQ's kmovb) (#2807).
#define BUSTER_IR_VECTORIZE_TEST_TARGETS 6u

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
    // vector path and its scalar twin agreed, then refrozen for #2808's count and strided-store admissions. For the
    // latter, every other original fixture's transformed IR and counts at all six targets match the base byte for
    // byte. The fixtures don't depend on the seed, which picks execution inputs alone.
    uint64_t digest;
};

#define BUSTER_IR_VECTORIZE_TEST_DIGEST UINT64_C(0xf27b1ae257b44c8d)

typedef struct IrVectorizeTestFixture IrVectorizeTestFixture;
struct IrVectorizeTestFixture
{
    const char* name;
    // A role per parameter: 'p' a pointer into the shared buffer, 'n' the loop's bound, 'm' a signed bound (negative too),
    // 'r' a count of rows (0 to 4), 's' a start below the bound, 'o' a small offset, 'k' any scalar; and for a 32-bit
    // index that wraps (#2808), 'w' 0xfffffff8 (a wrap within the first 64-byte vector of 4-byte lanes) and 'q' a pointer that many 4-byte elements before one into the buffer;
    // and 'z' a pointer to the bound's elements 64 bytes apart whose last 8 bytes end the buffer (#2808: a strided
    // load's bytes past the last element would be outside it). The remaining strided cases use 'v' for a pointer
    // around the middle of the larger buffer, 'h' for one whose last accessed byte ends the binding, or 'l' for one
    // whose first accessed byte starts it. access_step and access_size describe those parameters' byte ranges.
    // 'j' starts at 0x80000020; 'y' puts 4 * sext(i32 j)'s byte inside the binding, while 4 * zext(j)'s is outside it.
    const char* roles;
    // The loops vectorized at 64, 32 and 16 bytes (and at 64 without masked moves).
    uint32_t vectorized[3];
    // The refusal a fixture that vectorizes nothing gives, or IR_VECTORIZE_REFUSALS.
    uint32_t refusal;
    // Whether its loop computes floating values, whose tail stays the scalar loop's (#2807): a masked one's inactive
    // lanes would raise flags.
    bool floating;
    // Its integer loops of 8 lanes or fewer at 64 bytes (a lane of 8 bytes), whose mask is a byte's (#2807).
    uint32_t byte_masks;
    // Its counts (#2808), reductions whose masked tail needs no select.
    uint32_t counts;
    // Whether a load is strided (#2808): its tail is the scalar loop's.
    bool strided;
    // Whether some trials fault (#2808: a 32-bit index's wrap): the input and the result must fault alike, leaving the
    // same bytes.
    bool faults;
    // New large/reverse-stride fixtures use a larger binding without changing the original fixtures' input stream.
    uint32_t memory_bytes;
    int32_t access_step[3];
    uint32_t access_size[3];
    // A synthetic target without a mask/memory form may keep the entire loop scalar.
    uint32_t scalar_targets;
    // Nonzero: the exact appended vector loads at the full target, including masked loads. Grouping is observable
    // here even when duplicated loads would still execute correctly.
    uint32_t vector_loads;
};

static const IrVectorizeTestFixture ir_vectorize_test_fixtures[] = {
    { .name = "add-i32", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "add-u8", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "sub-i16", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "mul-i64", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .byte_masks = 1 },
    { .name = "xor-i32", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "fadd-f64", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .floating = true },
    { .name = "fmul-f32", .roles = "pppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .floating = true },
    { .name = "fill-u32", .roles = "pkn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "copy-u8", .roles = "ppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    // A reduction of a uniform: no vector load or store, the masked tail's mask from a general register alone (#2807).
    { .name = "sum-uniform-i64", .roles = "kn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .byte_masks = 1 },
    // 4- and 8-byte lanes: 8 lanes at 64 bytes, 4 at 32; at 16 bytes the doublewords would be 8 bytes.
    { .name = "sum-i32", .roles = "pn", .vectorized = { 1, 1, 0 }, .refusal = IR_VECTORIZE_REFUSALS, .byte_masks = 1 },
    { .name = "sum-u8", .roles = "pnk", .vectorized = { 1, 0, 0 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "dot-i16", .roles = "ppn", .vectorized = { 1, 1, 0 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "reductions-i32", .roles = "pn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "cond-sum-i32", .roles = "pnk", .vectorized = { 1, 1, 0 }, .refusal = IR_VECTORIZE_REFUSALS, .byte_masks = 1 },
    { .name = "clamp-i32", .roles = "ppkkm", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "scale-u8", .roles = "ppkn", .vectorized = { 1, 0, 0 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "popcount-u64", .roles = "pn", .vectorized = { 1, 1, 0 }, .refusal = IR_VECTORIZE_REFUSALS, .byte_masks = 1 },
    { .name = "stencil-u8", .roles = "ppn", .vectorized = { 1, 0, 0 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "axpy-f32", .roles = "ppkn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .floating = true },
    { .name = "fma-f32", .roles = "ppkn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .floating = true },
    { .name = "sitofp-i32", .roles = "ppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .floating = true },
    { .name = "start-offset", .roles = "ppsno", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "uniform-load", .roles = "ppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .byte_masks = 1 },
    { .name = "two-loops", .roles = "ppm", .vectorized = { 2, 2, 2 }, .refusal = IR_VECTORIZE_REFUSALS, .byte_masks = 2 },
    { .name = "nested", .roles = "pprn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
    { .name = "refuse-prefix-sum", .roles = "pn", .refusal = IR_VECTORIZE_REFUSE_DEPENDENCE },
    { .name = "refuse-histogram", .roles = "ppn", .refusal = IR_VECTORIZE_REFUSE_ADDRESS },
    { .name = "refuse-strided", .roles = "ppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true, .scalar_targets = 1u << 3 },
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

// The six targets: every operation of the IR, its moves at most 64, 32 or 16 bytes wide; then at most 64 bytes without
// masked moves or masks from a general register (an AVX2 or SSE2 target's tail at AVX-512's widths); then at most 64
// bytes with no select of 32- or 64-bit lanes; then at most 64 bytes with no mask of 8 lanes or fewer from a general
// register.
static void ir_vectorize_test_target(uint32_t index, IrVectorizeTarget* target)
{
    ir_vectorize_target_ir(target);
    target->moves = (uint8_t)(index == 1 ? 3 : index == 2 ? 1 : 7);
    for (uint32_t size = 0; index == 3 && size < 4; size += 1)
    {
        target->masked[size] = 0;
    }
    target->mask = (uint8_t)(index == 3 ? 0 : index == 5 ? 0x70 : target->mask);
    if (index == 4)
    {
        target->select[IR_SIMD_LANE_I32] = 0;
        target->select[IR_SIMD_LANE_I64] = 0;
    }
}

// A target's bytes at most: 64, 32, 16, then 64 three times.
static uint32_t ir_vectorize_test_bytes(uint32_t index)
{
    return index >= 3 ? 64u : 64u >> index;
}

// One run of `function` in the interpreter on `buffer` (its first `bytes` bytes the binding), the arguments placed by
// the fixture's roles.
static InterpreterResult ir_vectorize_test_run(const IrFunction* function, uint8_t* buffer, uint32_t bytes, const InterpreterValue* arguments, uint32_t count, Arena* execution)
{
    const InterpreterBinding binding = { .symbol = BUSTER_IR_VECTORIZE_TEST_SYMBOL, .bytes = buffer, .size = bytes, .writable = true };
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
static uint32_t ir_vectorize_test_arguments(IrVectorizeTest* test, const IrVectorizeTestFixture* fixture, const IrFunction* function, uint8_t* buffer, uint32_t bytes, uint32_t trial, InterpreterValue* arguments)
{
    static const int64_t deltas[] = { 0, 1, -1, 2, -2, 4, -4, 8, -8, 16, -16, 63, -63, 64, -64, 128, -128, 4096 };
    static const int64_t lengths[] = { 0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100, 129, 255, 300 };
    static const int64_t signed_lengths[] = { -1, -17, INT32_MIN, 5 };
    const uint32_t count = function->parameter_count;
    const char* roles = fixture->roles;
    uint64_t first = 0;
    const int64_t length = lengths[trial % BUSTER_ARRAY_LENGTH(lengths)];
    for (uint32_t k = 0; k < count && roles[k] != 0; k += 1)
    {
        const uint32_t type = function->parameter_types[k];
        uint64_t value = 0;
        if (roles[k] == 'p' || roles[k] == 'q' || roles[k] == 'y')
        {
            const uint64_t place = first == 0 ? 3072 + ir_vectorize_test_next(test) % 4096 : first + (uint64_t)deltas[ir_vectorize_test_next(test) % BUSTER_ARRAY_LENGTH(deltas)];
            first = first == 0 ? place : first;
            value = (uint64_t)(uintptr_t)(buffer + place) - (roles[k] == 'q' ? UINT64_C(0xfffffff8) * 4 : 0) + (roles[k] == 'y' ? UINT64_C(0x7fffffe0) * 4 : 0);
        }
        else if (roles[k] == 'w')
        {
            value = UINT64_C(0xfffffff8);
        }
        else if (roles[k] == 'j')
        {
            value = UINT64_C(0x80000020);
        }
        else if (roles[k] == 'z')
        {
            const uint64_t span = length > 0 ? (uint64_t)(length - 1) * 64 + 8 : 64;
            value = (uint64_t)(uintptr_t)(buffer + BUSTER_IR_VECTORIZE_TEST_BUFFER - span);
        }
        else if (roles[k] == 'v' || roles[k] == 'h' || roles[k] == 'l')
        {
            const int64_t step = k < BUSTER_ARRAY_LENGTH(fixture->access_step) ? fixture->access_step[k] : 0;
            const uint64_t size = k < BUSTER_ARRAY_LENGTH(fixture->access_size) ? fixture->access_size[k] : 0;
            const uint64_t distance = length > 0 ? (uint64_t)(length - 1) * (uint64_t)(step < 0 ? -step : step) : 0;
            uint64_t place = first == 0 ? bytes / 2 + ir_vectorize_test_next(test) % 127 : first + (uint64_t)deltas[ir_vectorize_test_next(test) % BUSTER_ARRAY_LENGTH(deltas)];
            if (roles[k] == 'h')
            {
                place = bytes - size - (step > 0 ? distance : 0);
            }
            else if (roles[k] == 'l')
            {
                place = step < 0 ? distance : 0;
            }
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
    const uint32_t bytes = fixture->memory_bytes == 0 ? BUSTER_IR_VECTORIZE_TEST_BUFFER : fixture->memory_bytes;
    uint8_t* initial = buffers;
    uint8_t* left = buffers + bytes;
    uint8_t* right = buffers + 2 * bytes;
    bool agree = true;
    for (uint32_t trial = 0; agree && trial < 132; trial += 1)
    {
        for (uint32_t k = 0; k < bytes; k += 8)
        {
            const uint64_t word = ir_vectorize_test_next(test);
            memcpy(initial + k, &word, 8);
        }
        InterpreterValue arguments[16] = { 0 };
        InterpreterValue moved[16] = { 0 };
        const uint32_t count = ir_vectorize_test_arguments(test, fixture, before, left, bytes, trial, arguments);
        // The same places in the other buffer.
        for (uint32_t k = 0; k < count; k += 1)
        {
            const bool pointer = fixture->roles[k] == 'p' || fixture->roles[k] == 'q' || fixture->roles[k] == 'y' || fixture->roles[k] == 'z' || fixture->roles[k] == 'v' || fixture->roles[k] == 'h' || fixture->roles[k] == 'l';
            moved[k] = arguments[k];
            moved[k].low = pointer ? arguments[k].low - (uint64_t)(uintptr_t)left + (uint64_t)(uintptr_t)right : arguments[k].low;
        }
        memcpy(left, initial, bytes);
        memcpy(right, initial, bytes);
        const InterpreterResult a = ir_vectorize_test_run(before, left, bytes, arguments, count, execution);
        const InterpreterResult b = ir_vectorize_test_run(after, right, bytes, moved, count, execution);
        // A returned pointer would differ by the buffers' distance: no fixture returns one. A fault's value is none.
        const bool ok = a.status == INTERPRETER_OK;
        const bool value = !ok || (a.value.low == b.value.low && a.value.high == b.value.high);
        agree = (ok || fixture->faults) && a.status == b.status && value && memcmp(left, right, bytes) == 0;
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
    const bool parsed = ir_optimize_text_read(path, source, &text) && ir_optimize_text_parse(text, input, scratch, &function);
    ir_vectorize_test_expect(test, parsed, fixture->name, "the fixture is read and parsed");
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
        snprintf(what, sizeof(what), "the pass accepts it and restores scratch at %u bytes%s", bytes, t == 3 ? " without masked moves" : t == 4 ? " without 32- and 64-bit selects" : t == 5 ? " without byte masks" : "");
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
        const uint32_t expected = fixture->refusal == IR_VECTORIZE_REFUSALS && ((fixture->scalar_targets >> t) & 1) == 0 ? fixture->vectorized[t >= 3 ? 0 : t] : 0;
        snprintf(what, sizeof(what), "%u loops vectorized at %u bytes, %u expected", a.vectorized, bytes, expected);
        ir_vectorize_test_expect(test, ac.error == IR_OK && (t == 4 ? a.vectorized <= expected : a.vectorized == expected && a.changed == (expected != 0)), fixture->name, what);
        // #2807: every integer loop's tail is masked where the target has masked moves, and none otherwise; without
        // selects of 32- and 64-bit lanes, a loop's with no reduction alone, whose accumulators need one (no fixture
        // has two loops with reductions: each reduction is then one loop's); without masks of 8 lanes or fewer from a
        // general register, a loop's of more lanes alone.
        // A count (#2808) needs no select; a strided load keeps the scalar tail.
        const uint32_t selecting = a.reductions - (fixture->counts < a.reductions ? fixture->counts : a.reductions);
        const uint32_t reducing = selecting < a.vectorized ? selecting : a.vectorized;
        const uint32_t masked = t == 3 || fixture->floating || fixture->strided ? 0 : t == 4 ? a.vectorized - reducing : t == 5 ? expected - fixture->byte_masks : expected;
        snprintf(what, sizeof(what), "%u masked tails at %u bytes, %u expected", a.masked, bytes, masked);
        ir_vectorize_test_expect(test, ac.error == IR_OK && a.masked == masked, fixture->name, what);
        if (fixture->refusal != IR_VECTORIZE_REFUSALS && t == 0)
        {
            snprintf(what, sizeof(what), "refused for its reason %u", fixture->refusal);
            ir_vectorize_test_expect(test, ac.error == IR_OK && a.loops != 0 && a.refused[fixture->refusal] != 0 && !a.changed, fixture->name, what);
        }
        if (ac.error == IR_OK && a.changed)
        {
            if (t == 0 && fixture->vector_loads != 0)
            {
                uint32_t loads = 0;
                for (uint32_t i = function.instruction_count; i < a.function.instruction_count; i += 1)
                {
                    loads += ir_vector_type(a.function.type[i]) && (a.function.op[i] == IR_LOAD || a.function.op[i] == IR_MASKED_LOAD);
                }
                snprintf(what, sizeof(what), "%u vector loads after grouping, %u expected", loads, fixture->vector_loads);
                ir_vectorize_test_expect(test, loads == fixture->vector_loads, fixture->name, what);
            }
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
        uint8_t* buffers = calloc(3, BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER);
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
        // These cases postdate the frozen digest from #2924. Check their admission and execution at each width.
        static const IrVectorizeTestFixture minmax[] = {
            { .name = "minmax-select", .roles = "pnk", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS },
            { .name = "refuse-minmax-clobber", .roles = "pnk", .refusal = IR_VECTORIZE_REFUSE_SHAPE },
            { .name = "refuse-minmax-partial", .roles = "ppnk", .refusal = IR_VECTORIZE_REFUSE_USE },
            // #2808: counts of comparisons (bytes compared as bytes, into a 64-bit count; an int's into an int), strided
            // loads (#2743's array-of-structs sum, RGB to gray through 32-bit indices, pairs, a stride of eight bytes),
            // and a 32-bit index that wraps.
            { .name = "count-u8", .roles = "pnk", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .counts = 1 },
            { .name = "count-i32", .roles = "pnkk", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .counts = 1 },
            { .name = "aos-sum", .roles = "pn", .vectorized = { 1, 1, 0 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true, .vector_loads = 2 },
            { .name = "rgb-gray", .roles = "ppm", .vectorized = { 1, 0, 0 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true, .vector_loads = 1 },
            { .name = "strided-pairs-i16", .roles = "ppn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true },
            { .name = "stride8-u8", .roles = "pn", .vectorized = { 1, 0, 0 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true },
            // Elements 64 bytes apart, the last ending the buffer (past 255 of them, its start is before the buffer: both fault):
            // 2 lanes of 8 bytes at 16 bytes take 5 loads, not 8.
            { .name = "stride8-i64", .roles = "zn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true, .faults = true },
            { .name = "narrow-wrap", .roles = "qnw", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .faults = true },
        };
        for (uint32_t k = 0; ready && k < BUSTER_ARRAY_LENGTH(minmax); k += 1)
        {
            ir_vectorize_test_fixture(&test, &minmax[k], &source, &input, &vector, &cleanup, &scratch, &execution, buffers);
        }
        // #2808's remaining affine accesses: exact binding ends, reverse traversal, large strides, every stored
        // lane width, and aliased input/output pointers. The whole binding is compared, including untouched gaps.
        static const IrVectorizeTestFixture strided[] = {
            { .name = "stride9-i32", .roles = "hn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { 36 }, .access_size = { 4 }, .scalar_targets = 1u << 3 },
            { .name = "stride17-i64", .roles = "hn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { 136 }, .access_size = { 8 }, .scalar_targets = (1u << 3) | (1u << 5) },
            { .name = "reverse-i32", .roles = "ln", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { -4 }, .access_size = { 4 } },
            { .name = "reverse-stride17-i64", .roles = "ln", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { -136 }, .access_size = { 8 }, .scalar_targets = (1u << 3) | (1u << 5) },
            { .name = "reverse-stride3-i16", .roles = "vvn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { 2, -6 }, .access_size = { 2, 2 } },
            { .name = "reverse-stride9-i32", .roles = "vvn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { 4, -36 }, .access_size = { 4, 4 }, .scalar_targets = 1u << 3 },
            { .name = "store-stride3-u8", .roles = "vvn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { 3, 1 }, .access_size = { 1, 1 }, .scalar_targets = 1u << 3 },
            { .name = "store-reverse-stride3-i16", .roles = "vvn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { -6, 2 }, .access_size = { 2, 2 }, .scalar_targets = 1u << 3 },
            { .name = "store-stride9-i32", .roles = "vvn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { 36, 4 }, .access_size = { 4, 4 }, .scalar_targets = 1u << 3 },
            { .name = "store-reverse-stride17-i64", .roles = "vvn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { -136, 8 }, .access_size = { 8, 8 }, .scalar_targets = (1u << 3) | (1u << 5) },
            { .name = "copy-opposite-strides-i32", .roles = "vvn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { -36, 36 }, .access_size = { 4, 4 }, .scalar_targets = 1u << 3 },
            { .name = "copy-stride17-u8", .roles = "vvn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { 1, 17 }, .access_size = { 1, 1 }, .scalar_targets = 1u << 3 },
            { .name = "store-stride9-u8", .roles = "vvn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { 9, 1 }, .access_size = { 1, 1 }, .scalar_targets = 1u << 3 },
            { .name = "store-stride17-u8", .roles = "vvn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { 17, 1 }, .access_size = { 1, 1 }, .scalar_targets = 1u << 3 },
            { .name = "copy-opposite-strides-i16", .roles = "vvn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { -18, 34 }, .access_size = { 2, 2 }, .scalar_targets = 1u << 3 },
            { .name = "aos-order-i32", .roles = "hn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { 16 }, .access_size = { 16 }, .vector_loads = 4 },
            { .name = "aos-partial-i32", .roles = "hn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { 16 }, .access_size = { 12 }, .vector_loads = 4 },
            { .name = "aos-mixed-width", .roles = "hn", .vectorized = { 1, 1, 0 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { 16 }, .access_size = { 8 }, .vector_loads = 8 },
            { .name = "strided-extension-domains", .roles = "ynj", .vectorized = { 1, 0, 0 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true, .faults = true },
            // Unary negation is an affine reverse index too. Its unsigned 32-bit extension wraps after the ninth
            // element; the vector guard must preserve the scalar fault after the initial valid accesses.
            { .name = "narrow-negation-wrap", .roles = "pnw", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true, .faults = true },
            // 2^31 + 1 elements per step requires qword indices. With i32 data, the lanes shrink until the index
            // vector fits; a 16-byte-only target has no legal shape. Both functions fault at their second element.
            { .name = "qword-index-i32", .roles = "pn", .vectorized = { 1, 1, 0 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true, .faults = true,
              .scalar_targets = (1u << 3) | (1u << 5) },
            { .name = "qword-index-reverse-i64", .roles = "pn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true, .faults = true,
              .scalar_targets = (1u << 3) | (1u << 5) },
            { .name = "strided-store-reload-i32", .roles = "vn", .vectorized = { 1, 1, 1 }, .refusal = IR_VECTORIZE_REFUSALS, .strided = true,
              .memory_bytes = BUSTER_IR_VECTORIZE_TEST_STRIDED_BUFFER, .access_step = { 8 }, .access_size = { 4 }, .scalar_targets = 1u << 3, .vector_loads = 4 },
            { .name = "refuse-overlapping-stride-i32", .roles = "ppn", .refusal = IR_VECTORIZE_REFUSE_ADDRESS },
        };
        for (uint32_t k = 0; ready && k < BUSTER_ARRAY_LENGTH(strided); k += 1)
        {
            ir_vectorize_test_fixture(&test, &strided[k], &source, &input, &vector, &cleanup, &scratch, &execution, buffers);
        }
        ir_vectorize_test_scans(&test);
        static const char* const mutants[BUSTER_IR_VECTORIZE_MUTATIONS] = {
            "none", "no overlap test", "an unrounded tail", "a reduction's identity", "the lanes' width", "a masked tail's lane more", "no 32-bit index wrap test", "a strided load's lane",
            "a reverse access's direction", "a sparse store's mask", "a shared load's member", "no shared load relation guard",
        };
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
