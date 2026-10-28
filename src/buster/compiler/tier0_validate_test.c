// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier0's validation checks (#569): the stream's validation's vector path (tier0_validate_accepts) against its twin,
// the scalar validator (tier0_validate_scalar). The vector path may leave a stream to the scalar validator, but it must
// never take one the scalar validator refuses, so that tier0_validate's verdict and rule are the scalar's; and it must
// take the streams it is built for, so that a rule it holds too strictly doesn't hide behind the scalar validator:
//   - entries: every kind (NONE, X86 and one past the kinds among them) of every class (the vector's and one past the
//     classes among them), after no operand, one or two of every class, with values at its kind's bounds and past them,
//     then its result and its operands discarded, in a function with locals of each kind (values, objects of 16, 4 and 1
//     bytes, address-taken ones and an address-only one), direct and indirect calls with arguments, a result object and a
//     long double result, a long double constant, the access column and a label JUMP_INDIRECT may go to, with and without
//     long double's and the 128-bit integers' values: the two verdicts the same on every one, but where the vector path
//     leaves to the scalar validator an arrival it doesn't decide (a backward one with values on the stack);
//   - programs: tier0_test_build's checks that the vector path takes every function of every generated program
//     (tier0_test_scan_program) and that the twins agree on each of them changed in one place (a kind, a class or a
//     value replaced, two entries swapped, one removed or repeated);
//   - arrivals: joins of ?: and of && and || at depths, joins whose values differ in class, a value from below the arms,
//     loops, a backward arrival with values, values at the end and an aggregate result reached there;
//   - blocks: values carried across 64-entry blocks at every depth to 40, calls whose operands span blocks, and the
//     limits the vector path leaves to the scalar validator: a block whose pops come to more than its limit, a read more
//     than 63 places below a block's start, a depth past its limit, and more than 128 locals and calls (its gathers);
//   - page edges: the columns of each of those streams ending at a PROT_NONE page, so that a read past one faults.
// tier0_test.c includes this file before its command, whose tally it uses.

// The fixture function's locals: an I32, an address-taken U64, an F64, objects of 16, 4 and 1 bytes, a va_list (an
// address-only U64) and an F32.
#define BUSTER_TIER0_TEST_SCAN_LOCALS 8
static const uint8_t tier0_test_scan_local_classes[BUSTER_TIER0_TEST_SCAN_LOCALS] = {
    BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_U64,  BUSTER_TYPED_CLASS_F64, BUSTER_TYPED_CLASS_VOID,
    BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_F32,
};
static const uint8_t tier0_test_scan_local_flags[BUSTER_TIER0_TEST_SCAN_LOCALS] = {
    0,
    BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
    0,
    BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
    BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
    BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
    BUSTER_TYPED_LOCAL_ADDRESS_TAKEN | BUSTER_TYPED_LOCAL_VA_LIST,
    0,
};
static const uint32_t tier0_test_scan_local_sizes[BUSTER_TIER0_TEST_SCAN_LOCALS] = { 0, 0, 0, 16, 4, 1, 0, 0 };
static const uint8_t tier0_test_scan_local_alignments[BUSTER_TIER0_TEST_SCAN_LOCALS] = { 0, 0, 0, 4, 2, 0, 0, 0 };

// Its calls, every argument in a 16-byte stack slot: call 0 (I32, U64), call 1 an indirect one of an I32, call 2 of
// nothing, call 3 of an F64, call 4 whose result an object (the 16-byte one, local 3) takes through the hidden pointer,
// call 5 whose result is a long double in st0.
#define BUSTER_TIER0_TEST_SCAN_CALLS 6
static const SymbolId tier0_test_scan_call_targets[BUSTER_TIER0_TEST_SCAN_CALLS] = { 5, 0, 5, 5, 5, 5 };
static const uint32_t tier0_test_scan_call_first[BUSTER_TIER0_TEST_SCAN_CALLS] = { 0, 2, 3, 3, 4, 4 };
static const uint32_t tier0_test_scan_call_counts[BUSTER_TIER0_TEST_SCAN_CALLS] = { 2, 1, 0, 1, 0, 0 };
static const uint8_t tier0_test_scan_call_variadic[BUSTER_TIER0_TEST_SCAN_CALLS] = { 0, 0, 0, 0, 0, 0 };
static const uint32_t tier0_test_scan_call_objects[BUSTER_TIER0_TEST_SCAN_CALLS] = { 0, 0, 0, 0, 1 + 3, 0 };
#define BUSTER_TIER0_TEST_SCAN_ARGUMENTS 4
static const uint8_t tier0_test_scan_argument_classes[BUSTER_TIER0_TEST_SCAN_ARGUMENTS] = {
    BUSTER_TYPED_CLASS_I32,
    BUSTER_TYPED_CLASS_U64,
    BUSTER_TYPED_CLASS_I32,
    BUSTER_TYPED_CLASS_F64,
};
// The symbols the global entries name: 1 to 4, one of each access, and none past them.
#define BUSTER_TIER0_TEST_SCAN_SYMBOLS 5
static const uint8_t tier0_test_scan_access[BUSTER_TIER0_TEST_SCAN_SYMBOLS] = {
    BUSTER_TIER0_ACCESS_DIRECT, BUSTER_TIER0_ACCESS_DIRECT, BUSTER_TIER0_ACCESS_GOT, BUSTER_TIER0_ACCESS_LOCAL_EXEC, BUSTER_TIER0_ACCESS_INITIAL_EXEC,
};
static const uint64_t tier0_test_scan_x87_constants[2] = { 0x8000000000000000ull, 0x3fff };

// The fixture's own columns and the function over them, a stream of up to `capacity` entries.
#define BUSTER_TIER0_TEST_SCAN_CAPACITY 16
typedef struct Tier0TestScan Tier0TestScan;
struct Tier0TestScan
{
    uint8_t kinds[BUSTER_TIER0_TEST_SCAN_CAPACITY];
    uint8_t classes[BUSTER_TIER0_TEST_SCAN_CAPACITY];
    uint64_t values[BUSTER_TIER0_TEST_SCAN_CAPACITY];
    AbiLocation argument_locations[BUSTER_TIER0_TEST_SCAN_ARGUMENTS];
    AbiCall calls[BUSTER_TIER0_TEST_SCAN_CALLS];
    uint32_t label_entries[1];
    Tier0Function function;
};

