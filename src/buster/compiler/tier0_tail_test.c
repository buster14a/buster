// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tail calls (TAIL_CALL entries, #147's musttail), on what the C programs of test-checker reach rarely: functions that
// pass their own parameters on to the aggregate probe (tier0_test_aggregate_probe) by a TAIL_CALL, one per fixture of
// tier0_test_passings in each of five frames, compiled by both paths into the same records, encoded by both encoders
// and run natively from a driver. The probe must find each parameter where its location says, the driver's slots past
// the call's arguments as the driver stored them, rsp where the driver's call left it and the callee-saved registers as
// the driver had them, which only a jump that released the whole frame leaves; what it returns must reach the driver,
// through the hidden pointer too. And tier0_validate's rules for tail calls, each broken once. tier0_test.c includes it
// after the aggregates' tests, whose helpers it uses, for its "tail errors" and "tail calls" checks.

// The frames: none but the parameters' homes; a frame pointer, rsp moved by an ALLOCA; a call first (of a function that
// clobbers every register a call may), with a local in a callee-saved register across it; an object aligned to 64, which
// realigns the frame; and both, a realigned frame whose ALLOCA needs a base (r15).
#define BUSTER_TIER0_TEST_TAIL_PLAIN 0
#define BUSTER_TIER0_TEST_TAIL_ALLOCA 1
#define BUSTER_TIER0_TEST_TAIL_SAVED 2
#define BUSTER_TIER0_TEST_TAIL_ALIGNED 3
#define BUSTER_TIER0_TEST_TAIL_BASE 4
#define BUSTER_TIER0_TEST_TAIL_SHAPES 5

static const char* const tier0_test_tail_shapes[BUSTER_TIER0_TEST_TAIL_SHAPES] = {
    "plain", "an ALLOCA", "saved registers", "an object aligned to 64", "aligned, with an ALLOCA",
};

#define BUSTER_TIER0_TEST_TAIL_ENTRIES 64

// A fixture's tail-calling function (tier0_test_tail_function): its columns, its parameters and the frame's local past
// them, and its calls table, row 0 the tail call and row 1 the first call of the saved registers' frame. `tail` is the
// TAIL_CALL's entry.
typedef struct Tier0TestTail Tier0TestTail;
struct Tier0TestTail
{
    uint8_t kinds[BUSTER_TIER0_TEST_TAIL_ENTRIES];
    uint8_t classes[BUSTER_TIER0_TEST_TAIL_ENTRIES];
    uint64_t values[BUSTER_TIER0_TEST_TAIL_ENTRIES];
    uint8_t local_classes[2 + BUSTER_TIER0_TEST_PASSED];
    uint8_t local_flags[2 + BUSTER_TIER0_TEST_PASSED];
    uint32_t local_sizes[2 + BUSTER_TIER0_TEST_PASSED];
    uint8_t local_alignments[2 + BUSTER_TIER0_TEST_PASSED];
    AbiLocation parameters[1 + BUSTER_TIER0_TEST_PASSED];
    AbiCall calls[2];
    SymbolId targets[2];
    uint32_t first[2];
    uint32_t argument_counts[2];
    uint8_t variadic[2];
    uint8_t tails[2];
    uint32_t result_objects[2];
    uint8_t argument_classes[BUSTER_TIER0_TEST_PASSED];
    uint32_t argument_sizes[BUSTER_TIER0_TEST_PASSED];
    AbiLocation locations[BUSTER_TIER0_TEST_PASSED];
    uint32_t tail;
    Tier0Function function;
};

static uint32_t tier0_test_tail_add(Tier0TestTail* tail, uint32_t count, uint8_t kind, uint8_t class, uint64_t value)
{
    tail->kinds[count] = kind;
    tail->classes[count] = class;
    tail->values[count] = value;
    return count + 1;
}

