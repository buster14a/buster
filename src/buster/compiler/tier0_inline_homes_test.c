// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2841's loop-home guard, included only by c_check_tier0_inline_test.c. Generated typed streams establish both
// causes of home loss: more frequently referenced caller locals and registers consumed by deep expressions elsewhere.
// The checks exercise the public splice, its fallback and nested expansions; the production allocator supplies only
// the fixture's pressure preconditions, not a second implementation of the guard.

#define BUSTER_TIER0_INLINE_HOMES_TEST_ENTRIES 8192u
#define BUSTER_TIER0_INLINE_HOMES_TEST_LOCALS 96u
#define BUSTER_TIER0_INLINE_HOMES_TEST_CALLS 4u
#define BUSTER_TIER0_INLINE_HOMES_TEST_ARGUMENTS 16u

typedef struct Tier0InlineHomesTestFunction Tier0InlineHomesTestFunction;
struct Tier0InlineHomesTestFunction
{
    Tier0Function function;
    uint8_t kinds[BUSTER_TIER0_INLINE_HOMES_TEST_ENTRIES];
    uint8_t classes[BUSTER_TIER0_INLINE_HOMES_TEST_ENTRIES];
    uint64_t values[BUSTER_TIER0_INLINE_HOMES_TEST_ENTRIES];
    uint8_t local_classes[BUSTER_TIER0_INLINE_HOMES_TEST_LOCALS];
    uint8_t local_flags[BUSTER_TIER0_INLINE_HOMES_TEST_LOCALS];
    uint32_t local_sizes[BUSTER_TIER0_INLINE_HOMES_TEST_LOCALS];
    uint8_t local_alignments[BUSTER_TIER0_INLINE_HOMES_TEST_LOCALS];
    AbiLocation parameters[2];
    SymbolId targets[BUSTER_TIER0_INLINE_HOMES_TEST_CALLS];
    uint32_t first[BUSTER_TIER0_INLINE_HOMES_TEST_CALLS];
    uint32_t counts[BUSTER_TIER0_INLINE_HOMES_TEST_CALLS];
    uint8_t variadic[BUSTER_TIER0_INLINE_HOMES_TEST_CALLS];
    AbiCall calls[BUSTER_TIER0_INLINE_HOMES_TEST_CALLS];
    uint32_t objects[BUSTER_TIER0_INLINE_HOMES_TEST_CALLS];
    uint32_t callees[BUSTER_TIER0_INLINE_HOMES_TEST_CALLS];
    AbiLocation locations[BUSTER_TIER0_INLINE_HOMES_TEST_ARGUMENTS];
    uint8_t arguments[BUSTER_TIER0_INLINE_HOMES_TEST_ARGUMENTS];
};

static void tier0_inline_homes_test_begin(Tier0InlineHomesTestFunction* written, uint32_t locals, uint32_t parameters)
{
    memset(written, 0, sizeof(*written));
    assert(locals <= BUSTER_TIER0_INLINE_HOMES_TEST_LOCALS && parameters <= 2);
    memset(written->local_classes, BUSTER_TYPED_CLASS_U64, locals);
    for (uint32_t p = 0; p < parameters; p += 1)
    {
        written->parameters[p] = (AbiLocation){
            .registers = { p == 0 ? BUSTER_ABI_RDI : BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE },
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
        };
    }
    written->function = (Tier0Function){
        .kinds = written->kinds,
        .classes = written->classes,
        .values = written->values,
        .local_classes = written->local_classes,
        .local_flags = written->local_flags,
        .local_count = locals,
        .parameter_count = parameters,
        .parameters = written->parameters,
        .result = {
            .kind = BUSTER_ABI_RESULT_REGISTERS,
            .part_bytes = 8,
            .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
        },
        .call_targets = written->targets,
        .call_first = written->first,
        .call_argument_counts = written->counts,
        .call_variadic = written->variadic,
        .calls = written->calls,
        .argument_locations = written->locations,
        .argument_classes = written->arguments,
        .local_sizes = written->local_sizes,
        .local_alignments = written->local_alignments,
        .call_result_objects = written->objects,
    };
}