// The fixture function with `count` entries of its columns: long double's values and the 128-bit integers' where `memory`
// says so, the access column and a label JUMP_INDIRECT may go to (the function's end).
static void tier0_test_scan_function(Tier0TestScan* scan, uint32_t count, bool memory)
{
    memset(scan->argument_locations, 0, sizeof(scan->argument_locations));
    memset(scan->calls, 0, sizeof(scan->calls));
    for (uint32_t c = 0; c < BUSTER_TIER0_TEST_SCAN_CALLS; c += 1)
    {
        for (uint32_t a = 0; a < tier0_test_scan_call_counts[c]; a += 1)
        {
            AbiLocation* location = &scan->argument_locations[tier0_test_scan_call_first[c] + a];
            location->kind = BUSTER_ABI_LOCATION_STACK;
            location->stack_offset = 16 * a;
            location->registers[0] = BUSTER_ABI_REGISTER_NONE;
            location->registers[1] = BUSTER_ABI_REGISTER_NONE;
        }
        scan->calls[c].stack_bytes = 16 * tier0_test_scan_call_counts[c];
        scan->calls[c].stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2;
        for (uint32_t p = 0; p < BUSTER_ABI_RESULT_PARTS; p += 1)
        {
            scan->calls[c].result.registers[p] = BUSTER_ABI_REGISTER_NONE;
        }
    }
    scan->calls[4].result.kind = BUSTER_ABI_RESULT_MEMORY;
    scan->calls[5].result.kind = BUSTER_ABI_RESULT_REGISTERS;
    scan->calls[5].result.registers[0] = BUSTER_ABI_REGISTER_X87;
    scan->calls[5].result.part_bytes = 16;
    Tier0Function* function = &scan->function;
    memset(function, 0, sizeof(*function));
    function->kinds = scan->kinds;
    function->classes = scan->classes;
    function->values = scan->values;
    function->count = count;
    function->local_classes = tier0_test_scan_local_classes;
    function->local_flags = tier0_test_scan_local_flags;
    function->local_sizes = tier0_test_scan_local_sizes;
    function->local_alignments = tier0_test_scan_local_alignments;
    function->local_count = BUSTER_TIER0_TEST_SCAN_LOCALS;
    for (uint32_t p = 0; p < BUSTER_ABI_RESULT_PARTS; p += 1)
    {
        function->result.registers[p] = BUSTER_ABI_REGISTER_NONE;
    }
    function->call_targets = tier0_test_scan_call_targets;
    function->call_first = tier0_test_scan_call_first;
    function->call_argument_counts = tier0_test_scan_call_counts;
    function->call_variadic = tier0_test_scan_call_variadic;
    function->calls = scan->calls;
    function->argument_locations = scan->argument_locations;
    function->argument_classes = tier0_test_scan_argument_classes;
    function->call_result_objects = tier0_test_scan_call_objects;
    function->call_count = BUSTER_TIER0_TEST_SCAN_CALLS;
    function->argument_count = BUSTER_TIER0_TEST_SCAN_ARGUMENTS;
    function->symbol_access = tier0_test_scan_access;
    function->symbol_count = BUSTER_TIER0_TEST_SCAN_SYMBOLS;
    function->x87_constants = tier0_test_scan_x87_constants;
    function->x87_constant_count = 1;
    function->x87 = memory;
    function->int128 = memory;
    scan->label_entries[0] = count;
    function->label_entries = scan->label_entries;
    function->label_count = 1;
}

// What an entry pops and whether it pushes, as the vector path's tables have them (the streams below are built with them;
// the twins judge them).
static uint32_t tier0_test_scan_pops(const Tier0Function* function, uint32_t kind, uint32_t class, uint64_t value)
{
    uint32_t pops = kind < BUSTER_TYPED_KIND_LIMIT ? tier0_scan_shapes[kind] & 3 : 0;
    if (kind == BUSTER_TYPED_CALL)
    {
        pops = value < function->call_count ? function->call_argument_counts[value] + (function->call_targets[value] == 0 ? 1 : 0) : 0;
    }
    return kind == BUSTER_TYPED_RETURN && class == BUSTER_TYPED_CLASS_VOID ? 0 : pops;
}

static bool tier0_test_scan_pushes(uint32_t kind, uint32_t class)
{
    const bool pushes = kind < BUSTER_TYPED_KIND_LIMIT && (tier0_scan_shapes[kind] & BUSTER_TIER0_SCAN_PUSH) != 0;
    return pushes && !(kind == BUSTER_TYPED_CALL && class == BUSTER_TYPED_CLASS_VOID);
}

// Whether the twins agree on `function`: the vector path takes it exactly where the scalar validator does, or, with
// `may_leave`, it may leave one the scalar validator takes. Counts what each took into `taken` (vector, scalar).
static bool tier0_test_scan_agree(const Tier0Function* function, bool may_leave, uint64_t* taken)
{
    const bool vector = tier0_validate_accepts(function);
    const bool scalar = tier0_validate_scalar(function).error == TIER0_OK;
    taken[0] += vector ? 1 : 0;
    taken[1] += scalar ? 1 : 0;
    return vector == scalar || (may_leave && scalar);
}

// The values the entries check: each kind's bounds and past them.
static uint32_t tier0_test_scan_values(uint32_t kind, uint32_t entry, uint32_t count, uint64_t* values)
{
    uint32_t n = 0;
    switch (kind)
    {
    case BUSTER_TYPED_LOCAL:
    case BUSTER_TYPED_LOCAL_ADDRESS:
    case BUSTER_TYPED_STORE_LOCAL:
    case BUSTER_TYPED_SET_LOCAL:
        for (uint32_t l = 0; l <= BUSTER_TIER0_TEST_SCAN_LOCALS; l += 1)
        {
            values[n++] = l;
        }
        values[n++] = 1ull << 32;
        break;
    case BUSTER_TYPED_CALL:
        for (uint32_t c = 0; c <= BUSTER_TIER0_TEST_SCAN_CALLS; c += 1)
        {
            values[n++] = c;
        }
        break;
    case BUSTER_TYPED_GLOBAL:
    case BUSTER_TYPED_GLOBAL_ADDRESS:
    case BUSTER_TYPED_STORE_GLOBAL:
    case BUSTER_TYPED_SET_GLOBAL:
        values[n++] = 0;
        values[n++] = 1;
        values[n++] = 4;
        values[n++] = BUSTER_TIER0_TEST_SCAN_SYMBOLS;
        values[n++] = 1ull << 32;
        break;
    case BUSTER_TYPED_BRANCH_FALSE:
    case BUSTER_TYPED_BRANCH_TRUE:
    case BUSTER_TYPED_JUMP:
    case BUSTER_TYPED_JUMP_VALUE:
        // Ahead to the next entry and to the end, back to the start and to itself, and past the end.
        values[n++] = entry + 1;
        values[n++] = count;
        values[n++] = 0;
        values[n++] = entry;
        values[n++] = (uint64_t)count + 1;
        values[n++] = (1ull << 32) + entry + 1;
        break;
    case BUSTER_TYPED_COPY:
        values[n++] = 0;
        values[n++] = 1;
        values[n++] = BUSTER_TIER0_MAX_COPY_BYTES;
        values[n++] = BUSTER_TIER0_MAX_COPY_BYTES + 1;
        break;
    case BUSTER_TYPED_PTR_ADD:
        values[n++] = 0;
        values[n++] = 8;
        values[n++] = 1ull << 40;
        break;
    case BUSTER_TYPED_CONST:
        values[n++] = 0;
        values[n++] = 1;
        values[n++] = 1ull << 40;
        break;
    default:
        values[n++] = 0;
        values[n++] = 7;
        values[n++] = 1ull << 32;
        break;
    }
    return n;
}

