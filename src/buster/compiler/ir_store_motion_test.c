// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-ir-store-motion [--seed N] [--print NAME] (#3309): loop store motion's fixtures, tests/ir/store-motion/*.ir, one
// function each, read by both parser paths. The symbols are the test's: @1 and @2 objects of 8 bytes, @3 one of 64,
// @4 and @5 symbols the options don't list, @6 an object of 4 bytes, @7 one of 16, and @9 the callee (callee.ir).
// A fixture expects the loops and keys the pass moves, the loads and stores it gives a slot, and the outcome or the
// key refusal it is there for. Each result and its counts are folded into a frozen digest. Then the oracle, on every
// fixture the pass changed: the input, the result, the result promoted (ssa_ir.c, whose own verification takes it)
// and the promoted one optimized run in the shared interpreter on equal memory, with a pointer parameter at @1's
// bytes, in @3's or in a buffer of its own, at bounds from zero up, and must leave the same bytes, return the same
// value and end with the same status. The promoted result has no slot the input's promotion hasn't, and its loads are
// the input's less those the pass gave a slot, plus one a key. Every fault (BUSTER_IR_STORE_MOTION_MUTATE_*) must be
// rejected by the verification or the oracle on some fixture. The scans (the symbols' filter and the classification)
// are checked against their definitions on random columns at every length, and the entry point's contract with arenas
// of every short size. --print prints a fixture's result. Include after ir_store_motion.c, ir_optimize_text_test.c
// (its file reader and parse), ssa_ir.c and ir_interpreter.c.

#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)

#define BUSTER_IR_STORE_MOTION_TEST_SYMBOLS 8u
// Each symbol's bytes in an image (but @6's, which has its object's 4 alone), and the buffer after them.
#define BUSTER_IR_STORE_MOTION_TEST_OBJECT 64u
#define BUSTER_IR_STORE_MOTION_TEST_BUFFER 256u
#define BUSTER_IR_STORE_MOTION_TEST_IMAGE (BUSTER_IR_STORE_MOTION_TEST_SYMBOLS * BUSTER_IR_STORE_MOTION_TEST_OBJECT + BUSTER_IR_STORE_MOTION_TEST_BUFFER)
#define BUSTER_IR_STORE_MOTION_TEST_ENTRY 8u
#define BUSTER_IR_STORE_MOTION_TEST_CALLEE 9u
#define BUSTER_IR_STORE_MOTION_TEST_BUFFER_SYMBOL 10u
#define BUSTER_IR_STORE_MOTION_TEST_MUTANTS 6u
#define BUSTER_IR_STORE_MOTION_TEST_DIGEST UINT64_C(0x864d31ed771649b7)

// The options' list: the bytes of the test's distinct objects, by symbol.
static const uint32_t ir_store_motion_test_bytes[BUSTER_IR_STORE_MOTION_TEST_SYMBOLS] = { 0, 8, 8, 64, 0, 0, 4, 16 };

typedef struct IrStoreMotionTest IrStoreMotionTest;
struct IrStoreMotionTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
    bool killed[BUSTER_IR_STORE_MOTION_TEST_MUTANTS];
    const char* print;
    uint64_t digest;
    IrFunction callee;
};

typedef struct IrStoreMotionTestFixture IrStoreMotionTestFixture;
struct IrStoreMotionTestFixture
{
    const char* name;
    // A role per parameter: 'p' a pointer (at @1's bytes, in @3's, or in the buffer), 'n' the loop's bound, 'r' a count
    // of rows (0 to 3), 'k' a small scalar.
    const char* roles;
    // The loops it moves keys in, the keys, the loads that read a slot and the stores a slot takes too.
    uint32_t moved;
    uint32_t keys;
    uint32_t loads;
    uint32_t stores;
    // The outcome some loop of it has, or IR_STORE_MOTION_OUTCOME_COUNT; the refusal some key of it has, or
    // IR_STORE_MOTION_KEY_COUNT.
    uint32_t outcome;
    uint32_t refusal;
};