static uint32_t tier0_inline_homes_test_put(Tier0InlineHomesTestFunction* written, uint8_t kind, uint64_t value)
{
    const uint32_t entry = written->function.count;
    assert(entry < BUSTER_TIER0_INLINE_HOMES_TEST_ENTRIES);
    written->kinds[entry] = kind;
    written->classes[entry] = kind == BUSTER_TYPED_JUMP ? BUSTER_TYPED_CLASS_VOID :
                              (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE) ||
                              kind == BUSTER_TYPED_BRANCH_TRUE || kind == BUSTER_TYPED_BRANCH_FALSE ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U64;
    written->values[entry] = value;
    written->function.count = entry + 1;
    return entry;
}

// A sum loop, optionally returning its first word through a hidden pointer. With two scalar parameters the second
// controls the loop and the first starts the sum; its first binding is live across a nested call in argument two.
static void tier0_inline_homes_test_loop(Tier0InlineHomesTestFunction* written, bool memory, bool parameters)
{
    const uint32_t parameter_count = memory ? 1u : parameters ? 2u : 0u;
    const uint32_t counter = parameter_count;
    const uint32_t sum = counter + 1;
    tier0_inline_homes_test_begin(written, parameter_count + 2, parameter_count);
    if (memory)
    {
        written->function.result.kind = BUSTER_ABI_RESULT_MEMORY;
        written->function.result.part_bytes = 0;
        written->function.result.registers[0] = BUSTER_ABI_REGISTER_NONE;
    }
    tier0_inline_homes_test_put(written, BUSTER_TYPED_CONST, 0);
    tier0_inline_homes_test_put(written, BUSTER_TYPED_SET_LOCAL, counter);
    tier0_inline_homes_test_put(written, parameters ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_CONST, 0);
    tier0_inline_homes_test_put(written, BUSTER_TYPED_SET_LOCAL, sum);
    const uint32_t header = tier0_inline_homes_test_put(written, BUSTER_TYPED_LOCAL, counter);
    tier0_inline_homes_test_put(written, parameters ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_CONST, parameters ? 1 : 8);
    tier0_inline_homes_test_put(written, BUSTER_TYPED_LT, 0);
    const uint32_t exit = tier0_inline_homes_test_put(written, BUSTER_TYPED_BRANCH_FALSE, 0);
    tier0_inline_homes_test_put(written, BUSTER_TYPED_LOCAL, sum);
    tier0_inline_homes_test_put(written, BUSTER_TYPED_LOCAL, counter);
    tier0_inline_homes_test_put(written, BUSTER_TYPED_ADD, 0);
    tier0_inline_homes_test_put(written, BUSTER_TYPED_SET_LOCAL, sum);
    tier0_inline_homes_test_put(written, BUSTER_TYPED_LOCAL, counter);
    tier0_inline_homes_test_put(written, BUSTER_TYPED_CONST, 1);
    tier0_inline_homes_test_put(written, BUSTER_TYPED_ADD, 0);
    tier0_inline_homes_test_put(written, BUSTER_TYPED_SET_LOCAL, counter);
    tier0_inline_homes_test_put(written, BUSTER_TYPED_JUMP, header);
    written->values[exit] = written->function.count;
    if (memory)
    {
        tier0_inline_homes_test_put(written, BUSTER_TYPED_LOCAL, 0);
        tier0_inline_homes_test_put(written, BUSTER_TYPED_LOCAL, sum);
        tier0_inline_homes_test_put(written, BUSTER_TYPED_SET, 0);
    }
    tier0_inline_homes_test_put(written, BUSTER_TYPED_LOCAL, memory ? 0 : sum);
    tier0_inline_homes_test_put(written, BUSTER_TYPED_RETURN, 0);
}