// The function: the fixture's parameters, as tier0_test_aggregate_callee has them (after the hidden pointer, for a
// result through one), and its result; what `shape` needs; then the probe's address (`probe`) and each parameter (an
// aggregate's address, a value) passed on by a TAIL_CALL at the parameters' own locations, its row's result the
// function's. `clobber` is the address of the function the saved registers' frame calls first (tier0_test_tail_clobber).
static void tier0_test_tail_function(Tier0TestTail* tail, const Tier0TestPassing* fixture, uint32_t shape, uint64_t probe, uint64_t clobber)
{
    memset(tail, 0, sizeof(*tail));
    const bool hidden = fixture->result.kind == BUSTER_ABI_RESULT_MEMORY;
    const uint32_t first = hidden ? 1 : 0;
    const uint32_t parameters = first + fixture->count;
    if (hidden)
    {
        tail->local_classes[0] = BUSTER_TYPED_CLASS_U64;
        tail->parameters[0] = (AbiLocation){ .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS };
    }
    for (uint32_t a = 0; a < fixture->count; a += 1)
    {
        const Tier0TestPassed* passed = &fixture->passed[a];
        const uint32_t local = first + a;
        const bool object = passed->size != 0;
        tail->local_classes[local] = object ? BUSTER_TYPED_CLASS_VOID : passed->class;
        tail->local_flags[local] = object ? BUSTER_TYPED_LOCAL_ADDRESS_TAKEN : 0;
        tail->local_sizes[local] = passed->size;
        tail->local_alignments[local] = object ? passed->alignment_log2 : 0;
        tail->parameters[local] = passed->location;
        tail->argument_classes[a] = passed->class;
        tail->argument_sizes[a] = passed->size;
        tail->locations[a] = passed->location;
    }
    // The frame's local past the parameters: the value that lives across the call, or the object aligned to 64.
    const uint32_t extra = parameters;
    const bool aligned = shape == BUSTER_TIER0_TEST_TAIL_ALIGNED || shape == BUSTER_TIER0_TEST_TAIL_BASE;
    const bool allocates = shape == BUSTER_TIER0_TEST_TAIL_ALLOCA || shape == BUSTER_TIER0_TEST_TAIL_BASE;
    const bool saved = shape == BUSTER_TIER0_TEST_TAIL_SAVED;
    if (saved)
    {
        tail->local_classes[extra] = BUSTER_TYPED_CLASS_I64;
    }
    if (aligned)
    {
        tail->local_classes[extra] = BUSTER_TYPED_CLASS_VOID;
        tail->local_flags[extra] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
        tail->local_sizes[extra] = 64;
        tail->local_alignments[extra] = 6;
    }
    uint32_t count = 0;
    if (aligned)
    {
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, extra);
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0x5a5a5a5a5a5a5a5aull);
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    if (allocates)
    {
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 40);
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_U64, 0);
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
    }
    if (saved)
    {
        // Referenced three times and live across a call: a callee-saved register's local, which the prologue pushes.
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 7);
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, extra);
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, clobber);
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 1);
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, extra);
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, extra);
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_I64, 0);
        count = tier0_test_tail_add(tail, count, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
    }
    count = tier0_test_tail_add(tail, count, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, probe);
    for (uint32_t a = 0; a < fixture->count; a += 1)
    {
        const Tier0TestPassed* passed = &fixture->passed[a];
        const bool object = passed->size != 0;
        count = tier0_test_tail_add(tail, count, object ? BUSTER_TYPED_LOCAL_ADDRESS : BUSTER_TYPED_LOCAL, object ? BUSTER_TYPED_CLASS_U64 : passed->class,
                                    first + a);
    }
    tail->tail = count;
    count = tier0_test_tail_add(tail, count, BUSTER_TYPED_TAIL_CALL, BUSTER_TYPED_CLASS_VOID, 0);
    tail->calls[0] = (AbiCall){ .stack_bytes = fixture->stack_bytes, .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .result = fixture->result };
    tail->calls[1] = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .result = BUSTER_TIER0_TEST_NO_RESULT };
    tail->first[1] = fixture->count;
    tail->argument_counts[0] = fixture->count;
    tail->tails[0] = 1;
    Tier0Function* function = &tail->function;
    function->kinds = tail->kinds;
    function->classes = tail->classes;
    function->values = tail->values;
    function->count = count;
    function->local_classes = tail->local_classes;
    function->local_flags = tail->local_flags;
    function->local_sizes = tail->local_sizes;
    function->local_alignments = tail->local_alignments;
    function->local_count = parameters + (saved || aligned ? 1 : 0);
    function->parameter_count = parameters;
    function->parameters = tail->parameters;
    function->result = fixture->result;
    function->result_size = hidden ? 0 : fixture->result_size;
    function->call_targets = tail->targets;
    function->call_first = tail->first;
    function->call_argument_counts = tail->argument_counts;
    function->call_variadic = tail->variadic;
    function->calls = tail->calls;
    function->argument_locations = tail->locations;
    function->argument_classes = tail->argument_classes;
    function->argument_sizes = tail->argument_sizes;
    function->call_result_objects = tail->result_objects;
    function->call_count = saved ? 2 : 1;
    function->argument_count = fixture->count;
    function->call_tails = tail->tails;
}