static const IrStoreMotionTestFixture ir_store_motion_test_fixtures[] = {
    { .name = "basic", .roles = "n", .moved = 1, .keys = 2, .loads = 3, .stores = 2, .outcome = IR_STORE_MOTION_MOVED, .refusal = IR_STORE_MOTION_KEY_MOVED },
    { .name = "conditional", .roles = "n", .moved = 1, .keys = 2, .loads = 3, .stores = 2, .outcome = IR_STORE_MOTION_MOVED, .refusal = IR_STORE_MOTION_KEY_MOVED },
    // The outer loop takes both keys, the inner loop's accesses with them; the inner loop then has none.
    { .name = "nested", .roles = "rn", .moved = 1, .keys = 2, .loads = 3, .stores = 2, .outcome = IR_STORE_MOTION_NO_KEY, .refusal = IR_STORE_MOTION_KEY_MOVED },
    // The outer loop calls; the inner one takes its key, from a preheader inside the outer loop.
    { .name = "nested-call", .roles = "rn", .moved = 1, .keys = 1, .loads = 1, .stores = 1, .outcome = IR_STORE_MOTION_BARRIER, .refusal = IR_STORE_MOTION_KEY_MOVED },
    { .name = "fields", .roles = "n", .moved = 1, .keys = 3, .loads = 4, .stores = 3, .outcome = IR_STORE_MOTION_MOVED, .refusal = IR_STORE_MOTION_KEY_MOVED },
    { .name = "types", .roles = "n", .moved = 1, .keys = 4, .loads = 4, .stores = 4, .outcome = IR_STORE_MOTION_MOVED, .refusal = IR_STORE_MOTION_KEY_LIMIT },
    { .name = "unknown-load", .roles = "pn", .moved = 1, .keys = 1, .loads = 1, .stores = 1, .outcome = IR_STORE_MOTION_MOVED, .refusal = IR_STORE_MOTION_KEY_MOVED },
    { .name = "two-exits", .roles = "nk", .moved = 1, .keys = 2, .loads = 4, .stores = 2, .outcome = IR_STORE_MOTION_MOVED, .refusal = IR_STORE_MOTION_KEY_MOVED },
    { .name = "do-while", .roles = "n", .moved = 1, .keys = 2, .loads = 3, .stores = 2, .outcome = IR_STORE_MOTION_MOVED, .refusal = IR_STORE_MOTION_KEY_MOVED },
    { .name = "key-limit", .roles = "n", .moved = 1, .keys = 4, .loads = 4, .stores = 4, .outcome = IR_STORE_MOTION_MOVED, .refusal = IR_STORE_MOTION_KEY_LIMIT },
    { .name = "slots", .roles = "n", .moved = 1, .keys = 2, .loads = 3, .stores = 2, .outcome = IR_STORE_MOTION_MOVED, .refusal = IR_STORE_MOTION_KEY_MOVED },
    { .name = "unread", .roles = "n", .moved = 1, .keys = 1, .loads = 1, .stores = 1, .outcome = IR_STORE_MOTION_MOVED, .refusal = IR_STORE_MOTION_KEY_UNREAD },
    { .name = "other-symbols", .roles = "n", .moved = 1, .keys = 1, .loads = 2, .stores = 1, .outcome = IR_STORE_MOTION_MOVED, .refusal = IR_STORE_MOTION_KEY_MOVED },
    // An indexed load of @3 and a load of half of @1 beside their keys: only a store that meets a key refuses it.
    { .name = "met-loads", .roles = "n", .moved = 1, .keys = 2, .loads = 2, .stores = 2, .outcome = IR_STORE_MOTION_MOVED, .refusal = IR_STORE_MOTION_KEY_MOVED },
    { .name = "refuse-call", .roles = "n", .outcome = IR_STORE_MOTION_BARRIER, .refusal = IR_STORE_MOTION_KEY_COUNT },
    { .name = "refuse-volatile", .roles = "n", .outcome = IR_STORE_MOTION_BARRIER, .refusal = IR_STORE_MOTION_KEY_COUNT },
    { .name = "refuse-fence", .roles = "n", .outcome = IR_STORE_MOTION_BARRIER, .refusal = IR_STORE_MOTION_KEY_COUNT },
    { .name = "refuse-unknown-store", .roles = "pn", .outcome = IR_STORE_MOTION_UNKNOWN_STORE, .refusal = IR_STORE_MOTION_KEY_COUNT },
    { .name = "refuse-indexed", .roles = "n", .outcome = IR_STORE_MOTION_NO_KEY, .refusal = IR_STORE_MOTION_KEY_MET },
    { .name = "refuse-overlap", .roles = "n", .outcome = IR_STORE_MOTION_NO_KEY, .refusal = IR_STORE_MOTION_KEY_MET },
    { .name = "refuse-bounds", .roles = "n", .outcome = IR_STORE_MOTION_NO_KEY, .refusal = IR_STORE_MOTION_KEY_BOUNDS },
    { .name = "refuse-preheader", .roles = "nk", .outcome = IR_STORE_MOTION_PREHEADER, .refusal = IR_STORE_MOTION_KEY_COUNT },
    { .name = "refuse-irreducible", .roles = "nk", .outcome = IR_STORE_MOTION_IRREDUCIBLE, .refusal = IR_STORE_MOTION_KEY_COUNT },
    { .name = "refuse-type", .roles = "n", .outcome = IR_STORE_MOTION_NO_KEY, .refusal = IR_STORE_MOTION_KEY_TYPE },
    // No listed symbol is named: the pass looks at no loop.
    { .name = "refuse-unlisted", .roles = "n", .outcome = IR_STORE_MOTION_OUTCOME_COUNT, .refusal = IR_STORE_MOTION_KEY_COUNT },
    { .name = "refuse-many", .roles = "n", .outcome = IR_STORE_MOTION_MANY, .refusal = IR_STORE_MOTION_KEY_COUNT },
    // An address past the walk's steps is unknown, and its store may be any key's.
    { .name = "refuse-chain", .roles = "n", .outcome = IR_STORE_MOTION_OUTCOME_COUNT, .refusal = IR_STORE_MOTION_KEY_COUNT },
};