// Append a call row and its CALL. The argument expressions have already been written.
static uint32_t tier0_inline_homes_test_call(Tier0InlineHomesTestFunction* caller, const Tier0Function* callee, uint32_t index)
{
    const uint32_t row = caller->function.call_count;
    const uint32_t first = caller->function.argument_count;
    const bool memory = callee->result.kind == BUSTER_ABI_RESULT_MEMORY;
    const uint32_t hidden = memory ? 1u : 0u;
    const uint32_t count = callee->parameter_count - hidden;
    assert(row < BUSTER_TIER0_INLINE_HOMES_TEST_CALLS && first + count <= BUSTER_TIER0_INLINE_HOMES_TEST_ARGUMENTS);
    caller->targets[row] = index + 1;
    caller->callees[row] = index + 1;
    caller->first[row] = first;
    caller->counts[row] = count;
    caller->calls[row] = (AbiCall){
        .stack_alignment_log2 = 4,
        .integer_registers = (uint8_t)callee->parameter_count,
        .result = callee->result,
    };
    for (uint32_t a = 0; a < count; a += 1)
    {
        caller->arguments[first + a] = BUSTER_TYPED_CLASS_U64;
        caller->locations[first + a] = callee->parameters[hidden + a];
    }
    if (memory)
    {
        const uint32_t object = caller->function.local_count;
        assert(object < BUSTER_TIER0_INLINE_HOMES_TEST_LOCALS);
        caller->objects[row] = object + 1;
        caller->local_classes[object] = BUSTER_TYPED_CLASS_VOID;
        caller->local_flags[object] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
        caller->local_sizes[object] = 24;
        caller->local_alignments[object] = 3;
        caller->function.local_count = object + 1;
    }
    caller->function.call_count = row + 1;
    caller->function.argument_count = first + count;
    const uint32_t entry = tier0_inline_homes_test_put(caller, BUSTER_TYPED_CALL, row);
    caller->classes[entry] = memory ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_U64;
    return row;
}

static uint32_t tier0_inline_homes_test_calls(const Tier0Function* function, SymbolId target)
{
    uint32_t calls = 0;
    for (uint32_t e = 0; e < function->count; e += 1)
    {
        if (function->kinds[e] == BUSTER_TYPED_CALL)
        {
            calls += function->call_targets[function->values[e]] == target ? 1u : 0u;
        }
    }
    return calls;
}

static void tier0_inline_homes_test_compile(Tier0InlineTestTally* tally, const Tier0Function* function, Arena* arena, uint32_t test)
{
    const bool valid = tier0_validate(function).error == TIER0_OK;
    tier0_inline_test_expect(tally, valid, "homes", test, "the selected stream validates");
    if (valid)
    {
        const uint64_t mark = arena->length;
        // Capture homes only after splicing: entry_records also affects forwarding when it belongs to the input.
        Tier0Function recorded = *function;
        recorded.entry_records = true;
        Tier0Function normalized;
        Tier0Job planned;
        const bool homes = tier0_inline_home_plan(&recorded, arena, &normalized, &planned);
        const Tier0Code code = tier0_compile(&recorded, arena);
        tier0_inline_test_expect(tally, code.error == TIER0_OK, "homes", test, "the selected stream compiles");
        tier0_inline_test_expect(tally, homes && code.error == TIER0_OK && code.local_count == function->local_count &&
                                  memcmp(planned.homes, code.local_homes, (uint64_t)function->local_count * sizeof(Tier0Home)) == 0,
                                  "homes", test, "the preflight homes equal the emitted function's actual homes");
        arena_pop_to(arena, mark);
    }
}

