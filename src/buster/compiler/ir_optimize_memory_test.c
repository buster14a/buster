// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Value numbering with memory's generated programs (#2718), included by ir_optimize_text_test.c, whose parser it uses;
// run_ir_optimize_tests calls ir_optimize_memory_tests. Each program is generated as IR text: blocks of loads and stores
// of i32s and i64s at constant offsets and indexed places of three slots (one private, two whose addresses escape: the
// first to a call, the second stored in the first), calls and fences, in straight blocks, diamonds and bounded loops,
// every loaded value added to an accumulator in a fourth slot (private) that the function returns. Some addresses
// wrap around int64_t's range back to their slot, which no forwarding may follow. The pass runs alone with dead-code
// removal and in Tier 1's set, by both of the optimizer's paths, whose other passes keep their twins; every result folds
// into a digest frozen where the pass's own twins agreed (IrOptimizeTest.memory_digest, #2924, which deleted the scalar
// one); an independent interpreter runs the
// program before and after it, where each call writes every byte of the escaped slots anew, so a load that took a
// value across a call, a store of its bytes or another write would return something else.

#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)

#define BUSTER_IR_MEMORY_TEST_SLOTS 4u
#define BUSTER_IR_MEMORY_TEST_SLOT_BYTES 64u
#define BUSTER_IR_MEMORY_TEST_BASE UINT64_C(0x1000)
#define BUSTER_IR_MEMORY_TEST_STRIDE UINT64_C(0x100)

typedef struct IrMemoryTestText IrMemoryTestText;
struct IrMemoryTestText
{
    char* text;
    size_t length;
    size_t capacity;
    uint32_t next;
    uint32_t block;
    bool ok;
};

static void ir_memory_test_append(IrMemoryTestText* t, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    char line[256];
    const int written = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    t->ok = t->ok && written > 0 && (size_t)written < sizeof(line) && t->length + (size_t)written < t->capacity;
    if (t->ok)
    {
        memcpy(t->text + t->length, line, (size_t)written);
        t->length += (size_t)written;
        t->text[t->length] = 0;
    }
}

// Appends `%N = ` for the next value and returns N.
static uint32_t ir_memory_test_value(IrMemoryTestText* t)
{
    const uint32_t value = t->next;
    t->next += 1;
    ir_memory_test_append(t, "  %%%u = ", value);
    return value;
}

static uint32_t ir_memory_test_constant(IrMemoryTestText* t, uint64_t constant)
{
    const uint32_t value = ir_memory_test_value(t);
    ir_memory_test_append(t, "const.i64 0x0000000000000000%016" PRIx64 "\n", constant);
    return value;
}

// An access's address: slot `slot`'s (its IR_STACK_ADDR is value 2 + slot) at `offset`, through an index (value 7, the
// first parameter's low bit, times 8), or through a displacement that wraps around int64_t's range back to it.
static uint32_t ir_memory_test_address(IrMemoryTestText* t, IrOptimizeTest* test, uint32_t slot, uint32_t offset)
{
    const uint32_t form = (uint32_t)(ir_optimize_test_random(test) % 6);
    uint32_t value = 0;
    if (form < 3)
    {
        value = ir_memory_test_value(t);
        ir_memory_test_append(t, "address.ptr %%%u offset=%u\n", 2 + slot, offset);
    }
    else if (form < 5)
    {
        value = ir_memory_test_value(t);
        ir_memory_test_append(t, "address.ptr %%%u + %%7 * 8 offset=%u\n", 2 + slot, offset);
    }
    else
    {
        const uint32_t wide = ir_memory_test_value(t);
        ir_memory_test_append(t, "address.ptr %%%u offset=9223372036854775807\n", 2 + slot);
        value = ir_memory_test_value(t);
        ir_memory_test_append(t, "address.ptr %%%u offset=%" PRId64 "\n", wide, (int64_t)offset - INT64_MAX);
    }
    return value;
}

// A block's operations: loads into the accumulator (slot 3 at offset 0), stores of the first parameter plus a constant,
// calls and fences.
static void ir_memory_test_operations(IrMemoryTestText* t, IrOptimizeTest* test)
{
    const uint32_t count = (uint32_t)(ir_optimize_test_random(test) % 6);
    for (uint32_t k = 0; k < count; k += 1)
    {
        const uint32_t choice = (uint32_t)(ir_optimize_test_random(test) % 10);
        const uint32_t slot = (uint32_t)(ir_optimize_test_random(test) % 3);
        const bool wide = ir_optimize_test_random(test) % 2 == 0;
        const uint32_t offset = wide ? (uint32_t)(ir_optimize_test_random(test) % 3) * 8 : (uint32_t)(ir_optimize_test_random(test) % 5) * 4;
        if (choice < 4)
        {
            const uint32_t address = ir_memory_test_address(t, test, slot, offset);
            uint32_t loaded = ir_memory_test_value(t);
            ir_memory_test_append(t, "load.%s %%%u align=4\n", wide ? "i64" : "i32", address);
            if (!wide)
            {
                const uint32_t extended = ir_memory_test_value(t);
                ir_memory_test_append(t, "zext.i64 %%%u\n", loaded);
                loaded = extended;
            }
            const uint32_t accumulator = ir_memory_test_value(t);
            ir_memory_test_append(t, "load.i64 %%5 align=8\n");
            const uint32_t sum = ir_memory_test_value(t);
            ir_memory_test_append(t, "add.i64 %%%u, %%%u\n", accumulator, loaded);
            ir_memory_test_value(t);
            ir_memory_test_append(t, "store.void %%5, %%%u align=8\n", sum);
        }
        else if (choice < 8)
        {
            const uint32_t constant = ir_memory_test_constant(t, ir_optimize_test_random(test) % 1000);
            uint32_t stored = ir_memory_test_value(t);
            ir_memory_test_append(t, "add.i64 %%0, %%%u\n", constant);
            if (!wide)
            {
                const uint32_t narrow = ir_memory_test_value(t);
                ir_memory_test_append(t, "trunc.i32 %%%u\n", stored);
                stored = narrow;
            }
            const uint32_t address = ir_memory_test_address(t, test, slot, offset);
            ir_memory_test_value(t);
            ir_memory_test_append(t, "store.void %%%u, %%%u align=4\n", address, stored);
        }
        else if (choice == 8)
        {
            ir_memory_test_value(t);
            ir_memory_test_append(t, "call.void @2() fixed=() variadic=0\n");
        }
        else
        {
            ir_memory_test_value(t);
            ir_memory_test_append(t, "fence.void seq_cst\n");
        }
    }
}