static void ir_store_motion_test_expect(IrStoreMotionTest* test, bool condition, const char* name, const char* what)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        fprintf(stderr, "ir-store-motion: %s: %s\n", name, what);
    }
}

static uint64_t ir_store_motion_test_next(IrStoreMotionTest* test)
{
    uint64_t x = test->random;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    test->random = x;
    return x;
}

static IrStoreMotionOptions ir_store_motion_test_options(uint32_t mutate)
{
    IrStoreMotionOptions options = ir_store_motion_options();
    options.symbol_bytes = ir_store_motion_test_bytes;
    options.symbol_count = BUSTER_IR_STORE_MOTION_TEST_SYMBOLS;
    options.mutate = mutate;
    return options;
}

// One run of `function` on `image`: each symbol bound to its bytes there (@6 to its object's 4 alone, so that an access
// past them fails), the buffer after them, and the callee beside it.
static InterpreterResult ir_store_motion_test_run(const IrStoreMotionTest* test, const IrFunction* function, uint8_t* image, const InterpreterValue* arguments, uint32_t count, Arena* execution)
{
    InterpreterBinding bindings[BUSTER_IR_STORE_MOTION_TEST_SYMBOLS];
    for (uint32_t s = 1; s < BUSTER_IR_STORE_MOTION_TEST_SYMBOLS; s += 1)
    {
        bindings[s - 1] = (InterpreterBinding){ .symbol = s, .bytes = image + (uint64_t)(s - 1) * BUSTER_IR_STORE_MOTION_TEST_OBJECT, .size = s == 6 ? 4 : BUSTER_IR_STORE_MOTION_TEST_OBJECT, .writable = true };
    }
    bindings[BUSTER_IR_STORE_MOTION_TEST_SYMBOLS - 1] = (InterpreterBinding){
        .symbol = BUSTER_IR_STORE_MOTION_TEST_BUFFER_SYMBOL,
        .bytes = image + BUSTER_IR_STORE_MOTION_TEST_SYMBOLS * BUSTER_IR_STORE_MOTION_TEST_OBJECT,
        .size = BUSTER_IR_STORE_MOTION_TEST_BUFFER,
        .writable = true,
    };
    const IrInterpreterFunction functions[] = {
        { .symbol = BUSTER_IR_STORE_MOTION_TEST_ENTRY, .function = function },
        { .symbol = BUSTER_IR_STORE_MOTION_TEST_CALLEE, .function = &test->callee },
    };
    const IrInterpreterProgram program = { .functions = functions, .function_count = 2, .bindings = bindings, .binding_count = BUSTER_IR_STORE_MOTION_TEST_SYMBOLS };
    InterpreterOptions options = interpreter_default_options();
    options.max_steps = 2000000ull;
    const uint64_t mark = execution->length;
    const InterpreterResult result = ir_interpret(&program, BUSTER_IR_STORE_MOTION_TEST_ENTRY, arguments, count, &options, execution);
    arena_pop_to(execution, mark);
    return result;
}