// Generate register competition without a hotness estimate: each caller local has 41 references, while each loop
// counter has five. The second shape instead reaches every general depth register before the loop call.
static void tier0_inline_homes_test_pressure(Tier0InlineHomesTestFunction* caller, uint32_t locals, bool deep)
{
    tier0_inline_homes_test_begin(caller, locals, 0);
    for (uint32_t l = 0; l < locals; l += 1)
    {
        tier0_inline_homes_test_put(caller, BUSTER_TYPED_CONST, l + 1);
        tier0_inline_homes_test_put(caller, BUSTER_TYPED_SET_LOCAL, l);
        for (uint32_t r = 0; r < 20; r += 1)
        {
            tier0_inline_homes_test_put(caller, BUSTER_TYPED_LOCAL, l);
            tier0_inline_homes_test_put(caller, BUSTER_TYPED_CONST, 1);
            tier0_inline_homes_test_put(caller, BUSTER_TYPED_ADD, 0);
            tier0_inline_homes_test_put(caller, BUSTER_TYPED_SET_LOCAL, l);
        }
    }
    if (deep)
    {
        for (uint32_t d = 0; d < 20; d += 1)
        {
            tier0_inline_homes_test_put(caller, BUSTER_TYPED_CONST, d + 1);
        }
        for (uint32_t d = 1; d < 20; d += 1)
        {
            tier0_inline_homes_test_put(caller, BUSTER_TYPED_ADD, 0);
        }
        tier0_inline_homes_test_put(caller, BUSTER_TYPED_DISCARD, 0);
    }
}

static void tier0_inline_homes_test_pressure_cases(Tier0InlineTestTally* tally, Arena* arena)
{
    Tier0InlineHomesTestFunction loop;
    Tier0InlineHomesTestFunction simple;
    Tier0InlineHomesTestFunction caller;
    tier0_inline_homes_test_begin(&simple, 0, 0);
    tier0_inline_homes_test_put(&simple, BUSTER_TYPED_CONST, 37);
    tier0_inline_homes_test_put(&simple, BUSTER_TYPED_RETURN, 0);
    static const uint32_t competitors[] = { 0, 20, 80, 0 };
    for (uint32_t memory = 0; memory < 2; memory += 1)
    {
        tier0_inline_homes_test_loop(&loop, memory != 0, false);
        for (uint32_t shape = 0; shape < BUSTER_ARRAY_LENGTH(competitors); shape += 1)
        {
            const uint64_t mark = arena->length;
            const uint32_t test = memory * 4 + shape;
            tier0_inline_homes_test_pressure(&caller, competitors[shape], shape == 3);
            tier0_inline_homes_test_call(&caller, &loop.function, 0);
            if (memory == 0)
            {
                tier0_inline_homes_test_put(&caller, BUSTER_TYPED_DISCARD, 0);
            }
            tier0_inline_homes_test_call(&caller, &simple.function, 1);
            tier0_inline_homes_test_put(&caller, BUSTER_TYPED_RETURN, 0);
            const Tier0Function* callees[] = { &loop.function, &simple.function };
            Tier0InlineInput input = {
                .caller = &caller.function,
                .row_callees = caller.callees,
                .callees = callees,
                .callee_count = BUSTER_ARRAY_LENGTH(callees),
                .shapes = true,
            };
            Tier0Inlined inlined;
            const bool spliced = tier0_inline(&input, arena, &inlined);
            const Tier0Function* selected = spliced ? &inlined.function : &caller.function;
            tier0_inline_test_expect(tally, spliced && tier0_inline_homes_test_calls(selected, 2) == 0, "homes", test,
                                      "straight-line wrappers remain inlined when a loop is kept outlined");
            tier0_inline_test_expect(tally, tier0_inline_homes_test_calls(selected, 1) == (shape != 0 ? 1u : 0u), "homes", test,
                                      "only the caller with enough homes splices the loop");
            tier0_inline_homes_test_compile(tally, selected, arena, test);
            if (memory == 0 && shape != 0)
            {
                input.shapes = false;
                Tier0Inlined unchecked;
                const bool made = tier0_inline(&input, arena, &unchecked);
                Tier0Function normalized;
                Tier0Job job;
                const bool planned = made && tier0_inline_home_plan(&unchecked.function, arena, &normalized, &job);
                tier0_inline_test_expect(tally, planned && job.references[caller.function.local_count] != 0 && !job.homes[caller.function.local_count].is_register,
                                          "homes", test, "the unguarded splice really demotes the callee's loop counter");
            }
            arena_pop_to(arena, mark);
        }
    }
}