// The entries' matrix: each kind of each class after no operand, one or two of every class with a value (`second`
// below `top`, each a CONST of its class; 0 for none), each of its values, then what is left on the stack discarded,
// top first, but after a jump or a return (whose code after it no path reaches). With `memory`, the function has long
// double's and the 128-bit integers' values.
static void tier0_test_scan_entries(Tier0TestTally* tally, bool memory, uint64_t* checked, uint64_t* taken)
{
    Tier0TestScan scan;
    for (uint32_t kind = 0; kind <= BUSTER_TYPED_KINDS; kind += 1)
    {
        const bool two = kind < BUSTER_TYPED_KIND_LIMIT && ((tier0_scan_shapes[kind] & 3) == 2 || kind == BUSTER_TYPED_CALL);
        for (uint32_t class = 0; class < BUSTER_TYPED_CLASS_LIMIT; class += 1)
        {
            for (uint32_t top = 0; top < BUSTER_TYPED_CLASS_VECTOR; top += 1)
            {
                for (uint32_t second = 0; second < BUSTER_TYPED_CLASS_VECTOR; second += 1)
                {
                    // Two operands of every pair of classes for a kind that pops two (or a call), of a few otherwise.
                    if ((second != 0 && top == 0) || (!two && second != 0 && second != BUSTER_TYPED_CLASS_I32 && second != top))
                    {
                        continue;
                    }
                    uint32_t entry = 0;
                    uint8_t stack[3];
                    uint32_t depth = 0;
                    const uint32_t operands[2] = { second, top };
                    for (uint32_t o = 0; o < 2; o += 1)
                    {
                        if (operands[o] != 0)
                        {
                            scan.kinds[entry] = BUSTER_TYPED_CONST;
                            scan.classes[entry] = (uint8_t)operands[o];
                            scan.values[entry] = 0;
                            stack[depth] = (uint8_t)operands[o];
                            depth += 1;
                            entry += 1;
                        }
                    }
                    uint64_t values[16];
                    // The count (for the targets) with the discards: at most the operands and the result.
                    const uint32_t value_count = tier0_test_scan_values(kind, entry, entry + 1 + depth + 1, values);
                    for (uint32_t v = 0; v < value_count; v += 1)
                    {
                        uint32_t count = entry;
                        scan.kinds[count] = (uint8_t)kind;
                        scan.classes[count] = (uint8_t)class;
                        scan.values[count] = values[v];
                        count += 1;
                        tier0_test_scan_function(&scan, 0, memory);
                        const uint32_t pops = tier0_test_scan_pops(&scan.function, kind, class, values[v]);
                        const bool transfer = kind < BUSTER_TYPED_KIND_LIMIT && (tier0_scan_sides[kind] & BUSTER_TIER0_SIDE_TRANSFER) != 0;
                        uint32_t left = pops <= depth ? depth - pops : 0;
                        uint8_t remaining[4];
                        memcpy(remaining, stack, left);
                        if (pops <= depth && tier0_test_scan_pushes(kind, class))
                        {
                            remaining[left] = (uint8_t)class;
                            left += 1;
                        }
                        for (uint32_t r = left; r > 0 && !transfer; r -= 1)
                        {
                            scan.kinds[count] = BUSTER_TYPED_DISCARD;
                            scan.classes[count] = remaining[r - 1];
                            scan.values[count] = 0;
                            count += 1;
                        }
                        // The targets were made for the longest stream: keep them so, but for the end's.
                        if (kind >= BUSTER_TYPED_BRANCH_FALSE && kind <= BUSTER_TYPED_JUMP_VALUE && values[v] == entry + 1 + depth + 1)
                        {
                            scan.values[entry] = count;
                        }
                        tier0_test_scan_function(&scan, count, memory);
                        // A backward arrival with values on the stack (to the start, past the operands, or a JUMP_VALUE
                        // to itself) is the scalar validator's to decide.
                        const bool backward = kind >= BUSTER_TYPED_BRANCH_FALSE && kind <= BUSTER_TYPED_JUMP_VALUE && scan.values[entry] <= entry;
                        *checked += 1;
                        if (!tier0_test_scan_agree(&scan.function, backward, taken))
                        {
                            tier0_test_expect(tally, false, "validation", "kind %u class %u after %u and %u, value %" PRIu64 ": the vector path %s, the scalar validator %s (%s)",
                                              kind, class, second, top, values[v], tier0_validate_accepts(&scan.function) ? "takes it" : "leaves it",
                                              tier0_validate_scalar(&scan.function).error == TIER0_OK ? "takes it" : "refuses it",
                                              tier0_validate_scalar(&scan.function).rule != 0 ? tier0_validate_scalar(&scan.function).rule : "");
                        }
                    }
                }
            }
        }
    }
}

// A step of splitmix64 over `state`.
static uint64_t tier0_test_scan_random(uint64_t* state)
{
    *state += 0x9e3779b97f4a7c15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// A function of a generated program (tier0_test_build's), which the vector path must take, and `changes` copies of it each
// changed in one place, from `seed`: a kind, a class or a value replaced (by another entry's, or by any, or a value by its
// neighbour), two entries swapped, one removed or repeated. The vector path may take a copy only where the scalar
// validator does; `taken` counts what each took.
static void tier0_test_scan_program(Tier0TestTally* tally, const Tier0Function* function, Arena* arena, uint64_t seed, uint32_t changes, uint64_t* taken)
{
    tier0_test_expect(tally, function->vectors || tier0_validate_accepts(function), "validation", "seed %" PRIu64 ": the vector path leaves a valid function to the scalar validator",
                      seed);
    const uint32_t count = function->count;
    const uint64_t mark = arena->length;
    uint8_t* kinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1);
    uint8_t* classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)count + 1);
    uint64_t* values = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)count + 1);
    uint64_t state = seed * 0x2545f4914f6cdd1dull;
    for (uint32_t c = 0; c < changes && count != 0 && kinds != 0 && classes != 0 && values != 0; c += 1)
    {
        memcpy(kinds, function->kinds, count);
        memcpy(classes, function->classes, count);
        memcpy(values, function->values, (uint64_t)count * sizeof(uint64_t));
        const uint32_t way = (uint32_t)(tier0_test_scan_random(&state) % 9);
        const uint32_t at = (uint32_t)(tier0_test_scan_random(&state) % count);
        const uint32_t other = (uint32_t)(tier0_test_scan_random(&state) % count);
        const uint64_t any = tier0_test_scan_random(&state);
        uint32_t changed = count;
        switch (way)
        {
        case 0:
            kinds[at] = kinds[other];
            break;
        case 1:
            kinds[at] = (uint8_t)(any % (BUSTER_TYPED_KINDS + 1));
            break;
        case 2:
            classes[at] = classes[other];
            break;
        case 3:
            classes[at] = (uint8_t)(any % BUSTER_TYPED_CLASS_LIMIT);
            break;
        case 4:
            values[at] = values[other];
            break;
        case 5:
            values[at] += (any & 1) != 0 ? 1 : (uint64_t)-1;
            break;
        case 6:
        {
            const uint32_t next = at + 1 < count ? at + 1 : at;
            const uint8_t kind = kinds[at];
            const uint8_t class = classes[at];
            const uint64_t value = values[at];
            kinds[at] = kinds[next];
            classes[at] = classes[next];
            values[at] = values[next];
            kinds[next] = kind;
            classes[next] = class;
            values[next] = value;
            break;
        }
        case 7:
            memmove(kinds + at, kinds + at + 1, count - at - 1);
            memmove(classes + at, classes + at + 1, count - at - 1);
            memmove(values + at, values + at + 1, (uint64_t)(count - at - 1) * sizeof(uint64_t));
            changed = count - 1;
            break;
        default:
            memmove(kinds + at + 1, kinds + at, count - at);
            memmove(classes + at + 1, classes + at, count - at);
            memmove(values + at + 1, values + at, (uint64_t)(count - at) * sizeof(uint64_t));
            changed = count + 1;
            break;
        }
        Tier0Function copy = *function;
        copy.kinds = kinds;
        copy.classes = classes;
        copy.values = values;
        copy.count = changed;
        if (!tier0_test_scan_agree(&copy, true, taken))
        {
            const Tier0Check check = tier0_validate_scalar(&copy);
            tier0_test_expect(tally, false, "validation", "seed %" PRIu64 " change %u (way %u at %u): the vector path takes what the scalar validator refuses at %u (%s)", seed,
                              c, way, at, check.node, check.rule != 0 ? check.rule : "");
        }
    }
    arena_pop_to(arena, mark);
}

// A stream being built for the arrivals' and the blocks' checks, in the fixture function (tier0_test_scan_function's
// tables), with room for `capacity` entries.
typedef struct Tier0TestStream Tier0TestStream;
struct Tier0TestStream
{
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* values;
    uint32_t count;
    uint32_t capacity;
};