// The oracle: `before` and `after` on equal images and arguments, 64 trials. A trial the input itself fails (an access
// past an object) says nothing, but that the result may not succeed. Returns whether they agreed on every other one.
static bool ir_store_motion_test_agree(IrStoreMotionTest* test, const IrStoreMotionTestFixture* fixture, const IrFunction* before, const IrFunction* after, Arena* execution, uint8_t* images)
{
    static const uint64_t bounds[] = { 0, 1, 2, 3, 5, 8, 13, 0 };
    uint8_t* initial = images;
    uint8_t* left = images + BUSTER_IR_STORE_MOTION_TEST_IMAGE;
    uint8_t* right = images + 2 * BUSTER_IR_STORE_MOTION_TEST_IMAGE;
    bool agree = true;
    for (uint32_t trial = 0; agree && trial < 64; trial += 1)
    {
        for (uint32_t k = 0; k < BUSTER_IR_STORE_MOTION_TEST_IMAGE; k += 8)
        {
            const uint64_t word = ir_store_motion_test_next(test);
            memcpy(initial + k, &word, 8);
        }
        // (A pointer parameter in @3 reaches its first 40 bytes at most: the pointer @3 holds at 48 isn't the images' alike.)
        InterpreterValue arguments[8] = { 0 };
        InterpreterValue moved[8] = { 0 };
        const uint32_t count = before->parameter_count;
        for (uint32_t k = 0; k < count && fixture->roles[k] != 0; k += 1)
        {
            const char role = fixture->roles[k];
            uint64_t value = ir_store_motion_test_next(test) % 64;
            if (role == 'p')
            {
                const uint64_t place = trial % 4 == 0 ? 0 :
                                       trial % 4 == 1 ? 2 * BUSTER_IR_STORE_MOTION_TEST_OBJECT + 8 * (value % 2) :
                                                        BUSTER_IR_STORE_MOTION_TEST_SYMBOLS * BUSTER_IR_STORE_MOTION_TEST_OBJECT + 8 * (value % 16);
                arguments[k].low = (uint64_t)(uintptr_t)(left + place);
                moved[k].low = (uint64_t)(uintptr_t)(right + place);
            }
            else
            {
                value = role == 'n' ? bounds[trial % BUSTER_ARRAY_LENGTH(bounds)] : role == 'r' ? trial % 4 : value;
                arguments[k].low = value & (before->parameter_types[k] == IR_I32 ? UINT32_MAX : UINT64_MAX);
                moved[k] = arguments[k];
            }
        }
        // @3's pointer at 48 (types.ir): a place in the buffer, of the image that runs.
        uint64_t held = (uint64_t)(uintptr_t)(left + BUSTER_IR_STORE_MOTION_TEST_SYMBOLS * BUSTER_IR_STORE_MOTION_TEST_OBJECT);
        memcpy(initial + 2 * BUSTER_IR_STORE_MOTION_TEST_OBJECT + 48, &held, 8);
        memcpy(left, initial, BUSTER_IR_STORE_MOTION_TEST_IMAGE);
        held = (uint64_t)(uintptr_t)(right + BUSTER_IR_STORE_MOTION_TEST_SYMBOLS * BUSTER_IR_STORE_MOTION_TEST_OBJECT);
        memcpy(initial + 2 * BUSTER_IR_STORE_MOTION_TEST_OBJECT + 48, &held, 8);
        memcpy(right, initial, BUSTER_IR_STORE_MOTION_TEST_IMAGE);
        const InterpreterResult a = ir_store_motion_test_run(test, before, left, arguments, count, execution);
        const InterpreterResult b = ir_store_motion_test_run(test, after, right, moved, count, execution);
        // The pointer @3 holds differs by the images' distance, as it did at the start.
        uint64_t pointers[2] = { 0, 0 };
        memcpy(&pointers[0], left + 2 * BUSTER_IR_STORE_MOTION_TEST_OBJECT + 48, 8);
        memcpy(&pointers[1], right + 2 * BUSTER_IR_STORE_MOTION_TEST_OBJECT + 48, 8);
        const bool stepped = pointers[0] - (uint64_t)(uintptr_t)left == pointers[1] - (uint64_t)(uintptr_t)right;
        memset(left + 2 * BUSTER_IR_STORE_MOTION_TEST_OBJECT + 48, 0, 8);
        memset(right + 2 * BUSTER_IR_STORE_MOTION_TEST_OBJECT + 48, 0, 8);
        const bool ran = a.status == INTERPRETER_OK;
        agree = ran ? b.status == INTERPRETER_OK && a.value.low == b.value.low && a.value.high == b.value.high && stepped && memcmp(left, right, BUSTER_IR_STORE_MOTION_TEST_IMAGE) == 0 :
                      b.status != INTERPRETER_OK;
    }
    return agree;
}

static uint32_t ir_store_motion_test_loads(const IrFunction* f)
{
    uint32_t loads = 0;
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        loads += f->op[i] == IR_LOAD;
    }
    return loads;
}