static void tier0_inline_homes_test_forwarded(Tier0InlineTestTally* tally, Arena* arena)
{
    Tier0InlineHomesTestFunction loop;
    Tier0InlineHomesTestFunction caller;
    tier0_inline_homes_test_begin(&loop, 1, 0);
    tier0_inline_homes_test_put(&loop, BUSTER_TYPED_CONST, 1);
    tier0_inline_homes_test_put(&loop, BUSTER_TYPED_SET_LOCAL, 0);
    tier0_inline_homes_test_put(&loop, BUSTER_TYPED_LOCAL, 0);
    tier0_inline_homes_test_put(&loop, BUSTER_TYPED_DISCARD, 0);
    const uint32_t condition = tier0_inline_homes_test_put(&loop, BUSTER_TYPED_CONST, 0);
    loop.classes[condition] = BUSTER_TYPED_CLASS_I32;
    tier0_inline_homes_test_put(&loop, BUSTER_TYPED_BRANCH_TRUE, 0);
    tier0_inline_homes_test_put(&loop, BUSTER_TYPED_CONST, 17);
    tier0_inline_homes_test_put(&loop, BUSTER_TYPED_RETURN, 0);
    tier0_inline_homes_test_pressure(&caller, 0, true);
    tier0_inline_homes_test_call(&caller, &loop.function, 0);
    tier0_inline_homes_test_put(&caller, BUSTER_TYPED_RETURN, 0);
    const Tier0Function* callees[] = { &loop.function };
    const Tier0InlineInput input = {
        .caller = &caller.function,
        .row_callees = caller.callees,
        .callees = callees,
        .callee_count = BUSTER_ARRAY_LENGTH(callees),
        .shapes = true,
    };
    const uint64_t mark = arena->length;
    Tier0Function normalized;
    Tier0Job original;
    const bool planned = tier0_inline_home_plan(&loop.function, arena, &normalized, &original);
    uint32_t protected[BUSTER_TIER0_INLINE_REGISTER_HOMES];
    const uint32_t count = planned ? tier0_inline_loop_homes(&normalized, &original, protected) : 0;
    tier0_inline_test_expect(tally, planned && count == 1 && protected[0] == 0, "homes", 8, "the unforwarded loop has one register-homed temporary");
    Tier0Inlined inlined;
    const bool spliced = tier0_inline(&input, arena, &inlined);
    Tier0Job after;
    const bool replanned = spliced && tier0_inline_home_plan(&inlined.function, arena, &normalized, &after);
    tier0_inline_test_expect(tally, replanned && after.references[0] == 0 && tier0_inline_homes_test_calls(&inlined.function, 1) == 0, "homes", 8,
                              "a local removed by forwarding does not veto the splice even with every register occupied");
    if (spliced)
    {
        tier0_inline_homes_test_compile(tally, &inlined.function, arena, 8);
    }
    arena_pop_to(arena, mark);
}