static uint32_t tier0_test_stream_add(Tier0TestStream* stream, uint32_t kind, uint32_t class, uint64_t value)
{
    const uint32_t entry = stream->count;
    if (entry < stream->capacity)
    {
        stream->kinds[entry] = (uint8_t)kind;
        stream->classes[entry] = (uint8_t)class;
        stream->values[entry] = value;
        stream->count += 1;
    }
    return entry;
}

// `pairs` entries' worth of code at statement level: a local's value, discarded.
static void tier0_test_stream_pad(Tier0TestStream* stream, uint32_t pairs)
{
    for (uint32_t p = 0; p < pairs; p += 1)
    {
        tier0_test_stream_add(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 0);
        tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0);
    }
}

// The same over a value of class `top`, which the entry after the code then takes as its last operand: the code's last
// entry has its class (a copy of it, discarded), as tier0_last_mismatch's rule has it.
static void tier0_test_stream_pad_over(Tier0TestStream* stream, uint32_t pairs, uint32_t top)
{
    tier0_test_stream_pad(stream, pairs);
    if (pairs != 0)
    {
        tier0_test_stream_add(stream, BUSTER_TYPED_DUP, top, 0);
        tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, top, 0);
    }
}

// The twins' verdicts on `function` with the stream's columns, copied to end at a PROT_NONE page (`guard`, a page after one
// that may be read, or 0 to keep them where they are): the scalar validator takes it where `valid` says, and the vector
// path takes it exactly then, but where `leaves` says it leaves it to the scalar validator.
static void tier0_test_stream_judge(Tier0TestTally* tally, const char* name, const Tier0TestStream* stream, const Tier0Function* base, bool valid, bool leaves,
                                    uint8_t* guard, uint64_t* taken)
{
    Tier0Function function = *base;
    function.kinds = stream->kinds;
    function.classes = stream->classes;
    function.values = stream->values;
    if (guard != 0 && (uint64_t)stream->count * sizeof(uint64_t) <= BUSTER_TIER0_TEST_PAGE)
    {
        // Three separate readable pages, each followed by its own inaccessible page.
        uint64_t* values = (uint64_t*)(void*)(guard - (uint64_t)stream->count * sizeof(uint64_t));
        uint8_t* classes = guard + 2 * BUSTER_TIER0_TEST_PAGE - stream->count;
        uint8_t* kinds = guard + 4 * BUSTER_TIER0_TEST_PAGE - stream->count;
        memcpy(values, stream->values, (uint64_t)stream->count * sizeof(uint64_t));
        memcpy(classes, stream->classes, stream->count);
        memcpy(kinds, stream->kinds, stream->count);
        function.kinds = kinds;
        function.classes = classes;
        function.values = values;
    }
    const bool vector = tier0_validate_accepts(&function);
    const Tier0Check scalar = tier0_validate_scalar(&function);
    taken[0] += vector ? 1 : 0;
    taken[1] += scalar.error == TIER0_OK ? 1 : 0;
    tier0_test_expect(tally, (scalar.error == TIER0_OK) == valid && vector == (valid && !leaves), "validation",
                      "%s (%u entries%s): the scalar validator %s (%s at %u), the vector path %s", name, stream->count, guard != 0 ? ", at a page's end" : "",
                      scalar.error == TIER0_OK ? "takes it" : "refuses it", scalar.rule != 0 ? scalar.rule : "", scalar.node, vector ? "takes it" : "leaves it");
}

// The same in the fixture function (tier0_test_scan_function's).
static void tier0_test_stream_check(Tier0TestTally* tally, const char* name, const Tier0TestStream* stream, bool memory, bool valid, bool leaves, uint8_t* guard,
                                    uint64_t* taken)
{
    Tier0TestScan scan;
    tier0_test_scan_function(&scan, stream->count, memory);
    tier0_test_stream_judge(tally, name, stream, &scan.function, valid, leaves, guard, taken);
}

// The arrivals' fixtures: joins at depths, each check's break, and the arrivals the vector path leaves to the scalar
// validator.
static void tier0_test_scan_arrivals(Tier0TestTally* tally, Tier0TestStream* stream, uint8_t* guard, uint64_t* taken)
{
    for (uint32_t edge = 0; edge < 2; edge += 1)
    {
        uint8_t* at = edge != 0 ? guard : 0;
        // x + (c ? a : b), and with b of another class than a.
        for (uint32_t way = 0; way < 2; way += 1)
        {
            stream->count = 0;
            tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 1);
            tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
            tier0_test_stream_add(stream, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 5);
            tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 2);
            tier0_test_stream_add(stream, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I64, 6);
            tier0_test_stream_add(stream, BUSTER_TYPED_CONST, way == 0 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_I32, 3);
            tier0_test_stream_add(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_I64, 0);
            tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
            tier0_test_stream_check(tally, way == 0 ? "?: under a value" : "?: of arms of two classes", stream, false, way == 0, false, at, taken);
        }
        // a && b as a value.
        stream->count = 0;
        tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
        tier0_test_stream_add(stream, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 6);
        tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 2);
        tier0_test_stream_add(stream, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 6);
        tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
        tier0_test_stream_add(stream, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I32, 7);
        tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
        tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0);
        tier0_test_stream_check(tally, "&& as a value", stream, false, true, false, at, taken);
        // A loop, and code after a return that no path reaches.
        stream->count = 0;
        tier0_test_stream_add(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 0);
        tier0_test_stream_add(stream, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 7);
        tier0_test_stream_add(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 0);
        tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
        tier0_test_stream_add(stream, BUSTER_TYPED_SUB, BUSTER_TYPED_CLASS_I32, 0);
        tier0_test_stream_add(stream, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, 0);
        tier0_test_stream_add(stream, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, 0);
        tier0_test_stream_add(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
        tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0);
        tier0_test_stream_check(tally, "a loop, then code no path reaches", stream, false, true, false, at, taken);
        // A backward arrival with a value below it, which the scalar validator takes and the vector path leaves to it.
        stream->count = 0;
        tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 5);
        tier0_test_stream_add(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 0);
        tier0_test_stream_add(stream, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 4);
        // A jump's class is its own: I64, the class of the value the DISCARD after it takes.
        tier0_test_stream_add(stream, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_I64, 1);
        tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
        tier0_test_stream_check(tally, "a backward arrival with a value below", stream, false, true, true, at, taken);
        // x ?: y, whose arrivals bring values of two producers, of one class, which the vector path leaves to the scalar
        // validator; and of two classes, which both refuse.
        for (uint32_t way = 0; way < 2; way += 1)
        {
            stream->count = 0;
            tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 5);
            tier0_test_stream_add(stream, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_I32, 0);
            tier0_test_stream_add(stream, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_I32, 5);
            tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0);
            tier0_test_stream_add(stream, BUSTER_TYPED_CONST, way == 0 ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U64, 7);
            tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, way == 0 ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_stream_check(tally, way == 0 ? "x ?: y" : "x ?: y of two classes", stream, false, way == 0, way == 0, at, taken);
        }
        // A target reached at two depths; a value left at the end, by a jump and by a JUMP_VALUE.
        stream->count = 0;
        tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
        tier0_test_stream_add(stream, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 4);
        tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 3);
        tier0_test_stream_add(stream, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, 4);
        tier0_test_stream_add(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        tier0_test_stream_check(tally, "a target reached at two depths", stream, false, false, false, at, taken);
        for (uint32_t way = 0; way < 2; way += 1)
        {
            stream->count = 0;
            tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
            tier0_test_stream_add(stream, way == 0 ? BUSTER_TYPED_JUMP : BUSTER_TYPED_JUMP_VALUE, way == 0 ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_I32, 2);
            tier0_test_stream_check(tally, way == 0 ? "a jump to the end with a value" : "a JUMP_VALUE to the end", stream, false, false, false, at, taken);
        }
        // x + (c ? a : b) whose arms and condition are longer than a block, so that the arrivals reach other blocks: at
        // depth 1, with the value of the right class and of the wrong one.
        for (uint32_t way = 0; way < 2; way += 1)
        {
            stream->count = 0;
            tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 1);
            tier0_test_stream_pad(stream, 40);
            tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
            const uint32_t branch = tier0_test_stream_add(stream, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 0);
            tier0_test_stream_pad(stream, 40);
            tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 2);
            const uint32_t jump = tier0_test_stream_add(stream, BUSTER_TYPED_JUMP_VALUE, BUSTER_TYPED_CLASS_I64, 0);
            stream->values[branch] = stream->count;
            tier0_test_stream_pad(stream, 40);
            tier0_test_stream_add(stream, BUSTER_TYPED_CONST, way == 0 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_U64, 3);
            stream->values[jump] = stream->count;
            tier0_test_stream_add(stream, way == 0 ? BUSTER_TYPED_ADD : BUSTER_TYPED_DISCARD, way == 0 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
            tier0_test_stream_check(tally, way == 0 ? "?: across blocks" : "?: across blocks of arms of two classes", stream, false, way == 0, false, 0, taken);
        }
    }
}