// One fixture: the pass, its counts, the digest, and the oracle on its result as it is, promoted and optimized; then
// the faults.
static void ir_store_motion_test_fixture(IrStoreMotionTest* test, const IrStoreMotionTestFixture* fixture, Arena* source, Arena* input, Arena* output, Arena* promotion, Arena* cleanup,
                                         Arena* scratch, Arena* execution, uint8_t* images)
{
    char path[256];
    char what[256];
    snprintf(path, sizeof(path), "tests/ir/store-motion/%s.ir", fixture->name);
    String8 text = { 0 };
    IrFunction function = { 0 };
    IrFunction twin = { 0 };
    const bool parsed = ir_optimize_text_read(path, source, &text) && ir_optimize_text_parse(text, input, execution, scratch, &function, &twin);
    arena_pop_to(execution, 0);
    ir_store_motion_test_expect(test, parsed, fixture->name, "the fixture is read and parsed alike by both parser paths");
    if (parsed)
    {
        const uint64_t om = output->length;
        const uint64_t sm = scratch->length;
        const IrStoreMotionOptions options = ir_store_motion_test_options(0);
        IrStoreMotionResult moved = { 0 };
        const IrCheck check = ir_store_motion(&function, &options, output, scratch, &moved);
        ir_store_motion_test_expect(test, check.error == IR_OK && scratch->length == sm, fixture->name, "the pass accepts it and restores scratch");
        const bool changed = check.error == IR_OK && moved.changed;
        if (check.error == IR_OK)
        {
            ir_optimize_test_fold_function(&test->digest, &moved.function);
            for (uint32_t k = 0; k < IR_STORE_MOTION_OUTCOME_COUNT; k += 1)
            {
                ir_optimize_test_fold(&test->digest, moved.outcomes[k]);
            }
            for (uint32_t k = 0; k < IR_STORE_MOTION_KEY_COUNT; k += 1)
            {
                ir_optimize_test_fold(&test->digest, moved.keys[k]);
            }
            ir_optimize_test_fold(&test->digest, moved.loads);
            ir_optimize_test_fold(&test->digest, moved.stores);
            ir_optimize_test_fold(&test->digest, moved.added);
        }
        snprintf(what, sizeof(what), "%u loops moved, %u keys, %u loads and %u stores; %u, %u, %u and %u expected", moved.outcomes[IR_STORE_MOTION_MOVED], moved.keys[IR_STORE_MOTION_KEY_MOVED],
                 moved.loads, moved.stores, fixture->moved, fixture->keys, fixture->loads, fixture->stores);
        ir_store_motion_test_expect(test, check.error == IR_OK && moved.outcomes[IR_STORE_MOTION_MOVED] == fixture->moved && moved.keys[IR_STORE_MOTION_KEY_MOVED] == fixture->keys &&
                                              moved.loads == fixture->loads && moved.stores == fixture->stores && changed == (fixture->keys != 0), fixture->name, what);
        uint32_t looked = 0;
        for (uint32_t k = 0; k < IR_STORE_MOTION_OUTCOME_COUNT; k += 1)
        {
            looked += moved.outcomes[k];
        }
        if (fixture->outcome != IR_STORE_MOTION_OUTCOME_COUNT)
        {
            snprintf(what, sizeof(what), "a loop of it has the outcome %s", ir_store_motion_outcome_names[fixture->outcome]);
            ir_store_motion_test_expect(test, check.error == IR_OK && moved.outcomes[fixture->outcome] != 0, fixture->name, what);
        }
        else
        {
            ir_store_motion_test_expect(test, check.error == IR_OK && looked == 0, fixture->name, "no loop of it is looked at");
        }
        if (fixture->refusal != IR_STORE_MOTION_KEY_COUNT)
        {
            snprintf(what, sizeof(what), "a key of it has the refusal %s", ir_store_motion_key_names[fixture->refusal]);
            ir_store_motion_test_expect(test, check.error == IR_OK && moved.keys[fixture->refusal] != 0, fixture->name, what);
        }
        // What it added: a load, a slot's address and a store a key, with the address's chain, and a store a taken store.
        ir_store_motion_test_expect(test, check.error != IR_OK || (moved.added >= 3 * moved.keys[IR_STORE_MOTION_KEY_MOVED] + moved.stores &&
                                                                    moved.function.instruction_count == function.instruction_count + moved.added &&
                                                                    moved.function.slot_count == function.slot_count + moved.keys[IR_STORE_MOTION_KEY_MOVED]),
                                    fixture->name, "its added instructions and slots are its keys' and its stores'");
        if (check.error == IR_OK && !changed)
        {
            ir_store_motion_test_expect(test, moved.function.op == function.op && moved.function.a == function.a && output->length == om, fixture->name, "an unchanged function is the input's, and nothing is written");
        }
        if (changed)
        {
            if (test->print != NULL && strcmp(test->print, fixture->name) == 0)
            {
                ir_print(&moved.function, scratch, stdout);
            }
            const IrCheck verified = ir_verify(&moved.function, scratch, NULL);
            snprintf(what, sizeof(what), "the result verifies (error %u at %u)", verified.error, verified.instruction);
            ir_store_motion_test_expect(test, verified.error == IR_OK, fixture->name, what);
            // Promotion of the result and of the input: the slots the pass made are gone, and the loads it gave them.
            IrFunction promoted = { 0 };
            IrFunction reference = { 0 };
            const IrCheck pc = ssa_ir_promote(&moved.function, promotion, scratch, &promoted);
            const IrCheck rc = ssa_ir_promote(&function, promotion, scratch, &reference);
            snprintf(what, sizeof(what), "promotion takes the result (error %u at %u)", pc.error, pc.instruction);
            ir_store_motion_test_expect(test, pc.error == IR_OK && rc.error == IR_OK, fixture->name, what);
            const bool both = pc.error == IR_OK && rc.error == IR_OK;
            snprintf(what, sizeof(what), "the promoted result has %u slots and %u loads; the input's %u and %u, %u keys and %u loads moved", promoted.slot_count, ir_store_motion_test_loads(&promoted),
                     reference.slot_count, ir_store_motion_test_loads(&reference), moved.keys[IR_STORE_MOTION_KEY_MOVED], moved.loads);
            ir_store_motion_test_expect(test, both && promoted.slot_count == reference.slot_count &&
                                                  ir_store_motion_test_loads(&promoted) + moved.loads == ir_store_motion_test_loads(&reference) + moved.keys[IR_STORE_MOTION_KEY_MOVED],
                                        fixture->name, what);
            IrOptimizeOptions optimize = ir_optimize_options();
            optimize.passes |= BUSTER_IR_OPTIMIZE_MERGE;
            IrOptimizeResult optimized = { 0 };
            const IrCheck oc = both ? ir_optimize(&promoted, cleanup, scratch, optimize, &optimized) : pc;
            snprintf(what, sizeof(what), "the optimizer takes the promoted result and verifies it with dominance (error %u)", oc.error);
            ir_store_motion_test_expect(test, oc.error == IR_OK, fixture->name, what);
            ir_store_motion_test_expect(test, ir_store_motion_test_agree(test, fixture, &function, &moved.function, execution, images), fixture->name, "the result runs as the input does");
            ir_store_motion_test_expect(test, both && ir_store_motion_test_agree(test, fixture, &function, &promoted, execution, images), fixture->name, "the promoted result runs as the input does");
            ir_store_motion_test_expect(test, oc.error == IR_OK && ir_store_motion_test_agree(test, fixture, &function, &optimized.function, execution, images), fixture->name,
                                        "the optimized result runs as the input does");
        }
        arena_pop_to(output, om);
        arena_pop_to(promotion, 0);
        arena_pop_to(cleanup, 0);
        // The faults: one the verifier or promotion refuses, or whose function runs otherwise, is killed.
        for (uint32_t mutant = 0; mutant < BUSTER_IR_STORE_MOTION_TEST_MUTANTS; mutant += 1)
        {
            const IrStoreMotionOptions faulty = ir_store_motion_test_options(1u << mutant);
            IrStoreMotionResult result = { 0 };
            const IrCheck mc = ir_store_motion(&function, &faulty, output, scratch, &result);
            bool killed = mc.error != IR_OK;
            if (mc.error == IR_OK && result.changed)
            {
                IrFunction promoted = { 0 };
                const IrCheck pc = ssa_ir_promote(&result.function, promotion, scratch, &promoted);
                killed = pc.error != IR_OK || !ir_store_motion_test_agree(test, fixture, &function, &promoted, execution, images);
            }
            test->killed[mutant] = test->killed[mutant] || killed;
            arena_pop_to(output, om);
            arena_pop_to(promotion, 0);
        }
    }
    arena_pop_to(input, 0);
    arena_pop_to(source, 0);
}