// A branch on the second parameter's bit `bit`.
static uint32_t ir_memory_test_condition(IrMemoryTestText* t, uint32_t bit)
{
    const uint32_t mask = ir_memory_test_constant(t, UINT64_C(1) << bit);
    const uint32_t masked = ir_memory_test_value(t);
    ir_memory_test_append(t, "and.i64 %%1, %%%u\n", mask);
    const uint32_t zero = ir_memory_test_constant(t, 0);
    const uint32_t condition = ir_memory_test_value(t);
    ir_memory_test_append(t, "icmp.i1 %%%u, %%%u ne\n", masked, zero);
    return condition;
}

// A generated program's text: an entry block, then regions in turn (a straight block, a diamond, or a loop of at most
// two iterations), each starting a new block that the previous one jumps to, then the return of the accumulator.
static bool ir_memory_test_program(IrOptimizeTest* test, char* text, size_t capacity)
{
    IrMemoryTestText t = { .text = text, .capacity = capacity, .ok = true };
    ir_memory_test_append(&t, "function (i64, i64) -> i64 {\n");
    for (uint32_t slot = 0; slot < BUSTER_IR_MEMORY_TEST_SLOTS; slot += 1)
    {
        ir_memory_test_append(&t, "  slot $%u size=%u align=8 type=void flags=0x00\n", slot, BUSTER_IR_MEMORY_TEST_SLOT_BYTES);
    }
    ir_memory_test_append(&t, "b0:\n");
    ir_memory_test_value(&t);
    ir_memory_test_append(&t, "param.i64 0\n");
    ir_memory_test_value(&t);
    ir_memory_test_append(&t, "param.i64 1\n");
    for (uint32_t slot = 0; slot < BUSTER_IR_MEMORY_TEST_SLOTS; slot += 1)
    {
        ir_memory_test_value(&t);
        ir_memory_test_append(&t, "stack_addr.ptr $%u\n", slot);
    }
    // %6 = 1, %7 = the first parameter's low bit, the index; slot 1 escapes to a call, slot 2 into slot 1.
    ir_memory_test_constant(&t, 1);
    ir_memory_test_value(&t);
    ir_memory_test_append(&t, "and.i64 %%0, %%6\n");
    ir_memory_test_value(&t);
    ir_memory_test_append(&t, "call.void @1(%%3) fixed=(ptr) variadic=0\n");
    const uint32_t escaped = ir_memory_test_value(&t);
    ir_memory_test_append(&t, "ptrtoint.i64 %%4\n");
    const uint32_t place = ir_memory_test_value(&t);
    ir_memory_test_append(&t, "address.ptr %%3 offset=56\n");
    ir_memory_test_value(&t);
    ir_memory_test_append(&t, "store.void %%%u, %%%u align=8\n", place, escaped);
    const uint32_t zero = ir_memory_test_constant(&t, 0);
    ir_memory_test_value(&t);
    ir_memory_test_append(&t, "store.void %%5, %%%u align=8\n", zero);
    ir_memory_test_operations(&t, test);
    const uint32_t regions = 1 + (uint32_t)(ir_optimize_test_random(test) % 4);
    for (uint32_t region = 0; region < regions; region += 1)
    {
        const uint32_t shape = (uint32_t)(ir_optimize_test_random(test) % 3);
        const uint32_t head = t.block + 1;
        if (shape == 0)
        {
            ir_memory_test_value(&t);
            ir_memory_test_append(&t, "br.void b%u\nb%u:\n", head, head);
            t.block = head;
            ir_memory_test_operations(&t, test);
        }
        else if (shape == 1)
        {
            const uint32_t condition = ir_memory_test_condition(&t, region);
            ir_memory_test_value(&t);
            ir_memory_test_append(&t, "cbr.void %%%u, b%u, b%u\nb%u:\n", condition, head, head + 1, head);
            ir_memory_test_operations(&t, test);
            ir_memory_test_value(&t);
            ir_memory_test_append(&t, "br.void b%u\nb%u:\n", head + 2, head + 1);
            ir_memory_test_operations(&t, test);
            ir_memory_test_value(&t);
            ir_memory_test_append(&t, "br.void b%u\nb%u:\n", head + 2, head + 2);
            t.block = head + 2;
            ir_memory_test_operations(&t, test);
        }
        else
        {
            // b(head) is the header: a counter from 0 below 2, the latch b(head + 1), the exit b(head + 2).
            const uint32_t two = ir_memory_test_constant(&t, 2);
            ir_memory_test_value(&t);
            ir_memory_test_append(&t, "br.void b%u\nb%u:\n", head, head);
            // The latch's increment is numbered once the header's operations are: a placeholder takes its place.
            const uint32_t counter = ir_memory_test_value(&t);
            const size_t phi_at = t.length;
            ir_memory_test_append(&t, "phi.i64 [%%%u, b%u], [%%PLACEHOLDER, b%u]\n", zero, head - 1, head + 1);
            ir_memory_test_operations(&t, test);
            const uint32_t below = ir_memory_test_value(&t);
            ir_memory_test_append(&t, "icmp.i1 %%%u, %%%u ult\n", counter, two);
            ir_memory_test_value(&t);
            ir_memory_test_append(&t, "cbr.void %%%u, b%u, b%u\nb%u:\n", below, head + 1, head + 2, head + 1);
            ir_memory_test_operations(&t, test);
            const uint32_t next = ir_memory_test_value(&t);
            ir_memory_test_append(&t, "add.i64 %%%u, %%6\n", counter);
            ir_memory_test_value(&t);
            ir_memory_test_append(&t, "br.void b%u\nb%u:\n", head, head + 2);
            t.block = head + 2;
            // Fill the placeholder in with the increment's number, padded to the placeholder's width.
            char* hole = t.ok ? strstr(text + phi_at, "%PLACEHOLDER") : 0;
            t.ok = t.ok && hole != 0;
            if (t.ok)
            {
                char number[16];
                const int digits = snprintf(number, sizeof(number), "%%%u", next);
                memmove(hole + digits, hole + 12, t.length - (size_t)(hole + 12 - text) + 1);
                memcpy(hole, number, (size_t)digits);
                t.length -= (size_t)(12 - digits);
            }
            ir_memory_test_operations(&t, test);
        }
    }
    const uint32_t result = ir_memory_test_value(&t);
    ir_memory_test_append(&t, "load.i64 %%5 align=8\n");
    ir_memory_test_value(&t);
    ir_memory_test_append(&t, "ret.void %%%u\n}\n", result);
    return t.ok;
}