// The blocks' checks: values carried across 64-entry blocks, at every depth to 40 and every place of the boundary in
// the code between their pushes and their pops; a call's operands spanning a block's start; and the limits the vector
// path leaves to the scalar validator: a depth past BUSTER_TIER0_SCAN_DEPTH at a block's start. With `guard`, the columns
// end at a PROT_NONE page.
static void tier0_test_scan_blocks(Tier0TestTally* tally, Tier0TestStream* stream, uint8_t* guard, uint64_t* taken)
{
    for (uint32_t depth = 1; depth <= 40; depth += 3)
    {
        for (uint32_t pad = 0; pad < 70; pad += 1)
        {
            stream->count = 0;
            tier0_test_stream_pad(stream, pad);
            for (uint32_t d = 0; d < depth; d += 1)
            {
                tier0_test_stream_add(stream, BUSTER_TYPED_CONST, d % 2 == 0 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_F64, d);
            }
            tier0_test_stream_pad_over(stream, pad % 37, (depth - 1) % 2 == 0 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_F64);
            // Each pair from the top, F64 under I64 or I64 under F64, converted and added, then discarded.
            for (uint32_t d = depth; d > 1; d -= 1)
            {
                const uint32_t top = (d - 1) % 2 == 0 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_F64;
                const uint32_t below = (d - 2) % 2 == 0 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_F64;
                tier0_test_stream_add(stream, BUSTER_TYPED_CONVERT, below, 0);
                tier0_test_stream_add(stream, BUSTER_TYPED_ADD, below, 0);
                (void)top;
            }
            tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
            tier0_test_stream_check(tally, "values carried across blocks", stream, false, true, false, (pad + depth) % 5 == 0 ? guard : 0, taken);
        }
    }
    // f(a, b), the arguments in one block and the call in the next, at every place of the boundary.
    for (uint32_t pad = 0; pad < 70; pad += 1)
    {
        stream->count = 0;
        tier0_test_stream_pad(stream, pad / 2);
        if (pad % 2 != 0)
        {
            tier0_test_stream_add(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0);
            tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0);
        }
        tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
        tier0_test_stream_pad(stream, pad % 7);
        tier0_test_stream_add(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_test_stream_pad(stream, pad % 5);
        tier0_test_stream_add(stream, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_I64, 0);
        tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
        // An indirect call of the same argument: its callee under it.
        tier0_test_stream_add(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        tier0_test_stream_pad(stream, pad % 3);
        tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
        tier0_test_stream_add(stream, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 1);
        tier0_test_stream_check(tally, "calls across blocks", stream, false, true, false, pad % 4 == 0 ? guard : 0, taken);
        // The same with an argument of the wrong class.
        stream->classes[stream->count - 2] = BUSTER_TYPED_CLASS_U32;
        tier0_test_stream_check(tally, "calls across blocks, an argument of another class", stream, false, false, false, 0, taken);
    }
    // 134 values at once, 64 at the second block's start and 128 at the third's, past BUSTER_TIER0_SCAN_DEPTH: the
    // scalar validator's.
    stream->count = 0;
    for (uint32_t d = 0; d < 134; d += 1)
    {
        tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, d);
    }
    for (uint32_t d = 1; d < 134; d += 1)
    {
        tier0_test_stream_add(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_I64, 0);
    }
    tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
    tier0_test_stream_check(tally, "a depth past the vector path's at a block's start", stream, false, true, true, 0, taken);
}

// Interacting malformed entries must defer before byte depths become producer indices or classes become shifts.
// Wide indirect calls additionally need one more operand than their explicit arguments.
static void tier0_test_scan_review_regressions(Tier0TestTally* tally, Tier0TestStream* stream, uint8_t* guard, uint64_t* taken)
{
    const uint32_t bad_classes[] = { BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_VECTOR, UINT8_MAX };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(bad_classes); c += 1)
    {
        for (uint32_t call = 0; call < 2; call += 1)
        {
            stream->count = 0;
            tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, bad_classes[c], 0);
            if (call != 0)
            {
                tier0_test_stream_add(stream, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 2);
            }
            tier0_test_stream_add(stream, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, call != 0 ? 3 : 2);
            tier0_test_stream_add(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            tier0_test_stream_check(tally, "underflow or invalid class before calls and arrivals", stream, false, false, false, guard, taken);
        }
    }
    for (uint32_t indirect = 0; indirect < 2; indirect += 1)
    {
        for (uint32_t arguments = 63; arguments <= 64; arguments += 1)
        {
            const SymbolId target = indirect != 0 ? 0 : 1;
            const uint32_t first = 0;
            const uint8_t variadic = 0;
            uint8_t classes[64];
            AbiLocation locations[64];
            AbiCall call = { 0 };
            call.stack_bytes = arguments * 8;
            call.stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2;
            for (uint32_t p = 0; p < BUSTER_ABI_RESULT_PARTS; p += 1)
            {
                call.result.registers[p] = BUSTER_ABI_REGISTER_NONE;
            }
            for (uint32_t a = 0; a < arguments; a += 1)
            {
                classes[a] = BUSTER_TYPED_CLASS_I32;
                locations[a] = (AbiLocation){
                    .stack_offset = a * 8,
                    .registers = { BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
                    .kind = BUSTER_ABI_LOCATION_STACK,
                };
            }
            Tier0Function function = {
                .call_targets = &target,
                .call_first = &first,
                .call_argument_counts = &arguments,
                .call_variadic = &variadic,
                .calls = &call,
                .argument_locations = locations,
                .argument_classes = classes,
                .call_count = 1,
                .argument_count = arguments,
            };
            for (uint32_t p = 0; p < BUSTER_ABI_RESULT_PARTS; p += 1)
            {
                function.result.registers[p] = BUSTER_ABI_REGISTER_NONE;
            }
            const uint32_t operands = arguments + indirect;
            stream->count = 0;
            // Put the final argument at entry 64 and the call just beyond the block boundary.
            for (uint32_t pad = operands; pad < 65; pad += 1)
            {
                tier0_test_stream_add(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            }
            if (indirect != 0)
            {
                tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 1);
            }
            for (uint32_t a = 0; a < arguments; a += 1)
            {
                tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, a);
            }
            const uint32_t last = stream->count - 1;
            tier0_test_stream_add(stream, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 0);
            tier0_test_stream_add(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            function.count = stream->count;
            tier0_test_stream_judge(tally, "63/64 argument direct/indirect call across blocks", stream, &function, true, operands > 64, guard, taken);
            stream->classes[last] = BUSTER_TYPED_CLASS_U32;
            tier0_test_stream_judge(tally, "wide call last argument has the wrong class", stream, &function, false, false, guard, taken);
        }
    }
}