// The scans against their definitions, on random columns at every length to 200: the symbols' filter, with a list that
// has every other symbol, and the classification's lists of plain accesses and barriers.
static void ir_store_motion_test_scans(IrStoreMotionTest* test, Arena* scratch)
{
    IrOptimizeTest columns = { .random = test->random };
    IrOptimizeTestScan scan;
    static const uint32_t listed[6] = { 0, 8, 0, 8, 0, 0 };
    bool filtered = true;
    bool classified = true;
    uint32_t found = 0;
    uint32_t stopped = 0;
    for (uint32_t round = 0; round < 24; round += 1)
    {
        ir_optimize_test_scan_fill(&columns, &scan);
        IrFunction f = scan.function;
        // A third of the rounds name no listed symbol.
        for (uint32_t i = 0; round % 3 == 0 && i < BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS; i += 1)
        {
            scan.a[i] = scan.op[i] == IR_SYMBOL_ADDR ? 2 : scan.a[i];
        }
        for (uint32_t length = 0; length <= BUSTER_IR_OPTIMIZE_TEST_SCAN_ROWS; length += 1)
        {
            f.instruction_count = length;
            IrStoreMotionOptions options = ir_store_motion_options();
            options.symbol_bytes = listed;
            options.symbol_count = BUSTER_ARRAY_LENGTH(listed);
            bool expected = false;
            for (uint32_t i = 0; i < length; i += 1)
            {
                expected = expected || (f.op[i] == IR_SYMBOL_ADDR && f.a[i] < BUSTER_ARRAY_LENGTH(listed) && listed[f.a[i]] != 0);
            }
            filtered = filtered && ir_store_motion_symbols(&f, &options) == expected;
            found += expected;
            const uint64_t mark = scratch->length;
            IrStoreMotionWork work = { .function = &f, .options = &options };
            // A sentinel past each list: the compressing stores write the selected lanes alone.
            work.accesses = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)length + 1);
            work.barriers = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)length + 1);
            bool same = work.accesses != NULL && work.barriers != NULL;
            if (same)
            {
                for (uint32_t i = 0; i <= length; i += 1)
                {
                    work.accesses[i] = 0xa5a5a5a5u;
                    work.barriers[i] = 0xa5a5a5a5u;
                }
                ir_store_motion_classify(&work);
                uint32_t accesses = 0;
                uint32_t barriers = 0;
                for (uint32_t i = 0; i < length; i += 1)
                {
                    const uint32_t kind = ir_store_motion_kinds[f.op[i]];
                    const bool access = kind == BUSTER_IR_STORE_MOTION_LOAD || kind == BUSTER_IR_STORE_MOTION_STORE;
                    const bool observable = access && (f.flags[i] & BUSTER_IR_OBSERVABLE) != 0;
                    if (access && !observable)
                    {
                        same = same && accesses < work.access_count && work.accesses[accesses] == i;
                        accesses += 1;
                    }
                    else if (kind == BUSTER_IR_STORE_MOTION_BARRIER || observable)
                    {
                        same = same && barriers < work.barrier_count && work.barriers[barriers] == i;
                        barriers += 1;
                    }
                }
                same = same && accesses == work.access_count && barriers == work.barrier_count && work.accesses[accesses] == 0xa5a5a5a5u && work.barriers[barriers] == 0xa5a5a5a5u;
                stopped += barriers;
            }
            classified = classified && same;
            arena_pop_to(scratch, mark);
        }
    }
    ir_store_motion_test_expect(test, filtered && found != 0, "scans", "the symbols' filter, against its definition");
    ir_store_motion_test_expect(test, classified && stopped != 0, "scans", "the classification's lists, against their definition");
}