typedef struct IrMemoryTestRun IrMemoryTestRun;
struct IrMemoryTestRun
{
    uint64_t value;
    uint32_t status;
};

// The independent interpreter of the generated programs' operations: slots at BUSTER_IR_MEMORY_TEST_BASE apart by
// BUSTER_IR_MEMORY_TEST_STRIDE, little-endian bytes, wrapping address arithmetic, and each call writing every byte of the
// escaped slots (1 and 2) anew. Status 1 returned, 2 an access out of the slots, 3 the step limit, 4 an operation it
// doesn't know.
static IrMemoryTestRun ir_memory_test_run(const IrFunction* f, uint64_t first, uint64_t second, uint64_t* values)
{
    uint8_t memory[BUSTER_IR_MEMORY_TEST_SLOTS][BUSTER_IR_MEMORY_TEST_SLOT_BYTES];
    memset(memory, 0, sizeof(memory));
    IrMemoryTestRun run = { 0 };
    uint32_t block = 0;
    uint32_t predecessor = BUSTER_IR_NONE;
    uint32_t calls = 0;
    bool active = f->block_count != 0;
    for (uint32_t step = 0; active && step < 100000; step += 1)
    {
        uint32_t target = BUSTER_IR_NONE;
        uint64_t phis[16];
        uint32_t phi_count = 0;
        for (uint32_t i = f->block_offsets[block]; active && i < f->block_offsets[(uint64_t)block + 1] && f->op[i] == IR_PHI; i += 1)
        {
            bool found = false;
            for (uint32_t incoming = 0; incoming < f->b[i]; incoming += 1)
            {
                if (f->extra[f->a[i] + incoming * 2] == predecessor)
                {
                    phis[phi_count] = values[f->extra[f->a[i] + incoming * 2 + 1]];
                    found = true;
                }
            }
            active = found && phi_count < 16;
            run.status = active ? run.status : 4;
            phi_count += 1;
        }
        for (uint32_t k = 0; active && k < phi_count; k += 1)
        {
            values[f->block_offsets[block] + k] = phis[k];
        }
        for (uint32_t i = f->block_offsets[block] + phi_count; active && i < f->block_offsets[(uint64_t)block + 1]; i += 1)
        {
            const uint32_t a = f->a[i];
            const uint32_t b = f->b[i];
            uint64_t value = 0;
            switch (f->op[i])
            {
                case IR_PARAM: value = a == 0 ? first : second; break;
                case IR_CONST: value = f->constants[a].low; break;
                case IR_ADD: value = values[a] + values[b]; break;
                case IR_AND: value = values[a] & values[b]; break;
                case IR_TRUNC: value = values[a]; break;
                case IR_ZEXT: value = values[a]; break;
                case IR_PTRTOINT: value = values[a]; break;
                case IR_ICMP: value = f->c[i] == IR_NE ? values[a] != values[b] : f->c[i] == IR_ULT ? values[a] < values[b] : 0; break;
                case IR_STACK_ADDR: value = BUSTER_IR_MEMORY_TEST_BASE + a * BUSTER_IR_MEMORY_TEST_STRIDE; break;
                case IR_ADDRESS: value = values[a] + (b == BUSTER_IR_NONE ? 0 : values[b] * f->addresses[f->c[i]].scale) + (uint64_t)f->addresses[f->c[i]].offset; break;
                case IR_LOAD:
                case IR_STORE:
                {
                    const uint64_t address = values[a];
                    const uint32_t bytes = (ir_type_bits[f->op[i] == IR_LOAD ? f->type[i] : f->type[b]] + 7) / 8;
                    const uint64_t slot = (address - BUSTER_IR_MEMORY_TEST_BASE) / BUSTER_IR_MEMORY_TEST_STRIDE;
                    const uint64_t at = (address - BUSTER_IR_MEMORY_TEST_BASE) % BUSTER_IR_MEMORY_TEST_STRIDE;
                    active = address >= BUSTER_IR_MEMORY_TEST_BASE && slot < BUSTER_IR_MEMORY_TEST_SLOTS && at + bytes <= BUSTER_IR_MEMORY_TEST_SLOT_BYTES;
                    run.status = active ? run.status : 2;
                    for (uint32_t byte = 0; active && byte < bytes; byte += 1)
                    {
                        if (f->op[i] == IR_STORE)
                        {
                            memory[slot][at + byte] = (uint8_t)(values[b] >> (byte * 8));
                        }
                        else
                        {
                            value |= (uint64_t)memory[slot][at + byte] << (byte * 8);
                        }
                    }
                } break;
                case IR_CALL:
                {
                    for (uint32_t slot = 1; slot < 3; slot += 1)
                    {
                        for (uint32_t byte = 0; byte < BUSTER_IR_MEMORY_TEST_SLOT_BYTES; byte += 1)
                        {
                            memory[slot][byte] = (uint8_t)(calls * 37 + slot * 11 + byte * 7 + 1);
                        }
                    }
                    calls += 1;
                } break;
                case IR_FENCE: break;
                case IR_BR: target = a; break;
                case IR_CBR: target = values[a] != 0 ? b : f->c[i]; break;
                case IR_RET:
                {
                    run.value = values[a];
                    run.status = 1;
                    active = false;
                } break;
                default:
                {
                    run.status = 4;
                    active = false;
                } break;
            }
            if (f->type[i] != IR_VOID && ir_type_bits[f->type[i]] <= 64)
            {
                values[i] = value & (UINT64_MAX >> (64 - ir_type_bits[f->type[i]]));
            }
        }
        if (active)
        {
            predecessor = block;
            block = target;
            active = target < f->block_count;
            run.status = active ? run.status : 4;
        }
    }
    run.status = active ? 3 : run.status;
    return run;
}