// A function of `locals` I64 locals and `calls` calls, call c of (c % 3) I64 arguments in 16-byte stack slots but the
// last, of 64: past 128 of each, the vector path's gathers. Its stream: each local read and discarded, each call but the
// last made of locals' values and its result discarded, and with `wide`, the last call, of 64 values, made at the start of
// a block whose pops then come to more than BUSTER_TIER0_SCAN_POPS (the scalar validator's).
typedef struct Tier0TestLarge Tier0TestLarge;
struct Tier0TestLarge
{
    uint8_t* local_classes;
    uint8_t* local_flags;
    SymbolId* call_targets;
    uint32_t* call_first;
    uint32_t* call_counts;
    uint8_t* call_variadic;
    AbiCall* calls;
    AbiLocation* argument_locations;
    uint8_t* argument_classes;
};

static void tier0_test_scan_large(Tier0TestTally* tally, Tier0TestStream* stream, Arena* arena, uint32_t locals, uint32_t calls, uint64_t* taken)
{
    const uint64_t mark = arena->length;
    Tier0TestLarge large;
    const uint32_t arguments = 3 * calls + 64;
    large.local_classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, locals);
    large.local_flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, locals);
    large.call_targets = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, calls + 1);
    large.call_first = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, calls + 1);
    large.call_counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, calls + 1);
    large.call_variadic = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, calls + 1);
    large.calls = BUSTER_ARENA_PUSH_ARRAY(arena, AbiCall, calls + 1);
    large.argument_locations = BUSTER_ARENA_PUSH_ARRAY(arena, AbiLocation, arguments);
    large.argument_classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, arguments);
    if (tier0_test_expect(tally, large.argument_classes != 0, "validation", "no memory for the large function"))
    {
        memset(large.local_flags, 0, locals);
        memset(large.local_classes, BUSTER_TYPED_CLASS_I64, locals);
        memset(large.call_variadic, 0, calls + 1);
        memset(large.calls, 0, (uint64_t)(calls + 1) * sizeof(AbiCall));
        uint32_t argument = 0;
        for (uint32_t c = 0; c <= calls; c += 1)
        {
            const uint32_t count = c < calls ? c % 3 : 64;
            large.call_targets[c] = 5;
            large.call_first[c] = argument;
            large.call_counts[c] = count;
            large.calls[c].stack_bytes = 16 * count;
            large.calls[c].stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2;
            for (uint32_t p = 0; p < BUSTER_ABI_RESULT_PARTS; p += 1)
            {
                large.calls[c].result.registers[p] = BUSTER_ABI_REGISTER_NONE;
            }
            for (uint32_t a = 0; a < count; a += 1)
            {
                large.argument_locations[argument] = (AbiLocation){ .stack_offset = 16 * a, .registers = { BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
                                                                    .kind = BUSTER_ABI_LOCATION_STACK };
                large.argument_classes[argument] = BUSTER_TYPED_CLASS_I64;
                argument += 1;
            }
        }
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.local_classes = large.local_classes;
        function.local_flags = large.local_flags;
        function.local_count = locals;
        for (uint32_t p = 0; p < BUSTER_ABI_RESULT_PARTS; p += 1)
        {
            function.result.registers[p] = BUSTER_ABI_REGISTER_NONE;
        }
        function.call_targets = large.call_targets;
        function.call_first = large.call_first;
        function.call_argument_counts = large.call_counts;
        function.call_variadic = large.call_variadic;
        function.calls = large.calls;
        function.argument_locations = large.argument_locations;
        function.argument_classes = large.argument_classes;
        function.call_count = calls + 1;
        function.argument_count = argument;
        for (uint32_t wide = 0; wide < 2; wide += 1)
        {
            stream->count = 0;
            for (uint32_t l = 0; l < locals; l += 1)
            {
                tier0_test_stream_add(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, l);
                tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
            }
            for (uint32_t c = 0; c < calls; c += 1)
            {
                for (uint32_t a = 0; a < c % 3; a += 1)
                {
                    tier0_test_stream_add(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, (c + a) % locals);
                }
                tier0_test_stream_add(stream, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_I64, c);
                tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
            }
            if (wide != 0)
            {
                // 64 values to the next block's start, the call there, and 15 sums after it in the same block.
                while (stream->count % 64 != 0)
                {
                    tier0_test_stream_add(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
                }
                for (uint32_t a = 0; a < 64; a += 1)
                {
                    tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, a);
                }
                tier0_test_stream_add(stream, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_I64, calls);
                for (uint32_t s = 0; s < 15; s += 1)
                {
                    tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, s);
                    tier0_test_stream_add(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, s);
                    tier0_test_stream_add(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_I64, 0);
                    tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
                }
                tier0_test_stream_add(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
            }
            function.count = stream->count;
            tier0_test_stream_judge(tally, wide != 0 ? "a block that pops more than the vector path's" : "more locals and calls than vpermi2b's 128", stream, &function, true,
                                    wide != 0, 0, taken);
            if (wide == 0)
            {
                // A local past the locals, and a call's argument of another class.
                stream->values[2 * (locals - 1)] = locals;
                tier0_test_stream_judge(tally, "a local past the locals, of more than 128", stream, &function, false, false, 0, taken);
                stream->values[2 * (locals - 1)] = locals - 1;
                large.argument_classes[argument - 65] = BUSTER_TYPED_CLASS_U64;
                tier0_test_stream_judge(tally, "a call's argument of another class, of more than 128 calls", stream, &function, false, false, 0, taken);
                large.argument_classes[argument - 65] = BUSTER_TYPED_CLASS_I64;
            }
        }
    }
    arena_pop_to(arena, mark);
}

// The metadata's checks: the tables beside a stream (the locals, the calls, their arguments, the parameters and the
// result), each field changed in turn to each of its edges' values, with a stream built from the tables as they are
// (each local named, each call made of operands of its arguments' classes), so that the stream's own rules hold and the
// tables' decide: on a plain function (the vector path's own checks of them, tier0_scan_tables) and on one with a result
// object (the scalar helpers'), the twins' verdicts the same on every one.
typedef struct Tier0TestMeta Tier0TestMeta;
struct Tier0TestMeta
{
    uint8_t local_classes[BUSTER_TIER0_TEST_SCAN_LOCALS];
    uint8_t local_flags[BUSTER_TIER0_TEST_SCAN_LOCALS];
    uint32_t local_sizes[BUSTER_TIER0_TEST_SCAN_LOCALS];
    uint8_t local_alignments[BUSTER_TIER0_TEST_SCAN_LOCALS];
    SymbolId call_targets[BUSTER_TIER0_TEST_SCAN_CALLS];
    uint32_t call_first[BUSTER_TIER0_TEST_SCAN_CALLS];
    uint32_t call_counts[BUSTER_TIER0_TEST_SCAN_CALLS];
    uint8_t call_variadic[BUSTER_TIER0_TEST_SCAN_CALLS];
    uint32_t call_objects[BUSTER_TIER0_TEST_SCAN_CALLS];
    AbiCall calls[BUSTER_TIER0_TEST_SCAN_CALLS];
    AbiLocation argument_locations[BUSTER_TIER0_TEST_SCAN_ARGUMENTS];
    uint8_t argument_classes[BUSTER_TIER0_TEST_SCAN_ARGUMENTS];
    AbiLocation parameters[2];
    uint8_t kinds[64];
    uint8_t classes[64];
    uint64_t values[64];
    Tier0Function function;
};