// A break of a fixture's tail-calling function (plain) that tier0_validate must refuse by `rule`, at the TAIL_CALL's
// entry, or for BUSTER_TIER0_TEST_METADATA's at entry 0, which both compile paths must refuse too.
typedef struct Tier0TestTailBreak Tier0TestTailBreak;
struct Tier0TestTailBreak
{
    const char* name;
    uint8_t fixture;
    const char* rule;
};

#define BUSTER_TIER0_TEST_TAIL_ROW "a tail call past the calls table, of a row that isn't a tail call's (call_tails), or with a class"
// The fixture of the variadic function's break, whose metadata is valid without the tail call.
#define BUSTER_TIER0_TEST_TAIL_VARIADIC 1

static const Tier0TestTailBreak tier0_test_tail_breaks[] = {
    { "a TAIL_CALL of a row that isn't a tail call's", 0, BUSTER_TIER0_TEST_TAIL_ROW },
    { "a TAIL_CALL without the tail calls' column", 0, BUSTER_TIER0_TEST_TAIL_ROW },
    { "a TAIL_CALL past the calls table", 0, BUSTER_TIER0_TEST_TAIL_ROW },
    { "a TAIL_CALL of class I64", 0, BUSTER_TIER0_TEST_TAIL_ROW },
    { "a CALL of a tail call's row", 0, "a CALL of a tail call's row (call_tails), which a TAIL_CALL makes" },
    { "a tail call with a value under its operands", 0, "a tail call with values left under its operands" },
    { "a variadic tail call", 0, BUSTER_TIER0_TEST_METADATA },
    { "a tail call whose result isn't the function's", 0, BUSTER_TIER0_TEST_METADATA },
    { "a tail call's stack arguments past the function's incoming ones", 0, BUSTER_TIER0_TEST_METADATA },
    { "a tail call with a result object", 3, BUSTER_TIER0_TEST_METADATA },
    { "a tail call of a variadic function", BUSTER_TIER0_TEST_TAIL_VARIADIC, BUSTER_TIER0_TEST_METADATA },
    { "a tail call's argument in rdi, which the hidden pointer takes", 12, BUSTER_TIER0_TEST_METADATA },
};

// The variadic function's own area: its named parameters' registers, and no stack arguments.
static AbiCall tier0_test_tail_incoming(void)
{
    return (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 4, .vector_registers = 8 };
}

// Applies break `b` to a fixture's function.
static void tier0_test_tail_break(uint32_t b, Tier0TestTail* tail, AbiCall* incoming)
{
    Tier0Function* function = &tail->function;
    switch (b)
    {
        case 0:
            tail->tails[0] = 0;
            break;
        case 1:
            function->call_tails = 0;
            break;
        case 2:
            tail->values[tail->tail] = function->call_count;
            break;
        case 3:
            tail->classes[tail->tail] = BUSTER_TYPED_CLASS_I64;
            break;
        case 4:
            tail->kinds[tail->tail] = BUSTER_TYPED_CALL;
            break;
        case 5:
            // A constant first, under every entry after it.
            for (uint32_t i = function->count; i > 0; i -= 1)
            {
                tail->kinds[i] = tail->kinds[i - 1];
                tail->classes[i] = tail->classes[i - 1];
                tail->values[i] = tail->values[i - 1];
            }
            tail->kinds[0] = BUSTER_TYPED_CONST;
            tail->classes[0] = BUSTER_TYPED_CLASS_I64;
            tail->values[0] = 1;
            function->count += 1;
            tail->tail += 1;
            break;
        case 6:
            tail->variadic[0] = 1;
            break;
        case 7:
            tail->calls[0].result = (AbiReturn)BUSTER_TIER0_TEST_RESULT(REGISTERS, 0, 8, BUSTER_ABI_RAX, BUSTER_TIER0_TEST_N);
            break;
        case 8:
            tail->calls[0].stack_bytes += BUSTER_ABI_SLOT_BYTES;
            break;
        case 9:
            tail->result_objects[0] = 1;
            break;
        case 10:
            *incoming = tier0_test_tail_incoming();
            function->incoming_variadic = incoming;
            break;
        case 11:
            tail->locations[0].registers[0] = BUSTER_ABI_RDI;
            break;
        default:
            break;
    }
}