// One generated program through the optimizer with `passes`: a function that verifies, and the same runs.
static void ir_memory_test_check(IrOptimizeTest* test, const IrFunction* f, uint32_t passes, Arena* vector, Arena* scratch, uint64_t* values, uint64_t* forwarded)
{
    IrOptimizeOptions options = ir_optimize_options();
    options.passes = passes;
    IrOptimizeResult vector_result = { 0 };
    const IrCheck vector_check = ir_optimize(f, vector, scratch, options, &vector_result);
    ir_optimize_test_expect(test, vector_check.error == IR_OK, "memory: the optimizer takes a generated program");
    if (vector_check.error == IR_OK)
    {
        *forwarded += vector_result.forwarded_loads;
        ir_optimize_test_fold_function(&test->memory_digest, &vector_result.function);
        ir_optimize_test_fold(&test->memory_digest, vector_result.forwarded_loads);
        for (uint32_t trial = 0; trial < 6; trial += 1)
        {
            const uint64_t first = ir_optimize_test_random(test);
            const uint64_t second = ir_optimize_test_random(test);
            const IrMemoryTestRun before = ir_memory_test_run(f, first, second, values);
            const IrMemoryTestRun after = ir_memory_test_run(&vector_result.function, first, second, values);
            ir_optimize_test_expect(test, before.status == 1 && after.status == 1 && before.value == after.value, "memory: a generated program returns what it did");
        }
    }
}

// Forwarding's arithmetic at the edges of int64_t: an address whose constant index or displacement sum overflows stops
// there, and a pair whose wrapped offsets meet again isn't forwarded through the overflow.
static void ir_memory_test_overflows(IrOptimizeTest* test, Arena* input, Arena* vector, Arena* scratch)
{
    static const char text[] =
        "function (i64) -> i64 {\n"
        "  slot $0 size=16 align=8 type=void flags=0x00\n"
        "b0:\n"
        "  %0 = param.i64 0\n"
        "  %1 = stack_addr.ptr $0\n"
        "  %2 = call.void @1(%1) fixed=(ptr) variadic=0\n"
        "  %3 = const.i64 0x00000000000000004000000000000000\n"
        "  %4 = address.ptr %1 + %3 * 4 offset=0\n"
        "  %5 = address.ptr %4 offset=8\n"
        "  %6 = address.ptr %1 offset=9223372036854775807\n"
        "  %7 = address.ptr %6 offset=9223372036854775807\n"
        "  %8 = address.ptr %7 offset=2\n"
        "  %9 = const.i64 0x00000000000000007fffffffffffffff\n"
        "  %10 = ptrtoint.i64 %1\n"
        "  %11 = add.i64 %10, %9\n"
        "  %12 = add.i64 %11, %9\n"
        "  %13 = const.i64 0x00000000000000000000000000000002\n"
        "  %14 = add.i64 %12, %13\n"
        "  %15 = inttoptr.ptr %14\n"
        "  %16 = store.void %1, %0 align=8\n"
        "  %17 = load.i64 %8 align=8\n"
        "  %18 = load.i64 %15 align=8\n"
        "  %19 = load.i64 %15 align=8\n"
        "  %20 = load.i64 %5 align=8\n"
        "  %21 = add.i64 %17, %18\n"
        "  %22 = add.i64 %21, %19\n"
        "  %23 = add.i64 %22, %20\n"
        "  %24 = ret.void %23\n"
        "}\n";
    const uint64_t mark = input->length;
    IrFunction f = { 0 };
    const String8 source = { .pointer = (char*)text, .length = sizeof(text) - 1 };
    const IrParseResult parsed = ir_parse(source, input, scratch, &f);
    ir_optimize_test_expect(test, parsed.error == IR_PARSE_OK, "memory: the overflow fixture parses");
    if (parsed.error == IR_PARSE_OK)
    {
        IrOptimizeOptions options = ir_optimize_options();
        options.passes = BUSTER_IR_OPTIMIZE_MEMORY;
        IrOptimizeResult result = { 0 };
        const uint64_t output_mark = vector->length;
        const IrCheck check = ir_optimize(&f, vector, scratch, options, &result);
        // Only %19 takes %18's value: the wrapped addresses keep their roots where an offset overflowed (%7 and %12, and
        // %4 for %5, whose constant index's product overflows), so none meets the store at offset 0 or another's key.
        ir_optimize_test_expect(test, check.error == IR_OK && result.forwarded_loads == 1, "memory: an overflowing offset stops the address there");
        if (check.error == IR_OK)
        {
            ir_optimize_test_fold_function(&test->memory_digest, &result.function);
        }
        arena_pop_to(vector, output_mark);
    }
    arena_pop_to(input, mark);
}