// The entry point's contract: one arena for both is refused; an output or a scratch too small for the work fails as
// memory's, with both arenas as they were; and where they have room the result is the full arenas' own.
static void ir_store_motion_test_contract(IrStoreMotionTest* test, Arena* source, Arena* input, Arena* output, Arena* scratch, Arena* execution)
{
    String8 text = { 0 };
    IrFunction function = { 0 };
    IrFunction twin = { 0 };
    const bool parsed = ir_optimize_text_read("tests/ir/store-motion/fields.ir", source, &text) && ir_optimize_text_parse(text, input, execution, scratch, &function, &twin);
    arena_pop_to(execution, 0);
    ir_store_motion_test_expect(test, parsed, "contract", "the fixture is read");
    if (parsed)
    {
        const IrStoreMotionOptions options = ir_store_motion_test_options(0);
        IrStoreMotionResult result = { 0 };
        const IrCheck shared = ir_store_motion(&function, &options, scratch, scratch, &result);
        ir_store_motion_test_expect(test, shared.error == IR_ERROR_STRUCTURE && scratch->length == 0, "contract", "one arena for the output and the scratch is refused");
        IrStoreMotionResult reference = { 0 };
        const IrCheck full = ir_store_motion(&function, &options, output, scratch, &reference);
        ir_store_motion_test_expect(test, full.error == IR_OK && reference.changed, "contract", "the fixture changes with room");
        uint64_t reference_digest = BUSTER_IR_OPTIMIZE_TEST_DIGEST_BASIS;
        ir_optimize_test_fold_function(&reference_digest, &reference.function);
        const uint64_t needs[2] = { output->length, 0 };
        bool failed_small = true;
        bool same = true;
        uint32_t failures = 0;
        uint32_t successes = 0;
        for (uint32_t which = 0; which < 2; which += 1)
        {
            // Sizes from nothing up, in steps, to past what the full run took of the output (or 64 KB of scratch).
            const uint64_t limit = which == 0 ? needs[0] + 4096 : UINT64_C(1) << 16;
            for (uint64_t size = 0; size <= limit; size += which == 0 ? 96 : 1024)
            {
                Arena small = arena_reserve_small(size == 0 ? 1 : size, size == 0 ? 1 : size);
                Arena* out = which == 0 ? &small : execution;
                Arena* work = which == 0 ? scratch : &small;
                const uint64_t out_mark = out->length;
                const uint64_t work_mark = work->length;
                IrStoreMotionResult small_result = { 0 };
                const IrCheck check = small.base != 0 ? ir_store_motion(&function, &options, out, work, &small_result) : ir_check(IR_ERROR_MEMORY, BUSTER_IR_NONE, 0);
                if (check.error == IR_OK)
                {
                    uint64_t digest = BUSTER_IR_OPTIMIZE_TEST_DIGEST_BASIS;
                    ir_optimize_test_fold_function(&digest, &small_result.function);
                    same = same && small_result.changed && digest == reference_digest && work->length == work_mark;
                    successes += 1;
                }
                else
                {
                    failed_small = failed_small && check.error == IR_ERROR_MEMORY && out->length == out_mark && work->length == work_mark;
                    failures += 1;
                }
                arena_pop_to(out, out_mark);
                arena_release(&small);
            }
        }
        ir_store_motion_test_expect(test, failed_small && failures != 0, "contract", "an arena too small fails as memory's and leaves both arenas as they were");
        ir_store_motion_test_expect(test, same && successes != 0, "contract", "an arena with room gives the full arenas' result");
    }
    arena_pop_to(output, 0);
    arena_pop_to(input, 0);
    arena_pop_to(source, 0);
}