// Each break of a plain tail-calling function, built afresh: tier0_validate must refuse it by its rule, at its place,
// and for the metadata both compile paths must refuse it too; and the variadic function's metadata must be valid
// without the tail call, so that its refusal is the tail call's.
static void tier0_test_tail_errors(Tier0TestTally* tally)
{
    static const char check[] = "tail errors";
    Arena arena = arena_reserve(1ull << 24);
    for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(tier0_test_tail_breaks) && arena.base != 0; b += 1)
    {
        const Tier0TestTailBreak* broken = &tier0_test_tail_breaks[b];
        Tier0TestTail tail;
        AbiCall incoming;
        tier0_test_tail_function(&tail, &tier0_test_passings[broken->fixture], BUSTER_TIER0_TEST_TAIL_PLAIN, 0x1000, 0x2000);
        const Tier0Check before = tier0_validate(&tail.function);
        tier0_test_tail_break(b, &tail, &incoming);
        const Tier0Check after = tier0_validate(&tail.function);
        const bool metadata = strcmp(broken->rule, BUSTER_TIER0_TEST_METADATA) == 0;
        const Tier0Code scalar = metadata ? tier0_compile_scalar(&tail.function, &arena) : (Tier0Code){ .error = TIER0_ERROR_UNSUPPORTED };
        const Tier0Code vector = metadata ? tier0_compile(&tail.function, &arena) : (Tier0Code){ .error = TIER0_ERROR_UNSUPPORTED };
        const bool refused = after.error == TIER0_ERROR_INVALID && after.rule != 0 && strcmp(after.rule, broken->rule) == 0 && after.node == (metadata ? 0 : tail.tail);
        tier0_test_expect(tally, before.error == TIER0_OK && refused && scalar.error == TIER0_ERROR_UNSUPPORTED && vector.error == TIER0_ERROR_UNSUPPORTED, check,
                          "%s: before it, \"%s\"; after it, \"%s\" at %u, and the paths' errors %d and %d", broken->name, before.rule != 0 ? before.rule : "valid",
                          after.rule != 0 ? after.rule : "valid", after.node, scalar.error, vector.error);
        arena_pop_to(&arena, 0);
    }
    Tier0TestTail control;
    tier0_test_tail_function(&control, &tier0_test_passings[BUSTER_TIER0_TEST_TAIL_VARIADIC], BUSTER_TIER0_TEST_TAIL_PLAIN, 0x1000, 0x2000);
    const AbiCall incoming = tier0_test_tail_incoming();
    control.kinds[0] = BUSTER_TYPED_RETURN;
    control.classes[0] = BUSTER_TYPED_CLASS_VOID;
    control.function.count = 1;
    control.tails[0] = 0;
    control.function.incoming_variadic = &incoming;
    const Tier0Check valid = tier0_validate(&control.function);
    tier0_test_expect(tally, valid.error == TIER0_OK, check, "the variadic function without its tail call: \"%s\" at %u", valid.rule != 0 ? valid.rule : "valid",
                      valid.node);
    arena_release(&arena);
}

// The callee-saved registers (rbx, rbp and r12 to r15) and rsp, as the driver had them at its entry and the probe at
// its.
#define BUSTER_TIER0_TEST_TAIL_SAVED_REGISTERS 6

typedef struct Tier0TestTailRegisters Tier0TestTailRegisters;
struct Tier0TestTailRegisters
{
    uint64_t saved[BUSTER_TIER0_TEST_TAIL_SAVED_REGISTERS];
    uint64_t rsp;
};

static const uint8_t tier0_test_tail_saved[BUSTER_TIER0_TEST_TAIL_SAVED_REGISTERS] = {
    X86_REGISTER_RBX, X86_REGISTER_RBP, X86_REGISTER_R12, X86_REGISTER_R13, X86_REGISTER_R14, X86_REGISTER_R15,
};

// A run of a function's code by each encoder.
#define BUSTER_TIER0_TEST_TAIL_RUNS 2