// Value numbering with memory's shapes that the generated programs miss (#2924, found by mutants of its vector phases):
// an address added to a constant that comes first; a slot whose address reaches a call three steps down, or from a
// batch's last lane, which escapes it; and two slots whose only addresses are in a batch's last lane, which stay apart.
// Each runs through the pass with its forwarded loads counted by the pass's definition, the last on the interpreter too,
// and folds into the digest.
static void ir_memory_test_shapes(IrOptimizeTest* test, Arena* input, Arena* output, Arena* scratch, uint64_t* values)
{
    static const char added[] =
        "function (i64) -> i64 {\n"
        "  slot $0 size=16 align=8 type=void flags=0x00\n"
        "b0:\n"
        "  %0 = param.i64 0\n"
        "  %1 = stack_addr.ptr $0\n"
        "  %2 = ptrtoint.i64 %1\n"
        "  %3 = const.i64 0x00000000000000000000000000000008\n"
        "  %4 = add.i64 %3, %2\n"
        "  %5 = inttoptr.ptr %4\n"
        "  %6 = store.void %5, %0 align=8\n"
        "  %7 = add.i64 %2, %3\n"
        "  %8 = inttoptr.ptr %7\n"
        "  %9 = load.i64 %8 align=8\n"
        "  %10 = ret.void %9\n"
        "}\n";
    static const char chained[] =
        "function (i64) -> i64 {\n"
        "  slot $0 size=64 align=8 type=void flags=0x00\n"
        "b0:\n"
        "  %0 = param.i64 0\n"
        "  %1 = stack_addr.ptr $0\n"
        "  %2 = address.ptr %1 offset=8\n"
        "  %3 = address.ptr %2 offset=8\n"
        "  %4 = address.ptr %3 offset=8\n"
        "  %5 = store.void %1, %0 align=8\n"
        "  %6 = call.void @1(%4) fixed=(ptr) variadic=0\n"
        "  %7 = load.i64 %1 align=8\n"
        "  %8 = ret.void %7\n"
        "}\n";
    static const char escaped[] =
        "function (i64) -> i64 {\n"
        "  slot $0 size=16 align=8 type=void flags=0x00\n"
        "b0:\n"
        "  %0 = param.i64 0\n"
        "  %1 = stack_addr.ptr $0\n"
        "  %2 = const.i64 0x00000000000000000000000000000002\n"
        "  %3 = const.i64 0x00000000000000000000000000000003\n"
        "  %4 = const.i64 0x00000000000000000000000000000004\n"
        "  %5 = const.i64 0x00000000000000000000000000000005\n"
        "  %6 = const.i64 0x00000000000000000000000000000006\n"
        "  %7 = const.i64 0x00000000000000000000000000000007\n"
        "  %8 = const.i64 0x00000000000000000000000000000008\n"
        "  %9 = const.i64 0x00000000000000000000000000000009\n"
        "  %10 = const.i64 0x0000000000000000000000000000000a\n"
        "  %11 = const.i64 0x0000000000000000000000000000000b\n"
        "  %12 = const.i64 0x0000000000000000000000000000000c\n"
        "  %13 = const.i64 0x0000000000000000000000000000000d\n"
        "  %14 = const.i64 0x0000000000000000000000000000000e\n"
        "  %15 = address.ptr %1 offset=8\n"
        "  %16 = store.void %1, %0 align=8\n"
        "  %17 = call.void @1(%15) fixed=(ptr) variadic=0\n"
        "  %18 = load.i64 %1 align=8\n"
        "  %19 = ret.void %18\n"
        "}\n";
    static const char apart[] =
        "function (i64, i64) -> i64 {\n"
        "  slot $0 size=16 align=8 type=void flags=0x00\n"
        "  slot $1 size=16 align=8 type=void flags=0x00\n"
        "b0:\n"
        "  %0 = param.i64 0\n"
        "  %1 = param.i64 1\n"
        "  %2 = const.i64 0x00000000000000000000000000000002\n"
        "  %3 = const.i64 0x00000000000000000000000000000003\n"
        "  %4 = const.i64 0x00000000000000000000000000000004\n"
        "  %5 = const.i64 0x00000000000000000000000000000005\n"
        "  %6 = const.i64 0x00000000000000000000000000000006\n"
        "  %7 = const.i64 0x00000000000000000000000000000007\n"
        "  %8 = const.i64 0x00000000000000000000000000000008\n"
        "  %9 = const.i64 0x00000000000000000000000000000009\n"
        "  %10 = const.i64 0x0000000000000000000000000000000a\n"
        "  %11 = const.i64 0x0000000000000000000000000000000b\n"
        "  %12 = const.i64 0x0000000000000000000000000000000c\n"
        "  %13 = const.i64 0x0000000000000000000000000000000d\n"
        "  %14 = const.i64 0x0000000000000000000000000000000e\n"
        "  %15 = stack_addr.ptr $0\n"
        "  %16 = store.void %15, %0 align=8\n"
        "  %17 = const.i64 0x00000000000000000000000000000011\n"
        "  %18 = const.i64 0x00000000000000000000000000000012\n"
        "  %19 = const.i64 0x00000000000000000000000000000013\n"
        "  %20 = const.i64 0x00000000000000000000000000000014\n"
        "  %21 = const.i64 0x00000000000000000000000000000015\n"
        "  %22 = const.i64 0x00000000000000000000000000000016\n"
        "  %23 = const.i64 0x00000000000000000000000000000017\n"
        "  %24 = const.i64 0x00000000000000000000000000000018\n"
        "  %25 = const.i64 0x00000000000000000000000000000019\n"
        "  %26 = const.i64 0x0000000000000000000000000000001a\n"
        "  %27 = const.i64 0x0000000000000000000000000000001b\n"
        "  %28 = const.i64 0x0000000000000000000000000000001c\n"
        "  %29 = const.i64 0x0000000000000000000000000000001d\n"
        "  %30 = const.i64 0x0000000000000000000000000000001e\n"
        "  %31 = stack_addr.ptr $1\n"
        "  %32 = store.void %31, %1 align=8\n"
        "  %33 = load.i64 %15 align=8\n"
        "  %34 = ret.void %33\n"
        "}\n";
    const char* const texts[] = {
        added,
        chained,
        escaped,
        apart,
    };
    const uint32_t expected[] = {
        1,
        0,
        0,
        1,
    };
    static const char* const descriptions[] = {
        "memory: an address added to a constant that comes first meets the same address",
        "memory: a slot whose address reaches a call three steps down escapes",
        "memory: a slot whose address escapes from a batch's last lane escapes",
        "memory: two slots whose only addresses are in a batch's last lane stay apart",
    };
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(texts); k += 1)
    {
        const uint64_t mark = input->length;
        const uint64_t output_mark = output->length;
        IrFunction f = { 0 };
        const String8 source = { .pointer = (char*)texts[k], .length = strlen(texts[k]) };
        const IrParseResult parsed = ir_parse(source, input, scratch, &f);
        ir_optimize_test_expect(test, parsed.error == IR_PARSE_OK, "memory: a shape's fixture parses");
        if (parsed.error == IR_PARSE_OK)
        {
            IrOptimizeOptions options = ir_optimize_options();
            options.passes = BUSTER_IR_OPTIMIZE_MEMORY;
            IrOptimizeResult result = { 0 };
            const IrCheck check = ir_optimize(&f, output, scratch, options, &result);
            bool same = check.error == IR_OK && result.forwarded_loads == expected[k];
            if (same && k == 3)
            {
                const IrMemoryTestRun before = ir_memory_test_run(&f, 5, 9, values);
                const IrMemoryTestRun after = ir_memory_test_run(&result.function, 5, 9, values);
                same = before.status == 1 && after.status == 1 && before.value == 5 && after.value == 5;
            }
            ir_optimize_test_expect(test, same, descriptions[k]);
            if (check.error == IR_OK)
            {
                ir_optimize_test_fold_function(&test->memory_digest, &result.function);
                ir_optimize_test_fold(&test->memory_digest, result.forwarded_loads);
            }
        }
        arena_pop_to(output, output_mark);
        arena_pop_to(input, mark);
    }
}