// The base tables: the fixture's locals, locals 0 and 1 parameters (an I32 in rdi, a U64 on the stack), call 0's arguments
// in rdi and rsi, call 3's in xmm0; and with `objects`, call 4's result through its hidden pointer into local 3.
static void tier0_test_meta_base(Tier0TestMeta* meta, bool objects)
{
    memcpy(meta->local_classes, tier0_test_scan_local_classes, sizeof(meta->local_classes));
    memcpy(meta->local_flags, tier0_test_scan_local_flags, sizeof(meta->local_flags));
    memcpy(meta->local_sizes, tier0_test_scan_local_sizes, sizeof(meta->local_sizes));
    memcpy(meta->local_alignments, tier0_test_scan_local_alignments, sizeof(meta->local_alignments));
    memcpy(meta->call_targets, tier0_test_scan_call_targets, sizeof(meta->call_targets));
    memcpy(meta->call_first, tier0_test_scan_call_first, sizeof(meta->call_first));
    memcpy(meta->call_counts, tier0_test_scan_call_counts, sizeof(meta->call_counts));
    memcpy(meta->call_variadic, tier0_test_scan_call_variadic, sizeof(meta->call_variadic));
    memcpy(meta->call_objects, tier0_test_scan_call_objects, sizeof(meta->call_objects));
    memcpy(meta->argument_classes, tier0_test_scan_argument_classes, sizeof(meta->argument_classes));
    Tier0TestScan scan;
    tier0_test_scan_function(&scan, 0, true);
    memcpy(meta->calls, scan.calls, sizeof(meta->calls));
    memcpy(meta->argument_locations, scan.argument_locations, sizeof(meta->argument_locations));
    if (!objects)
    {
        meta->calls[4].result.kind = BUSTER_ABI_RESULT_IGNORE;
    }
    meta->argument_locations[0] = (AbiLocation){ .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS };
    meta->argument_locations[1] = (AbiLocation){ .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS };
    meta->argument_locations[3] = (AbiLocation){ .registers = { BUSTER_ABI_REGISTER_VECTOR | 0, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS };
    meta->parameters[0] = (AbiLocation){ .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS };
    meta->parameters[1] = (AbiLocation){ .stack_offset = 0, .registers = { BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_STACK };
    Tier0Function* function = &meta->function;
    *function = scan.function;
    function->kinds = meta->kinds;
    function->classes = meta->classes;
    function->values = meta->values;
    function->local_classes = meta->local_classes;
    function->local_flags = meta->local_flags;
    function->local_sizes = meta->local_sizes;
    function->local_alignments = meta->local_alignments;
    function->parameter_count = 2;
    function->parameters = meta->parameters;
    function->call_targets = meta->call_targets;
    function->call_first = meta->call_first;
    function->call_argument_counts = meta->call_counts;
    function->call_variadic = meta->call_variadic;
    function->calls = meta->calls;
    function->argument_locations = meta->argument_locations;
    function->argument_classes = meta->argument_classes;
    function->call_result_objects = objects ? meta->call_objects : 0;
    function->label_count = 0;
    function->label_entries = 0;
}

// A stream over the tables as they are: each local's value (or address, where it is taken), each call of operands of its
// arguments' classes (an indirect one's callee first), its result discarded, then the return.
static void tier0_test_meta_stream(Tier0TestMeta* meta)
{
    Tier0TestStream stream = { .kinds = meta->kinds, .classes = meta->classes, .values = meta->values, .count = 0, .capacity = 64 };
    const Tier0Function* function = &meta->function;
    for (uint32_t l = 0; l < BUSTER_TIER0_TEST_SCAN_LOCALS; l += 1)
    {
        const uint32_t class = meta->local_classes[l];
        if ((meta->local_flags[l] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0)
        {
            tier0_test_stream_add(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, l);
            tier0_test_stream_add(&stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
        }
        else if (class != BUSTER_TYPED_CLASS_VOID && class < BUSTER_TYPED_CLASS_VECTOR)
        {
            tier0_test_stream_add(&stream, BUSTER_TYPED_LOCAL, class, l);
            tier0_test_stream_add(&stream, BUSTER_TYPED_DISCARD, class, 0);
        }
    }
    for (uint32_t c = 0; c < BUSTER_TIER0_TEST_SCAN_CALLS; c += 1)
    {
        if (meta->call_targets[c] == 0)
        {
            tier0_test_stream_add(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
        }
        const uint32_t first = meta->call_first[c];
        for (uint32_t a = 0; a < meta->call_counts[c] && a < 4 && first + a < BUSTER_TIER0_TEST_SCAN_ARGUMENTS; a += 1)
        {
            const uint32_t class = meta->argument_classes[first + a];
            tier0_test_stream_add(&stream, BUSTER_TYPED_CONST, class != 0 && class < BUSTER_TYPED_CLASS_VECTOR ? class : BUSTER_TYPED_CLASS_I32, 0);
        }
        const bool object = function->call_result_objects != 0 && meta->call_objects[c] != 0;
        const bool x87 = meta->calls[c].result.kind == BUSTER_ABI_RESULT_REGISTERS && meta->calls[c].result.registers[0] == BUSTER_ABI_REGISTER_X87;
        const uint32_t result = object ? BUSTER_TYPED_CLASS_VOID : x87 ? BUSTER_TYPED_CLASS_F80 : BUSTER_TYPED_CLASS_I64;
        tier0_test_stream_add(&stream, BUSTER_TYPED_CALL, result, c);
        if (result != BUSTER_TYPED_CLASS_VOID)
        {
            tier0_test_stream_add(&stream, BUSTER_TYPED_DISCARD, result, 0);
        }
    }
    tier0_test_stream_add(&stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
    meta->function.count = stream.count;
}

// The twins on the tables with field `field` (and its subscript) at `value`, from the base ones. Returns whether the
// scalar validator took them.
static bool tier0_test_meta_check(Tier0TestTally* tally, bool objects, uint32_t field, uint32_t at, uint64_t value, uint64_t* taken)
{
    Tier0TestMeta meta;
    tier0_test_meta_base(&meta, objects);
    switch (field)
    {
    case 0: meta.local_classes[at] = (uint8_t)value; break;
    case 1: meta.local_flags[at] = (uint8_t)value; break;
    case 2: meta.local_sizes[at] = (uint32_t)value; break;
    case 3: meta.local_alignments[at] = (uint8_t)value; break;
    case 4: meta.call_targets[at] = (SymbolId)value; break;
    case 5: meta.call_first[at] = (uint32_t)value; break;
    case 6: meta.call_counts[at] = (uint32_t)value; break;
    case 7: meta.calls[at].stack_bytes = (uint32_t)value; break;
    case 8: meta.calls[at].stack_alignment_log2 = (uint8_t)value; break;
    case 9: meta.calls[at].result.kind = (uint8_t)value; break;
    case 10: meta.calls[at].result.registers[0] = (uint8_t)value; break;
    case 11: meta.argument_locations[at].kind = (uint8_t)value; break;
    case 12: meta.argument_locations[at].registers[0] = (uint8_t)value; break;
    case 13: meta.argument_locations[at].stack_offset = (uint32_t)value; break;
    case 14: meta.argument_classes[at] = (uint8_t)value; break;
    case 15: meta.parameters[at].kind = (uint8_t)value; break;
    case 16: meta.parameters[at].registers[0] = (uint8_t)value; break;
    case 17: meta.parameters[at].stack_offset = (uint32_t)value; break;
    case 18: meta.function.result.kind = (uint8_t)value; break;
    case 19: meta.function.result.registers[0] = (uint8_t)value; break;
    case 20: meta.function.parameter_count = (uint32_t)value; break;
    case 21: meta.function.x87_constant_count = (uint32_t)value; meta.function.x87_constants = at != 0 ? tier0_test_scan_x87_constants : 0; break;
    default: meta.calls[at].result.part_bytes = (uint8_t)value; break;
    }
    tier0_test_meta_stream(&meta);
    const bool vector = tier0_validate_accepts(&meta.function);
    const Tier0Check scalar = tier0_validate_scalar(&meta.function);
    taken[0] += vector ? 1 : 0;
    taken[1] += scalar.error == TIER0_OK ? 1 : 0;
    tier0_test_expect(tally, vector == (scalar.error == TIER0_OK), "validation", "metadata (%s) field %u [%u] = %" PRIu64 ": the vector path %s, the scalar validator %s (%s at %u)",
                      objects ? "with an object" : "plain", field, at, value, vector ? "takes it" : "leaves it", scalar.error == TIER0_OK ? "takes it" : "refuses it",
                      scalar.rule != 0 ? scalar.rule : "", scalar.node);
    return scalar.error == TIER0_OK;
}

static void tier0_test_scan_metadata(Tier0TestTally* tally, uint64_t* taken)
{
    static const uint64_t sizes[] = { 0, 1, 2, 3, 4, 7, 8, 15, 16, 17, 1u << 20, 1u << 30, 0xffffffffu };
    static const uint64_t alignments[] = { 0, 2, 4, 5, 12, 30, 31, 63, 64 };
    static const uint64_t registers[] = { BUSTER_ABI_RDI, BUSTER_ABI_RSI, BUSTER_ABI_RDX, BUSTER_ABI_RAX, 10, 15, 16, BUSTER_ABI_REGISTER_VECTOR | 0, BUSTER_ABI_REGISTER_VECTOR | 7,
                                          BUSTER_ABI_REGISTER_VECTOR | 8, BUSTER_ABI_REGISTER_VECTOR | 16, BUSTER_ABI_REGISTER_X87, BUSTER_ABI_REGISTER_NONE };
    static const uint64_t offsets[] = { 0, 4, 8, 12, 16, 24, 32, 1u << 31 };
    for (uint32_t objects = 0; objects < 2; objects += 1)
    {
        // The base tables, local 0's class as it is: valid, which the vector path must take too.
        tier0_test_expect(tally, tier0_test_meta_check(tally, objects != 0, 0, 0, BUSTER_TYPED_CLASS_I32, taken), "validation", "metadata (%s): the base tables are refused",
                          objects != 0 ? "with an object" : "plain");
        for (uint32_t l = 0; l < BUSTER_TIER0_TEST_SCAN_LOCALS; l += 1)
        {
            for (uint64_t v = 0; v < BUSTER_TYPED_CLASS_LIMIT + 2; v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 0, l, v, taken);
                tier0_test_meta_check(tally, objects != 0, 1, l, v < 16 ? v : v == 16 ? 0x10 : 0x80, taken);
            }
            for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(sizes); v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 2, l, sizes[v], taken);
            }
            for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(alignments); v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 3, l, alignments[v], taken);
            }
        }
        for (uint32_t c = 0; c < BUSTER_TIER0_TEST_SCAN_CALLS; c += 1)
        {
            for (uint64_t v = 0; v < 6; v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 4, c, v, taken);
                tier0_test_meta_check(tally, objects != 0, 5, c, v, taken);
            }
            static const uint64_t counts[] = { 0, 1, 2, 3, 4, 5, 64, 65 };
            for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(counts); v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 6, c, counts[v], taken);
            }
            static const uint64_t bytes[] = { 0, 8, 15, 16, 24, 32, 1u << 31 };
            for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(bytes); v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 7, c, bytes[v], taken);
            }
            static const uint64_t log2s[] = { 0, 4, 30, 31 };
            for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(log2s); v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 8, c, log2s[v], taken);
            }
            for (uint64_t v = 0; v < 4; v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 9, c, v, taken);
                tier0_test_meta_check(tally, objects != 0, 22, c, v * 8, taken);
            }
            for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(registers); v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 10, c, registers[v], taken);
            }
        }
        for (uint32_t a = 0; a < BUSTER_TIER0_TEST_SCAN_ARGUMENTS; a += 1)
        {
            for (uint64_t v = 0; v < 5; v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 11, a, v, taken);
            }
            for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(registers); v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 12, a, registers[v], taken);
            }
            for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(offsets); v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 13, a, offsets[v], taken);
            }
            for (uint64_t v = 0; v < BUSTER_TYPED_CLASS_LIMIT + 1; v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 14, a, v, taken);
            }
        }
        for (uint32_t p = 0; p < 2; p += 1)
        {
            for (uint64_t v = 0; v < 5; v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 15, p, v, taken);
            }
            for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(registers); v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 16, p, registers[v], taken);
            }
            for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(offsets); v += 1)
            {
                tier0_test_meta_check(tally, objects != 0, 17, p, offsets[v], taken);
            }
        }
        for (uint64_t v = 0; v < 4; v += 1)
        {
            tier0_test_meta_check(tally, objects != 0, 18, 0, v, taken);
            tier0_test_meta_check(tally, objects != 0, 20, 0, v, taken);
            tier0_test_meta_check(tally, objects != 0, 21, v & 1, v / 2, taken);
        }
        for (uint32_t v = 0; v < BUSTER_ARRAY_LENGTH(registers); v += 1)
        {
            tier0_test_meta_check(tally, objects != 0, 19, 0, registers[v], taken);
        }
    }
}