// What the driver passes (`arguments`) and the probe returns (`returned`); what the probe saw (`seen`) and the driver's
// call returned (`returns`); the registers at the driver's entry and at the probe's: the run in progress's ([0]),
// copied to its own after it; and the bytes the clobbering function loads into the vector registers.
typedef struct Tier0TestTailShared Tier0TestTailShared;
struct Tier0TestTailShared
{
    Tier0TestPreset arguments;
    Tier0TestPreset returned;
    Tier0TestSeen seen[1 + BUSTER_TIER0_TEST_TAIL_RUNS];
    Tier0TestSeen returns[1 + BUSTER_TIER0_TEST_TAIL_RUNS];
    Tier0TestTailRegisters entry[1 + BUSTER_TIER0_TEST_TAIL_RUNS];
    Tier0TestTailRegisters probe[1 + BUSTER_TIER0_TEST_TAIL_RUNS];
    uint8_t garbage[16];
};

// The pages of a function's runs: its code by each encoder, a driver of each, the probe and the clobbering function.
#define BUSTER_TIER0_TEST_TAIL_DRIVERS 2
#define BUSTER_TIER0_TEST_TAIL_PROBE 4
#define BUSTER_TIER0_TEST_TAIL_CLOBBER 5
#define BUSTER_TIER0_TEST_TAIL_PAGES 6

// Records that store the callee-saved registers and rsp into the Tier0TestTailRegisters at `address` (through r10, which
// the driver and the probe after them set again). Returns the count.
static uint32_t tier0_test_tail_registers(X86Instruction* records, uint64_t address)
{
    records[0] = tier0_instruction(X86_FORM_MOV_R64_IMM64, 0, X86_REGISTER_R10, 0, (int32_t)(address >> 32), (int32_t)(uint32_t)address);
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_TAIL_SAVED_REGISTERS; k += 1)
    {
        records[1 + k] = tier0_instruction(X86_FORM_MOV_RM64_R64, tier0_test_tail_saved[k], X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)(8 * k), 0);
    }
    records[1 + BUSTER_TIER0_TEST_TAIL_SAVED_REGISTERS] =
        tier0_instruction(X86_FORM_MOV_RM64_R64, X86_REGISTER_RSP, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, (int32_t)offsetof(Tier0TestTailRegisters, rsp), 0);
    return 2 + BUSTER_TIER0_TEST_TAIL_SAVED_REGISTERS;
}

// The general registers a call may change, but rsp.
static const uint8_t tier0_test_tail_clobbered[] = {
    X86_REGISTER_RAX, X86_REGISTER_RCX, X86_REGISTER_RDX, X86_REGISTER_RSI, X86_REGISTER_RDI,
    X86_REGISTER_R8,  X86_REGISTER_R9,  X86_REGISTER_R10, X86_REGISTER_R11,
};

// The function that the saved registers' frame calls first: garbage, the 16 bytes at `garbage`, in xmm0 to xmm15, and
// garbage of its own in every general register a call may change, then a ret. So the function passes on what its
// homes hold, not what is left where its parameters arrived. Returns the count.
static uint32_t tier0_test_tail_clobber(X86Instruction* records, uint64_t garbage)
{
    uint32_t count = 0;
    records[count] = tier0_instruction(X86_FORM_MOV_R64_IMM64, 0, X86_REGISTER_R10, 0, (int32_t)(garbage >> 32), (int32_t)(uint32_t)garbage);
    count += 1;
    for (uint32_t v = 0; v < 16; v += 1)
    {
        records[count] = tier0_instruction(X86_FORM_MOVUPS_X_XM, (uint8_t)v, X86_REGISTER_R10, BUSTER_X86_MEMORY_BASE, 0, 0);
        count += 1;
    }
    for (uint32_t g = 0; g < BUSTER_ARRAY_LENGTH(tier0_test_tail_clobbered); g += 1)
    {
        records[count] = tier0_instruction(X86_FORM_MOV_R64_IMM64, 0, tier0_test_tail_clobbered[g], 0, (int32_t)0xdeadbeef, (int32_t)(0x5a5a5a00 + g));
        count += 1;
    }
    records[count] = tier0_instruction(X86_FORM_RET, 0, 0, 0, 0, 0);
    return count + 1;
}

// The pieces a passed argument's location passes it in: a value's 8 bytes, an aggregate's slots whole, or its parts,
// each its bytes' offset and count and its register, or BUSTER_TIER0_PART_STACK for the location's stack offset.
// Returns the count.
static uint32_t tier0_test_tail_pieces(const Tier0TestPassed* passed, Tier0Part* pieces)
{
    const AbiLocation location = passed->location;
    uint32_t count = 1;
    if (passed->size == 0)
    {
        const bool stack = location.kind == BUSTER_ABI_LOCATION_STACK;
        pieces[0] = (Tier0Part){ .reg = stack ? BUSTER_TIER0_PART_STACK : tier0_abi_register(location.registers[0]), .offset = 0, .bytes = 8 };
    }
    else if (tier0_aggregate_whole(location))
    {
        pieces[0] = (Tier0Part){ .reg = BUSTER_TIER0_PART_STACK, .offset = 0, .bytes = (uint8_t)passed->size };
    }
    else
    {
        count = tier0_location_parts(location, passed->size, pieces);
    }
    return count;
}