// The clear analysis and rollback must preserve both the available values and the decisions to keep loads (#2826).
// These checks have their own explicit forwarding counts and semantic comparisons, outside the older frozen digest
// and random stream. Running with and without DCE also keeps an unused probe from hiding an incorrect decision.
static void ir_memory_test_scope_check(IrOptimizeTest* test, const char* name, const IrFunction* f, uint32_t expected, Arena* output, Arena* scratch)
{
    for (uint32_t dce = 0; dce < 2; dce += 1)
    {
        const uint64_t output_mark = output->length;
        const uint64_t scratch_mark = scratch->length;
        IrOptimizeOptions options = ir_optimize_options();
        options.passes = BUSTER_IR_OPTIMIZE_MEMORY | (dce != 0 ? BUSTER_IR_OPTIMIZE_DCE : 0);
        IrOptimizeResult result = { 0 };
        const IrCheck check = ir_optimize(f, output, scratch, options, &result);
        ir_optimize_text_expect(test, check.error == IR_OK, name, "the scope fixture optimizes");
        ir_optimize_text_expect(test, scratch->length == scratch_mark, name, "the pass restores scratch");
        if (check.error == IR_OK)
        {
            if (result.forwarded_loads != expected && test->failures < 8)
            {
                fprintf(stderr, "ir-optimize: %s: forwarded %u loads, expected %u\n", name, result.forwarded_loads, expected);
            }
            ir_optimize_text_expect(test, result.forwarded_loads == expected, name, "the exact safe loads forward");
            ir_optimize_text_execute(test, name, f, &result.function, scratch, true);
        }
        arena_pop_to(output, output_mark);
    }
}