static void tier0_inline_homes_test_levels(Tier0InlineTestTally* tally, Arena* arena)
{
    Tier0InlineHomesTestFunction loop;
    Tier0InlineHomesTestFunction caller;
    tier0_inline_homes_test_loop(&loop, false, true);
    bool deeper_only = false;
    for (uint32_t depth = 1; depth <= BUSTER_TIER0_INLINE_LEVELS; depth += 1)
    {
        const uint64_t mark = arena->length;
        tier0_inline_homes_test_begin(&caller, 0, 0);
        for (uint32_t level = 0; level < depth; level += 1)
        {
            tier0_inline_homes_test_put(&caller, BUSTER_TYPED_CONST, 3);
        }
        tier0_inline_homes_test_put(&caller, BUSTER_TYPED_CONST, 2);
        const uint32_t row = tier0_inline_homes_test_call(&caller, &loop.function, 0);
        for (uint32_t level = 1; level < depth; level += 1)
        {
            tier0_inline_homes_test_put(&caller, BUSTER_TYPED_CALL, row);
        }
        tier0_inline_homes_test_put(&caller, BUSTER_TYPED_RETURN, 0);
        const Tier0Function* callees[] = { &loop.function };
        Tier0InlineInput input = {
            .caller = &caller.function,
            .row_callees = caller.callees,
            .callees = callees,
            .callee_count = BUSTER_ARRAY_LENGTH(callees),
        };
        Tier0Inlined unchecked;
        const bool made = tier0_inline(&input, arena, &unchecked);
        Tier0Function normalized;
        Tier0Job job;
        const bool planned = made && tier0_inline_home_plan(&unchecked.function, arena, &normalized, &job);
        bool first_kept = planned;
        bool any_lost = false;
        for (uint32_t level = 0; planned && level < depth; level += 1)
        {
            // Parameter 1 is the bound; locals 2 and 3 are the counter and the sum. Parameter 0 is read before the loop.
            for (uint32_t local = 1; local < 4; local += 1)
            {
                const uint32_t index = 4 * level + local;
                const bool kept = job.references[index] == 0 || job.homes[index].is_register;
                first_kept &= level != 0 || kept;
                any_lost |= !kept;
            }
        }
        deeper_only |= first_kept && any_lost;
        input.shapes = true;
        Tier0Inlined guarded;
        const bool spliced = tier0_inline(&input, arena, &guarded);
        const Tier0Function* selected = spliced ? &guarded.function : &caller.function;
        tier0_inline_test_expect(tally, planned && tier0_inline_homes_test_calls(selected, 1) == (any_lost ? depth : 0), "homes", 8 + depth,
                                  "the guard checks every simultaneously bound nesting level");
        tier0_inline_homes_test_compile(tally, selected, arena, 8 + depth);
        arena_pop_to(arena, mark);
    }
    tier0_inline_test_expect(tally, deeper_only, "homes", 17, "a generated nesting keeps level zero's homes but loses a deeper level's");
}

static void tier0_inline_homes_test_nested(Tier0InlineTestTally* tally, Arena* arena)
{
    const uint64_t mark = arena->length;
    Tier0InlineHomesTestFunction memory;
    Tier0InlineHomesTestFunction wrapper;
    Tier0InlineHomesTestFunction caller;
    tier0_inline_homes_test_loop(&memory, true, false);
    tier0_inline_homes_test_begin(&wrapper, 0, 0);
    tier0_inline_homes_test_call(&wrapper, &memory.function, 0);
    tier0_inline_homes_test_put(&wrapper, BUSTER_TYPED_LOCAL_ADDRESS, 0);
    tier0_inline_homes_test_put(&wrapper, BUSTER_TYPED_LOAD, 0);
    tier0_inline_homes_test_put(&wrapper, BUSTER_TYPED_RETURN, 0);
    const Tier0Function* inner_callees[] = { &memory.function };
    const Tier0InlineInput inner = {
        .caller = &wrapper.function,
        .row_callees = wrapper.callees,
        .callees = inner_callees,
        .callee_count = BUSTER_ARRAY_LENGTH(inner_callees),
        .shapes = true,
    };
    Tier0Inlined expanded;
    const bool made = tier0_inline(&inner, arena, &expanded);
    tier0_inline_test_expect(tally, made && expanded.function.result.kind == BUSTER_ABI_RESULT_REGISTERS && tier0_inline_has_loop(&expanded.function), "homes", 18,
                              "the scalar wrapper safely expands its memory-result loop callee");
    if (made)
    {
        tier0_inline_homes_test_pressure(&caller, 20, false);
        tier0_inline_homes_test_call(&caller, &expanded.function, 0);
        tier0_inline_homes_test_put(&caller, BUSTER_TYPED_RETURN, 0);
        const Tier0Function* outer_callees[] = { &expanded.function };
        const Tier0InlineInput outer = {
            .caller = &caller.function,
            .row_callees = caller.callees,
            .callees = outer_callees,
            .callee_count = BUSTER_ARRAY_LENGTH(outer_callees),
            .shapes = true,
        };
        const uint64_t before = arena->length;
        Tier0Inlined inlined;
        const bool spliced = tier0_inline(&outer, arena, &inlined);
        tier0_inline_test_expect(tally, !spliced && arena->length == before, "homes", 18,
                                  "an already-expanded scalar callee cannot bypass the guard, and a refused splice releases its arena storage");
        tier0_inline_homes_test_compile(tally, spliced ? &inlined.function : &caller.function, arena, 18);
    }
    arena_pop_to(arena, mark);
}