// The validation's checks but the programs' (tier0_test_build's): the entries' matrix with and without the memory
// classes, the arrivals and the blocks, in place and at a page's end, and the large function.
static void tier0_test_validation(Tier0TestTally* tally)
{
    uint64_t checked = 0;
    uint64_t taken[2] = { 0, 0 };
    tier0_test_scan_entries(tally, true, &checked, taken);
    tier0_test_scan_entries(tally, false, &checked, taken);
    Arena arena = arena_reserve(1ull << 30);
    Tier0TestStream stream;
    stream.capacity = 1u << 14;
    stream.count = 0;
    stream.kinds = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, stream.capacity);
    stream.classes = BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, stream.capacity);
    stream.values = BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, stream.capacity);
    // One readable page and one guard per stream column.
    uint8_t* pages = mmap(0, 6 * BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool guarded = pages != MAP_FAILED;
    for (uint32_t column = 0; guarded && column < 3; column += 1)
    {
        guarded = mprotect(pages + (column * 2 + 1) * BUSTER_TIER0_TEST_PAGE, BUSTER_TIER0_TEST_PAGE, PROT_NONE) == 0;
    }
    if (tier0_test_expect(tally, stream.values != 0 && guarded, "validation", "no memory or no guard page for the streams"))
    {
        uint8_t* guard = pages + BUSTER_TIER0_TEST_PAGE;
        tier0_test_scan_arrivals(tally, &stream, guard, taken);
        tier0_test_scan_blocks(tally, &stream, guard, taken);
        tier0_test_scan_review_regressions(tally, &stream, guard, taken);
        tier0_test_scan_large(tally, &stream, &arena, 200, 200, taken);
        tier0_test_scan_large(tally, &stream, &arena, 100, 100, taken);
        tier0_test_scan_metadata(tally, taken);
    }
    if (pages != MAP_FAILED)
    {
        munmap(pages, 6 * BUSTER_TIER0_TEST_PAGE);
    }
    arena_release(&arena);
    printf("validation: %" PRIu64 " entries' streams; of every stream, %" PRIu64 " taken by the vector path and %" PRIu64 " by the scalar validator\n", checked, taken[0],
           taken[1]);
}