// A long dominator arm has just one writer, in its middle; the other arm reads its parent's values after the first
// subtree has been undone. At their join only the written memory kind loses its inherited value. A single arm also
// checks the childless and one-child paths, where no sibling needs a rollback. Lengths cross the vector batch boundary.
static bool ir_memory_test_scope_program(char* text, size_t capacity, uint32_t length, uint32_t mode, bool diamond)
{
    IrMemoryTestText t = {
        .text = text,
        .capacity = capacity,
        .ok = true,
    };
    ir_memory_test_append(&t, "function (i64, i64) -> i64 {\n");
    for (uint32_t slot = 0; slot < 2; slot += 1)
    {
        ir_memory_test_append(&t, "  slot $%u size=16 align=8 type=void flags=0x00\n", slot);
    }
    ir_memory_test_append(&t, "b0:\n");
    for (uint32_t parameter = 0; parameter < 2; parameter += 1)
    {
        ir_memory_test_value(&t);
        ir_memory_test_append(&t, "param.i64 %u\n", parameter);
    }
    for (uint32_t slot = 0; slot < 2; slot += 1)
    {
        ir_memory_test_value(&t);
        ir_memory_test_append(&t, "stack_addr.ptr $%u\n", slot);
    }
    ir_memory_test_value(&t);
    ir_memory_test_append(&t, "call.void @1(%%3) fixed=(ptr) variadic=0\n");
    const uint32_t zero = ir_memory_test_constant(&t, 0);
    for (uint32_t slot = 0; slot < 2; slot += 1)
    {
        ir_memory_test_value(&t);
        ir_memory_test_append(&t, "store.void %%%u, %%0 align=8\n", 2 + slot);
    }
    if (diamond)
    {
        const uint32_t condition = ir_memory_test_condition(&t, 0);
        ir_memory_test_value(&t);
        ir_memory_test_append(&t, "cbr.void %%%u, b1, b%u\n", condition, length + 1);
    }
    else
    {
        ir_memory_test_value(&t);
        ir_memory_test_append(&t, "br.void b1\n");
    }
    uint32_t sum = zero;
    for (uint32_t step = 0; step < length; step += 1)
    {
        const uint32_t block = step + 1;
        ir_memory_test_append(&t, "b%u:\n", block);
        if (step == length / 2)
        {
            if (mode == 1 || mode == 7)
            {
                ir_memory_test_value(&t);
                ir_memory_test_append(&t, "store.void %%3, %%1 align=8\n");
            }
            if (mode == 2 || mode == 7)
            {
                ir_memory_test_value(&t);
                ir_memory_test_append(&t, "store.void %%2, %%1 align=8\n");
            }
            if (mode >= 3 && mode <= 6)
            {
                ir_memory_test_value(&t);
                if (mode == 3)
                {
                    ir_memory_test_append(&t, "call.void @2() fixed=() variadic=0\n");
                }
                else if (mode == 4)
                {
                    ir_memory_test_append(&t, "fence.void seq_cst\n");
                }
                else
                {
                    ir_memory_test_append(&t, "lifetime.void $%u start\n", mode - 5);
                }
            }
        }
        for (uint32_t slot = 0; slot < 2; slot += 1)
        {
            const uint32_t loaded = ir_memory_test_value(&t);
            ir_memory_test_append(&t, "load.i64 %%%u align=8\n", 2 + slot);
            const uint32_t added = ir_memory_test_value(&t);
            ir_memory_test_append(&t, "add.i64 %%%u, %%%u\n", sum, loaded);
            sum = added;
        }
        ir_memory_test_value(&t);
        if (step + 1 < length || diamond)
        {
            ir_memory_test_append(&t, "br.void b%u\n", step + 1 < length ? block + 1 : length + 2);
        }
        else
        {
            ir_memory_test_append(&t, "ret.void %%%u\n", sum);
        }
    }
    if (diamond)
    {
        ir_memory_test_append(&t, "b%u:\n", length + 1);
        const uint32_t private_value = ir_memory_test_value(&t);
        ir_memory_test_append(&t, "load.i64 %%2 align=8\n");
        const uint32_t general_value = ir_memory_test_value(&t);
        ir_memory_test_append(&t, "load.i64 %%3 align=8\n");
        const uint32_t sibling_sum = ir_memory_test_value(&t);
        ir_memory_test_append(&t, "add.i64 %%%u, %%%u\n", private_value, general_value);
        ir_memory_test_value(&t);
        ir_memory_test_append(&t, "br.void b%u\nb%u:\n", length + 2, length + 2);
        const uint32_t joined = ir_memory_test_value(&t);
        ir_memory_test_append(&t, "phi.i64 [%%%u, b%u], [%%%u, b%u]\n", sum, length, sibling_sum, length + 1);
        sum = joined;
        for (uint32_t access = 0; access < 4; access += 1)
        {
            const uint32_t loaded = ir_memory_test_value(&t);
            ir_memory_test_append(&t, "load.i64 %%%u align=8\n", 2 + access % 2);
            const uint32_t added = ir_memory_test_value(&t);
            ir_memory_test_append(&t, "add.i64 %%%u, %%%u\n", sum, loaded);
            sum = added;
        }
        ir_memory_test_value(&t);
        ir_memory_test_append(&t, "ret.void %%%u\n", sum);
    }
    ir_memory_test_append(&t, "}\n");
    return t.ok;
}