// Unlike the generated streams' padded arrays, each scan column ends immediately before an unreadable page.
static void tier0_inline_homes_test_tails(Tier0InlineTestTally* tally)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, 4 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = mapping != MAP_FAILED;
    const bool ready = mapped && mprotect(mapping, page, PROT_READ | PROT_WRITE) == 0 &&
                       mprotect(mapping + 2 * page, page, PROT_READ | PROT_WRITE) == 0;
    tier0_inline_test_expect(tally, ready, "homes", 19, "guard pages are available for the scan tails");
    if (ready)
    {
        static const uint32_t counts[] = { 0, 1, 63, 64, 65, 127, 128, 129 };
        const uint8_t local_class = BUSTER_TYPED_CLASS_U64;
        Tier0Home home = { .is_register = 1 };
        uint32_t references = 1;
        const Tier0Job job = {
            .homes = &home,
            .references = &references,
        };
        for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(counts); c += 1)
        {
            const uint32_t count = counts[c];
            uint8_t* kinds = mapping + page - count;
            uint64_t* values = (uint64_t*)(mapping + 3 * page) - count;
            memset(kinds, BUSTER_TYPED_CONST, count);
            memset(values, 0, (uint64_t)count * sizeof(uint64_t));
            const Tier0Function function = {
                .kinds = kinds,
                .values = values,
                .count = count,
                .local_classes = &local_class,
                .local_count = 1,
            };
            uint32_t locals[BUSTER_TIER0_INLINE_REGISTER_HOMES];
            tier0_inline_test_expect(tally, !tier0_inline_has_loop(&function) && tier0_inline_loop_homes(&function, &job, locals) == 0,
                                      "homes", 19 + c, "masked scans do not invent a back edge beyond the last entry");
            if (count != 0)
            {
                kinds[count - 1] = BUSTER_TYPED_JUMP;
                values[count - 1] = count - 1;
                tier0_inline_test_expect(tally, tier0_inline_has_loop(&function) && tier0_inline_loop_homes(&function, &job, locals) == 0,
                                          "homes", 19 + c, "a last-entry self edge scans only its own range");
                if (count > 1)
                {
                    kinds[0] = BUSTER_TYPED_LOCAL;
                    values[count - 1] = 0;
                    tier0_inline_test_expect(tally, tier0_inline_has_loop(&function) && tier0_inline_loop_homes(&function, &job, locals) == 1 && locals[0] == 0,
                                              "homes", 19 + c, "a back edge spanning the guarded tail collects its register-homed local");
                }
            }
        }
    }
    if (mapped)
    {
        munmap(mapping, 4 * page);
    }
}

static void tier0_inline_test_homes(Tier0InlineTestTally* tally)
{
    Arena arena = arena_reserve(UINT64_C(1) << 28);
    const uint64_t checks = tally->checks;
    const uint64_t failures = tally->failures;
    tier0_inline_test_expect(tally, arena.base != NULL, "homes", 0, "the regression arena is available");
    const bool initialized = x86_encoder_initialize() && tier0_initialize();
    tier0_inline_test_expect(tally, initialized, "homes", 0, "the backend tables initialize before the home plans");
    if (arena.base != NULL && initialized)
    {
        tier0_inline_homes_test_pressure_cases(tally, &arena);
        tier0_inline_homes_test_forwarded(tally, &arena);
        tier0_inline_homes_test_levels(tally, &arena);
        tier0_inline_homes_test_nested(tally, &arena);
        tier0_inline_homes_test_tails(tally);
    }
    arena_release(&arena);
    printf("test-tier0-inline: loop homes: %" PRIu64 " checks, %" PRIu64 " failures\n", tally->checks - checks, tally->failures - failures);
}