// Runs the drivers in a child, each from the run in progress's records poisoned, which are then copied to its own run's.
// Returns whether the child exited with status 0.
static __attribute__((no_sanitize("function"))) bool tier0_test_tail_run(Tier0TestTailShared* shared, uint8_t* code)
{
    const pid_t child = fork();
    if (child == 0)
    {
        alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
        for (uint32_t r = 0; r < BUSTER_TIER0_TEST_TAIL_RUNS; r += 1)
        {
            typedef void (*Tier0TestEntryPoint)(void);
            Tier0TestEntryPoint entry = 0;
            void* address = code + (BUSTER_TIER0_TEST_TAIL_DRIVERS + r) * BUSTER_TIER0_TEST_PAGE;
            memcpy(&entry, &address, sizeof(entry));
            memset(&shared->seen[0], 0xa5, sizeof(shared->seen[0]));
            memset(&shared->returns[0], 0xa5, sizeof(shared->returns[0]));
            memset(&shared->entry[0], 0xa5, sizeof(shared->entry[0]));
            memset(&shared->probe[0], 0xa5, sizeof(shared->probe[0]));
            entry();
            shared->seen[1 + r] = shared->seen[0];
            shared->returns[1 + r] = shared->returns[0];
            shared->entry[1 + r] = shared->entry[0];
            shared->probe[1 + r] = shared->probe[0];
        }
        _exit(0);
    }
    int status = 0;
    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
    return waited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Each fixture's function in each frame, by both paths: a driver passes the fixture's bytes as its locations say (each
// register's and each slot's other bytes their own), and the function passes them on to the probe by a tail call.
static void tier0_test_tail_calls(Tier0TestTally* tally)
{
    static const char check[] = "tail calls";
    Tier0TestTailShared* shared = mmap(0, sizeof(Tier0TestTailShared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    const uint64_t pages = BUSTER_TIER0_TEST_TAIL_PAGES * BUSTER_TIER0_TEST_PAGE;
    uint8_t* code = mmap(0, pages, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Arena arena = arena_reserve(1ull << 26);
    bool ready = tier0_test_expect(tally, shared != MAP_FAILED && code != MAP_FAILED && arena.base != 0, check, "no memory");
    // The driver's frame (tier0_test_aggregate_driver's, of every slot) and its call's return address, which the probe
    // finds rsp below the driver's entry by: the function's whole frame released.
    const uint64_t below = (8 * BUSTER_TIER0_TEST_STACK_WORDS + 15) / 16 * 16 + 8 + 8;
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(tier0_test_passings) * BUSTER_TIER0_TEST_TAIL_SHAPES && ready; f += 1)
    {
        const Tier0TestPassing* fixture = &tier0_test_passings[f / BUSTER_TIER0_TEST_TAIL_SHAPES];
        const uint32_t shape = f % BUSTER_TIER0_TEST_TAIL_SHAPES;
        const char* frame = tier0_test_tail_shapes[shape];
        const bool hidden = fixture->result.kind == BUSTER_ABI_RESULT_MEMORY;
        arena_pop_to(&arena, 0);
        ready = mprotect(code, pages, PROT_READ | PROT_WRITE) == 0;
        // What the driver passes: every byte its own, then each parameter's bytes where its location says.
        uint8_t bytes[BUSTER_TIER0_TEST_PASSED][64];
        memset(&shared->arguments, 0, sizeof(shared->arguments));
        for (uint32_t i = 0; i < sizeof(shared->arguments.general) + 16 + sizeof(shared->arguments.vectors) + sizeof(shared->arguments.stack); i += 1)
        {
            ((uint8_t*)&shared->arguments)[i] = (uint8_t)(0x5c ^ (3 * i));
        }
        for (uint32_t a = 0; a < fixture->count; a += 1)
        {
            const Tier0TestPassed* passed = &fixture->passed[a];
            const uint64_t bits = tier0_test_passed_value(passed, a);
            memcpy(bytes[a], &bits, sizeof(bits));
            for (uint32_t i = 0; i < passed->size; i += 1)
            {
                bytes[a][i] = tier0_test_passed_byte(a, i, 0);
            }
            Tier0Part pieces[2];
            const uint32_t piece_count = tier0_test_tail_pieces(passed, pieces);
            for (uint32_t k = 0; k < piece_count; k += 1)
            {
                uint8_t* place = pieces[k].reg == BUSTER_TIER0_PART_STACK ? shared->arguments.stack + passed->location.stack_offset
                                                                         : tier0_test_preset_register(&shared->arguments, pieces[k].reg, false);
                memcpy(place, bytes[a] + pieces[k].offset, pieces[k].bytes);
            }
        }
        if (hidden)
        {
            shared->arguments.general[0] = (uint64_t)(uintptr_t)shared->seen[0].hidden;
        }
        // What the probe returns: registers of bytes of their own, and for a hidden result its bytes; and the garbage the
        // clobbering function leaves in the vector registers.
        memset(shared->garbage, 0xcc, sizeof(shared->garbage));
        memset(&shared->returned, 0, sizeof(shared->returned));
        shared->returned.rax = 0xa1a2a3a4a5a6a7a8ull;
        shared->returned.rdx = 0xb1b2b3b4b5b6b7b8ull;
        for (uint32_t i = 0; i < 16; i += 1)
        {
            shared->returned.vectors[0][i] = (uint8_t)(0xc0 + i);
            shared->returned.vectors[1][i] = (uint8_t)(0xd0 + i);
        }
        shared->returned.hidden_flag = hidden ? 1 : 0;
        for (uint32_t i = 0; i < BUSTER_TIER0_TEST_HIDDEN_BYTES; i += 1)
        {
            shared->returned.hidden[i] = (uint8_t)(0xe0 ^ (7 * i));
        }
        // The function, by both paths; its frame as its shape makes it, and its jump to the probe.
        Tier0TestTail tail;
        tier0_test_tail_function(&tail, fixture, shape, (uint64_t)(uintptr_t)(code + BUSTER_TIER0_TEST_TAIL_PROBE * BUSTER_TIER0_TEST_PAGE),
                                 (uint64_t)(uintptr_t)(code + BUSTER_TIER0_TEST_TAIL_CLOBBER * BUSTER_TIER0_TEST_PAGE));
        ready = ready && tier0_test_aggregate_encode(tally, check, fixture->name, &tail.function, &arena, code);
        const Tier0Code compiled = ready ? tier0_compile(&tail.function, &arena) : (Tier0Code){ .error = TIER0_ERROR_UNSUPPORTED };
        const bool jumps = compiled.error == TIER0_OK && compiled.tail_count == 1 && compiled.tail_records[0] < compiled.count &&
                           compiled.records[compiled.tail_records[0]].form == X86_FORM_JMP_RM64 && compiled.records[compiled.tail_records[0]].rm == X86_REGISTER_R11;
        const bool framed = shape == BUSTER_TIER0_TEST_TAIL_PLAIN ? !compiled.frame_pointer
                          : shape == BUSTER_TIER0_TEST_TAIL_ALLOCA ? compiled.frame_pointer && compiled.frame_alignment_log2 == 0
                          : shape == BUSTER_TIER0_TEST_TAIL_SAVED ? compiled.frame_saved != 0
                          : shape == BUSTER_TIER0_TEST_TAIL_ALIGNED ? compiled.frame_pointer && compiled.frame_alignment_log2 == 6 && !compiled.frame_base
                                                                    : compiled.frame_alignment_log2 == 6 && compiled.frame_base;
        ready = ready && tier0_test_expect(tally, jumps && framed, check, "%s, %s: no tail record of a jump through r11, or not the frame its shape makes",
                                           fixture->name, frame);
        for (uint32_t r = 0; r < BUSTER_TIER0_TEST_TAIL_RUNS && ready; r += 1)
        {
            X86Instruction driver[128];
            uint32_t driver_count = tier0_test_tail_registers(driver, (uint64_t)(uintptr_t)&shared->entry[0]);
            driver_count += tier0_test_aggregate_driver(driver + driver_count, (uint64_t)(uintptr_t)&shared->arguments, (uint64_t)(uintptr_t)&shared->returns[0],
                                                        (uint64_t)(uintptr_t)(code + r * BUSTER_TIER0_TEST_PAGE), BUSTER_TIER0_TEST_STACK_WORDS);
            ready = tier0_test_expect(tally, tier0_test_aggregate_load(driver, driver_count, code + (BUSTER_TIER0_TEST_TAIL_DRIVERS + r) * BUSTER_TIER0_TEST_PAGE),
                                      check, "%s, %s: the driver can't be loaded", fixture->name, frame);
        }
        X86Instruction probe[128];
        uint32_t probe_count = tier0_test_tail_registers(probe, (uint64_t)(uintptr_t)&shared->probe[0]);
        probe_count += tier0_test_aggregate_probe(probe + probe_count, (uint64_t)(uintptr_t)&shared->seen[0], (uint64_t)(uintptr_t)&shared->returned,
                                                  hidden ? fixture->result_size : 0);
        X86Instruction clobber[32];
        const uint32_t clobber_count = tier0_test_tail_clobber(clobber, (uint64_t)(uintptr_t)shared->garbage);
        ready = ready && tier0_test_expect(tally, tier0_test_aggregate_load(probe, probe_count, code + BUSTER_TIER0_TEST_TAIL_PROBE * BUSTER_TIER0_TEST_PAGE) &&
                                                      tier0_test_aggregate_load(clobber, clobber_count, code + BUSTER_TIER0_TEST_TAIL_CLOBBER * BUSTER_TIER0_TEST_PAGE) &&
                                                      mprotect(code, pages, PROT_READ | PROT_EXEC) == 0,
                                           check, "%s, %s: the probe can't be loaded", fixture->name, frame);
        ready = ready && tier0_test_expect(tally, tier0_test_tail_run(shared, code), check, "%s, %s: the child failed", fixture->name, frame);
        for (uint32_t r = 0; r < BUSTER_TIER0_TEST_TAIL_RUNS && ready; r += 1)
        {
            Tier0TestSeen* seen = &shared->seen[1 + r];
            for (uint32_t a = 0; a < fixture->count; a += 1)
            {
                const Tier0TestPassed* passed = &fixture->passed[a];
                Tier0Part pieces[2];
                const uint32_t piece_count = tier0_test_tail_pieces(passed, pieces);
                bool same = true;
                for (uint32_t k = 0; k < piece_count; k += 1)
                {
                    const uint8_t* recorded = pieces[k].reg == BUSTER_TIER0_PART_STACK ? seen->stack + passed->location.stack_offset
                                                                                      : tier0_test_seen_register(seen, pieces[k].reg, false);
                    same &= tier0_test_same_bytes(recorded, bytes[a] + pieces[k].offset, pieces[k].bytes);
                }
                tier0_test_expect(tally, same, check, "%s, %s, run %u: argument %u isn't where its location says", fixture->name, frame, r, a);
            }
            // The slots past the arguments are the driver's own, which the tail call never wrote: the probe returns to
            // the driver from where the function was called.
            const uint32_t argument_bytes = fixture->stack_bytes;
            const Tier0TestTailRegisters* entry = &shared->entry[1 + r];
            const Tier0TestTailRegisters* landed = &shared->probe[1 + r];
            tier0_test_expect(tally, memcmp(seen->stack + argument_bytes, shared->arguments.stack + argument_bytes, sizeof(seen->stack) - argument_bytes) == 0 &&
                                         memcmp(landed->saved, entry->saved, sizeof(entry->saved)) == 0 && landed->rsp + below == entry->rsp &&
                                         (!hidden || seen->general[0] == shared->arguments.general[0]),
                              check, "%s, %s, run %u: the probe's frame isn't the function's caller's, a callee-saved register changed, or the hidden pointer moved",
                              fixture->name, frame, r);
            const Tier0TestSeen* returns = &shared->returns[1 + r];
            const uint64_t rax = hidden ? shared->arguments.general[0] : shared->returned.rax;
            const bool returned = returns->rax == rax && returns->rdx == shared->returned.rdx && memcmp(returns->vectors[0], shared->returned.vectors[0], 16) == 0 &&
                                  memcmp(returns->vectors[1], shared->returned.vectors[1], 16) == 0 &&
                                  (!hidden || memcmp(seen->hidden, shared->returned.hidden, fixture->result_size) == 0);
            tier0_test_expect(tally, returned, check, "%s, %s, run %u: what the probe returned didn't reach the driver", fixture->name, frame, r);
        }
    }
    arena_release(&arena);
    if (code != MAP_FAILED)
    {
        munmap(code, pages);
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(Tier0TestTailShared));
    }
}