static void ir_memory_test_scopes(IrOptimizeTest* test, Arena* input, Arena* output, Arena* scratch, char* text, size_t capacity)
{
    static const uint32_t lengths[] = {
        1,
        2,
        17,
        65,
    };
    for (uint32_t size = 0; size < BUSTER_ARRAY_LENGTH(lengths); size += 1)
    {
        const uint32_t length = lengths[size];
        for (uint32_t mode = 0; mode < 8; mode += 1)
        {
            for (uint32_t diamond = 0; diamond < 2; diamond += 1)
            {
                const uint64_t input_mark = input->length;
                const bool generated = ir_memory_test_scope_program(text, capacity, length, mode, diamond != 0);
                char name[96];
                snprintf(name, sizeof(name), "memory scopes: %u blocks, writer %u, %s", length, mode, diamond != 0 ? "diamond" : "chain");
                IrFunction f = { 0 };
                const String8 source = {
                    .pointer = text,
                    .length = generated ? strlen(text) : 0,
                };
                const bool parsed = generated && ir_optimize_text_parse(source, input, scratch, &f);
                ir_optimize_text_expect(test, parsed, name, "the generated scope fixture parses");
                if (parsed)
                {
                    // A store supplies its own following load; a call, fence or lifetime loses just that kind's
                    // first load. A join loses one first load per written kind, but its two repeated loads forward.
                    const uint32_t lost_at_writer = mode >= 3 && mode <= 6 ? 1u : 0u;
                    const uint32_t lost_at_join = mode == 0 ? 0u : mode == 7 ? 2u : 1u;
                    const uint32_t expected = length * 2 - lost_at_writer + (diamond != 0 ? 6 - lost_at_join : 0);
                    ir_memory_test_scope_check(test, name, &f, expected, output, scratch);
                }
                arena_pop_to(input, input_mark);
            }
        }
    }
    static const char* const names[] = {
        "memory-scope-nested",
        "memory-scope-irreducible",
    };
    static const uint32_t expected[] = {
        // Two each in b1, b2, b3, b4 and b6; three in b5 after its sibling's stores and epochs are undone.
        13,
        // The private loads in both cycle entries, the load just stored in b3, and both loads after b1's exit.
        5,
    };
    for (uint32_t fixture = 0; fixture < BUSTER_ARRAY_LENGTH(names); fixture += 1)
    {
        const uint64_t input_mark = input->length;
        char path[128];
        snprintf(path, sizeof(path), "tests/ir/optimize/%s.ir", names[fixture]);
        String8 source = { 0 };
        IrFunction f = { 0 };
        const bool parsed = ir_optimize_text_read(path, input, &source) && ir_optimize_text_parse(source, input, scratch, &f);
        ir_optimize_text_expect(test, parsed, names[fixture], "the scope fixture reads and parses");
        if (parsed)
        {
            ir_memory_test_scope_check(test, names[fixture], &f, expected[fixture], output, scratch);
        }
        arena_pop_to(input, input_mark);
    }
}

static void ir_optimize_memory_tests(IrOptimizeTest* test)
{
    Arena input = arena_reserve(1ull << 24);
    Arena vector = arena_reserve(1ull << 24);
    Arena scratch = arena_reserve(1ull << 26);
    char* text = malloc(1u << 16);
    uint64_t* values = malloc((1u << 16) * sizeof(uint64_t));
    const bool ready = input.base != 0 && vector.base != 0 && scratch.base != 0 && text != 0 && values != 0;
    ir_optimize_test_expect(test, ready, "memory: test arenas");
    uint64_t forwarded = 0;
    uint32_t programs = 0;
    for (uint32_t k = 0; ready && k < 400; k += 1)
    {
        const bool generated = ir_memory_test_program(test, text, 1u << 16);
        ir_optimize_test_expect(test, generated, "memory: a generated program fits its text");
        IrFunction f = { 0 };
        const String8 source = { .pointer = text, .length = generated ? strlen(text) : 0 };
        const IrParseResult parsed = generated ? ir_parse(source, &input, &scratch, &f) : (IrParseResult){ .error = IR_PARSE_END };
        ir_optimize_test_expect(test, parsed.error == IR_PARSE_OK && f.instruction_count < (1u << 16), "memory: a generated program parses");
        if (parsed.error != IR_PARSE_OK && test->failures < 4)
        {
            fprintf(stderr, "ir-optimize: memory: generated program %u (error %u at %" PRIu64 "):\n%s", k, parsed.error, parsed.consumed, text);
        }
        if (parsed.error == IR_PARSE_OK && f.instruction_count < (1u << 16))
        {
            ir_memory_test_check(test, &f, BUSTER_IR_OPTIMIZE_MEMORY | BUSTER_IR_OPTIMIZE_DCE, &vector, &scratch, values, &forwarded);
            ir_memory_test_check(test, &f, BUSTER_IR_OPTIMIZE_PASSES, &vector, &scratch, values, &forwarded);
            programs += 1;
        }
        arena_pop_to(&input, 0);
        arena_pop_to(&vector, 0);
        ir_optimize_test_expect(test, scratch.length == 0, "memory: the pass restores scratch");
    }
    // The generator exercises the pass: most programs forward something.
    ir_optimize_test_expect(test, !ready || forwarded >= programs, "memory: the generated programs forward loads");
    if (ready)
    {
        ir_memory_test_overflows(test, &input, &vector, &scratch);
        ir_memory_test_shapes(test, &input, &vector, &scratch, values);
        ir_memory_test_scopes(test, &input, &vector, &scratch, text, 1u << 16);
    }
    free(values);
    free(text);
    arena_release(&scratch);
    arena_release(&vector);
    arena_release(&input);
}

#endif