static __attribute__((unused)) int run_ir_store_motion_tests(int argc, char** argv)
{
    IrStoreMotionTest test = {
        .random = 3309,
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
        Arena callee = arena_reserve(UINT64_C(1) << 24);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena promotion = arena_reserve(UINT64_C(1) << 26);
        Arena cleanup = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 28);
        Arena execution = arena_reserve(UINT64_C(1) << 28);
        uint8_t* images = calloc(3, BUSTER_IR_STORE_MOTION_TEST_IMAGE);
        const bool ready = source.base != 0 && input.base != 0 && callee.base != 0 && output.base != 0 && promotion.base != 0 && cleanup.base != 0 && scratch.base != 0 && execution.base != 0 &&
                           images != NULL;
        ir_store_motion_test_expect(&test, ready, "setup", "the arenas and images are reserved");
        String8 text = { 0 };
        IrFunction twin = { 0 };
        const bool called = ready && ir_optimize_text_read("tests/ir/store-motion/callee.ir", &source, &text) && ir_optimize_text_parse(text, &callee, &execution, &scratch, &test.callee, &twin);
        arena_pop_to(&execution, 0);
        arena_pop_to(&source, 0);
        ir_store_motion_test_expect(&test, called, "setup", "the callee is read");
        for (uint32_t k = 0; called && k < BUSTER_ARRAY_LENGTH(ir_store_motion_test_fixtures); k += 1)
        {
            ir_store_motion_test_fixture(&test, &ir_store_motion_test_fixtures[k], &source, &input, &output, &promotion, &cleanup, &scratch, &execution, images);
        }
        ir_store_motion_test_expect(&test, test.digest == BUSTER_IR_STORE_MOTION_TEST_DIGEST, "fixtures", "every result and its counts have their frozen digest");
        if (test.digest != BUSTER_IR_STORE_MOTION_TEST_DIGEST)
        {
            fprintf(stderr, "ir-store-motion: digest %#018" PRIx64 "\n", test.digest);
        }
        if (called)
        {
            ir_store_motion_test_scans(&test, &scratch);
            ir_store_motion_test_contract(&test, &source, &input, &output, &scratch, &execution);
        }
        static const char* const mutants[BUSTER_IR_STORE_MOTION_TEST_MUTANTS] = {
            "a barrier passed over",
            "an unknown store passed over",
            "a key beside an indexed access",
            "a key beside an overlapping one",
            "a store the slot doesn't get",
            "a key past its object's bytes",
        };
        for (uint32_t mutant = 0; called && mutant < BUSTER_IR_STORE_MOTION_TEST_MUTANTS; mutant += 1)
        {
            char what[128];
            snprintf(what, sizeof(what), "the oracle rejects the fault of %s on some fixture", mutants[mutant]);
            ir_store_motion_test_expect(&test, test.killed[mutant], "faults", what);
        }
        free(images);
        arena_release(&source);
        arena_release(&input);
        arena_release(&callee);
        arena_release(&output);
        arena_release(&promotion);
        arena_release(&cleanup);
        arena_release(&scratch);
        arena_release(&execution);
        printf("ir-store-motion: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
        result = test.failures == 0 ? 0 : 1;
    }
    else
    {
        fprintf(stderr, "usage: test-ir-store-motion [--seed N] [--print NAME]\n");
    }
    return result;
}

#endif
