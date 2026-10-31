// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Real selected-IR pressure fixtures for tier1_emit.c. The integer oracle is independent of the machine plan.
// Both allocation paths execute through the frame helper, branch sizing and encoder in a child process. This file
// tests the bounded backend contract; tier1_pipeline_test.c separately exercises the upstream typed/IR/SSA producer.

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct Tier1EmitTest Tier1EmitTest;
struct Tier1EmitTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t stores;
    uint64_t loads;
    uint64_t copies;
    uint32_t terms;
    uint32_t width;
    bool selects;
    Arena input;
    Arena output;
    Arena scratch;
    uint8_t* allowed;
    const uint8_t* native_allowed;
    uint64_t native_skipped;
};

// A requested output policy may exceed the current CPU. The policy/encoding checks still run; only the native call skips.
static bool tier1_emit_test_native(Tier1EmitTest* test, const X86Instruction* records, uint32_t count)
{
    const bool supported = test->native_allowed == 0 || x86_target_validate(test->native_allowed, records, count) == count;
    test->native_skipped += !supported;
    return supported;
}

static void tier1_emit_test_expect(Tier1EmitTest* test, bool passed, const char* message)
{
    test->checks += 1;
    if (!passed)
    {
        if (test->failures < 20)
        {
            fprintf(stderr, "tier1-emit: %s (terms=%u, width=%u, selects=%u)\n", message, test->terms, test->width, test->selects);
        }
        test->failures += 1;
    }
}

static uint64_t tier1_emit_test_random(uint64_t* random)
{
    *random ^= *random << 13;
    *random ^= *random >> 7;
    *random ^= *random << 17;
    return *random;
}

static void tier1_emit_test_node(IrFunction* function, uint32_t node, IrOp op, IrType type, uint32_t a, uint32_t b)
{
    function->op[node] = (uint8_t)op;
    function->type[node] = (uint8_t)type;
    function->a[node] = a;
    function->b[node] = b;
    function->c[node] = BUSTER_IR_NONE;
}

static bool tier1_emit_test_fixture(Tier1EmitTest* test, IrFunction* function)
{
    const uint32_t terms = test->terms;
    const uint32_t prefix = test->selects ? 4 : 2;
    const uint32_t count = terms * 3 + prefix + 1;
    const IrType type = test->width == 4 ? IR_I32 : IR_I64;
    const IrSizes sizes = { .instruction_count = count, .block_count = 1, .constant_count = terms + test->selects, .parameter_count = 2 };
    bool ok = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ok)
    {
        function->result_type = (uint8_t)type;
        function->parameter_types[0] = (uint8_t)type;
        function->parameter_types[1] = (uint8_t)type;
        function->block_offsets[1] = count;
        tier1_emit_test_node(function, 0, IR_PARAM, (IrType)function->parameter_types[0], 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 1, IR_PARAM, type, 1, BUSTER_IR_NONE);
        if (test->selects)
        {
            tier1_emit_test_node(function, 2, IR_CONST, type, terms, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 3, IR_ICMP, IR_I1, 0, 2);
            function->c[3] = IR_NE;
        }
        uint64_t random = UINT64_C(0x34635a27d768b2f1);
        for (uint32_t term = 0; term < terms; term += 1)
        {
            const uint64_t value = tier1_emit_test_random(&random);
            function->constants[term].low = test->width == 4 ? (uint32_t)value : value;
            const uint32_t node = prefix + term * (test->selects ? 1 : 2);
            tier1_emit_test_node(function, node, IR_CONST, type, term, BUSTER_IR_NONE);
            if (!test->selects)
            {
                tier1_emit_test_node(function, node + 1, IR_XOR, type, 0, node);
            }
        }
        uint32_t sum = 1;
        uint32_t node = prefix + terms * (test->selects ? 1 : 2);
        for (uint32_t term = 0; term < terms; term += 1)
        {
            uint32_t value = 3 + term * 2;
            if (test->selects)
            {
                tier1_emit_test_node(function, node, IR_SELECT, type, 3, prefix + term);
                function->c[node] = 1;
                value = node++;
            }
            tier1_emit_test_node(function, node, IR_ADD, type, sum, value);
            sum = node++;
        }
        tier1_emit_test_node(function, node, IR_RET, IR_VOID, sum, BUSTER_IR_NONE);
        ok = node + 1 == count;
    }
    return ok;
}

static uint64_t tier1_emit_test_expected(const Tier1EmitTest* test, const IrFunction* function, uint64_t a, uint64_t b)
{
    const uint64_t mask = test->width == 4 ? UINT32_MAX : UINT64_MAX;
    uint64_t result = b;
    for (uint32_t term = 0; term < test->terms; term += 1)
    {
        const uint64_t value = function->constants[term].low;
        result += test->selects ? (a != 0 ? value : b) : a ^ value;
    }
    return result & mask;
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_emit_test_call(const void* code, uint64_t a, uint64_t b)
{
    uint64_t (*function)(uint64_t, uint64_t);
    memcpy(&function, &code, sizeof(function));
    return function(a, b);
}

static void tier1_emit_test_execute(Tier1EmitTest* test, const IrFunction* function, Tier1EmitResult* emitted)
{
    Tier1SelectCode* code = &emitted->code;
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    const uint64_t bytes = (capacity + 4095) & ~UINT64_C(4095);
    void* executable = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint32_t* offsets = malloc((uint64_t)code->count * sizeof(uint32_t));
    bool ready = executable != MAP_FAILED && offsets != 0;
    tier1_emit_test_expect(test, ready, "native execution buffer");
    if (ready)
    {
        const BranchSizingCheck checked = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
        const BranchSizing sized = checked.error == BRANCH_SIZING_OK ? size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets) : (BranchSizing){ .error = checked.error };
        ready = sized.error == BRANCH_SIZING_OK;
        tier1_emit_test_expect(test, ready, "frame branches size with body records");
    }
    if (ready)
    {
        // #221: every record of the frame and body has a form x86_semantics.c covers, which the effects' consumers need.
        bool covered = true;
        for (uint32_t r = 0; r < code->count; r += 1)
        {
            covered &= x86_effects(code->records[r].form)->operation != X86_OPERATION_NONE;
        }
        tier1_emit_test_expect(test, covered, "every frame and body record has a covered form");
        ready = x86_target_validate(test->allowed, code->records, code->count) == code->count && x86_encode(code->records, code->count, executable, capacity, 0, 0) > 0;
        tier1_emit_test_expect(test, ready, "baseline encoder accepts complete frame and body");
        ready = ready && mprotect(executable, bytes, PROT_READ | PROT_EXEC) == 0;
        tier1_emit_test_expect(test, ready, "native code made executable");
    }
    if (ready && tier1_emit_test_native(test, code->records, code->count))
    {
        uint64_t random = UINT64_C(0x0a3385510b49decd);
        for (uint32_t sample = 0; sample < 64; sample += 1)
        {
            const uint64_t a = test->selects ? sample & 1 : sample < 4 ? UINT64_MAX >> sample : tier1_emit_test_random(&random);
            const uint64_t b = sample < 4 ? UINT64_MAX << sample : tier1_emit_test_random(&random);
            const uint64_t actual = tier1_emit_test_call(executable, a, b);
            tier1_emit_test_expect(test, actual == tier1_emit_test_expected(test, function, a, b), "native spilled function equals independent integer oracle");
        }
    }
    if (executable != MAP_FAILED)
    {
        munmap(executable, bytes);
    }
    free(offsets);
}

static void tier1_emit_test_pressure(Tier1EmitTest* test)
{
    const uint32_t terms[] = {1, 2, 7, 8, 9, 17, 65, 129, 600};
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        test->width = width;
        for (uint32_t selects = 0; selects < 2; selects += 1)
        {
            test->selects = selects != 0;
            for (uint32_t size = 0; size < sizeof(terms) / sizeof(terms[0]); size += 1)
            {
                test->terms = terms[size];
                IrFunction function;
                const bool created = tier1_emit_test_fixture(test, &function);
                tier1_emit_test_expect(test, created, "construct original pressure IR");
                for (uint32_t vector = 0; created && vector < 2; vector += 1)
                {
                    uint32_t ordinary_spills = 0;
                    for (uint32_t remat = 0; remat < 2; remat += 1)
                    {
                        Tier1SelectPlan plan;
                        uint32_t node;
                        const Tier1SelectError selected = tier1_select_plan(&function, 0, &test->output, &test->scratch, vector != 0, &plan, &node);
                        tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "select real pressure IR");
                        if (selected == TIER1_SELECT_OK)
                        {
                            const Tier1EmitOptions options = {
                                .register_mask = (1u << X86_REGISTER_RAX) | (1u << X86_REGISTER_RDI) | (1u << X86_REGISTER_RSI),
                                .frame_flags = size % 3 == 0 ? 0 : size % 3 == 1 ? BUSTER_TIER1_FRAME_KEEP_POINTER : BUSTER_TIER1_FRAME_NO_RED_ZONE | BUSTER_TIER1_FRAME_PROBE_STACK,
                                .vector = vector != 0,
                                .rematerialize = remat != 0,
                            };
                            Tier1EmitResult emitted;
                            const uint64_t scratch_mark = test->scratch.length;
                            const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node);
                            tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "liveness, allocation, frame and fragment emission succeed");
                            if (error != TIER1_EMIT_OK && test->failures < 20)
                            {
                                fprintf(stderr, "  emission error=%u node=%u vector=%u remat=%u\n", error, node, vector, remat);
                            }
                            tier1_emit_test_expect(test, test->scratch.length == scratch_mark, "complete emission returns scratch");
                            if (error == TIER1_EMIT_OK)
                            {
                                tier1_emit_test_expect(test, tier1_allocation_verify(&emitted.prepared.allocation, &emitted.allocation), "independent fragment interference oracle accepts");
                                tier1_emit_test_expect(test, emitted.frame.count == emitted.allocation.spill_count, "frame contains exactly the actual spill values");
                                if (test->terms > 8)
                                {
                                    tier1_emit_test_expect(test, emitted.transition_count != 0, "pressure actually changes physical locations");
                                    tier1_emit_test_expect(test, emitted.spill_stores != 0 && emitted.spill_loads != 0, "pressure actually executes spill stores and reloads");
                                    if (selects && remat)
                                    {
                                        tier1_emit_test_expect(test, emitted.rematerializations != 0, "selected constants are actually recomputed");
                                    }
                                }
                                if (remat == 0)
                                {
                                    ordinary_spills = emitted.allocation.spill_count;
                                }
                                else if (selects && test->terms > 8)
                                {
                                    tier1_emit_test_expect(test, emitted.allocation.spill_count < ordinary_spills, "rematerialization removes real frame slots");
                                }
                                test->stores += emitted.spill_stores;
                                test->loads += emitted.spill_loads;
                                test->copies += emitted.rematerializations;
                                tier1_emit_test_execute(test, &function, &emitted);
                            }
                        }
                        arena_pop_to(&test->output, 0);
                    }
                }
                arena_pop_to(&test->input, 0);
            }
        }
    }
}

static void tier1_emit_test_failures(Tier1EmitTest* test)
{
    test->terms = 17;
    test->width = 8;
    test->selects = true;
    IrFunction function;
    bool ready = tier1_emit_test_fixture(test, &function);
    Tier1SelectPlan plan;
    uint32_t node;
    ready = ready && tier1_select_plan(&function, 0, &test->output, &test->scratch, true, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, ready, "failure fixture selected");
    if (ready)
    {
        const uint64_t mark = test->output.length;
        const uint64_t scratch_mark = test->scratch.length;
        for (uint32_t failure = 0; failure < 4; failure += 1)
        {
            Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .vector = true, .rematerialize = true };
            Tier1EmitError wanted = TIER1_EMIT_INVALID;
            Arena limited = test->output;
            uint8_t* denied = calloc(X86_FORM_COUNT, 1);
            const uint8_t* allowed = test->allowed;
            if (failure == 0)
            {
                options.register_mask |= 1u << BUSTER_TIER1_SELECT_SCRATCH;
            }
            else if (failure == 1)
            {
                options.frame_flags = BUSTER_TIER1_FRAME_DYNAMIC_STACK;
                wanted = TIER1_EMIT_UNSUPPORTED;
            }
            else if (failure == 2)
            {
                limited.capacity = mark + 1;
                wanted = TIER1_EMIT_MEMORY;
            }
            else
            {
                allowed = denied;
                wanted = TIER1_EMIT_TARGET;
            }
            Tier1EmitResult output;
            memset(&output, 0xa5, sizeof(output));
            Tier1EmitResult before;
            memcpy(&before, &output, sizeof(output));
            const Tier1EmitError error = tier1_emit_function(&plan, &options, allowed, &limited, &test->scratch, &output, &node);
            tier1_emit_test_expect(test, error == wanted, "explicit failure category");
            tier1_emit_test_expect(test, limited.length == mark && test->scratch.length == scratch_mark && memcmp(&output, &before, sizeof(output)) == 0, "failure restores arenas and preserves result");
            free(denied);
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->input, 0);
    const IrSizes loop_sizes = { .instruction_count = 2, .block_count = 1, .parameter_count = 1 };
    ready = ir_create(&test->input, &loop_sizes, &function) == IR_OK;
    if (ready)
    {
        function.parameter_types[0] = IR_I64;
        function.block_offsets[1] = 2;
        tier1_emit_test_node(&function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(&function, 1, IR_BR, IR_VOID, 0, BUSTER_IR_NONE);
        ready = tier1_select_plan(&function, 0, &test->output, &test->scratch, true, &plan, &node) == TIER1_SELECT_OK;
        tier1_emit_test_expect(test, ready, "loop fixture selected independently");
        if (ready)
        {
            // The entry is its own loop's header: the CFG consumer puts a block before it and jumps back to the loop.
            const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .vector = true };
            Tier1EmitResult output;
            const bool emitted = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &output, &node) == TIER1_EMIT_OK;
            tier1_emit_test_expect(test, emitted, "a loop through the entry goes through the CFG consumer");
            bool back = false;
            for (uint32_t branch = 0; emitted && branch < output.code.branch_count; branch += 1)
            {
                back |= output.code.targets[branch] <= output.code.branches[branch];
            }
            tier1_emit_test_expect(test, !emitted || (output.general && output.cfg_input.block_count == 2 && back), "the loop keeps its jump back, after an added entry");
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->input, 0);
    const IrSizes empty_sizes = {0};
    ready = ir_create(&test->input, &empty_sizes, &function) == IR_OK && tier1_select_plan(&function, 0, &test->output, &test->scratch, true, &plan, &node) == TIER1_SELECT_OK;
    if (ready)
    {
        const Tier1EmitOptions options = { .vector = true };
        Tier1EmitResult output;
        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &output, &node);
        tier1_emit_test_expect(test, error == TIER1_EMIT_OK && output.code.count == 0 && output.allocation.spill_count == 0, "empty pipeline accepts null columns");
    }
    else
    {
        tier1_emit_test_expect(test, false, "empty fixture selected");
    }
}

#include "tier1_call_test.c"
#include "tier1_narrow_test.c"
#include "tier1_terminal_test.c"

// Frame objects' addresses in their users' memory operands (#982): the selected plan's folded loads, stores and
// addresses take the frame's base register at the object's offset plus the walked displacement, filled at emission as
// the object's lea's is. Shape 0 keeps two objects (8 and 256 bytes) with accesses at offsets on both sides of the
// 8-bit displacement's edge, through stores, loads and an addition fused into a memory operand; shape 1 reaches one
// object through an indexed address the accesses fold whole; shape 2 reads an over-aligned object, which realigns the
// frame; shape 3 keeps an address value the stores share. Every access must execute natively to its oracle's value,
// with no object address keeping its lea.
static uint64_t tier1_emit_test_object_oracle(uint32_t shape, uint64_t a, uint64_t b)
{
    uint64_t result = 0;
    if (shape == 0)
    {
        result = a + 2 * b;
    }
    else
    {
        result = a + b;
    }
    return result;
}

static bool tier1_emit_test_object_fixture(Tier1EmitTest* test, uint32_t shape, IrFunction* function)
{
    IrSizes sizes = {0};
    if (shape == 0)
    {
        sizes = (IrSizes){ .instruction_count = 25, .block_count = 1, .constant_count = 2, .slot_count = 2, .parameter_count = 2 };
    }
    else if (shape == 1)
    {
        sizes = (IrSizes){ .instruction_count = 16, .block_count = 1, .constant_count = 1, .slot_count = 1, .address_count = 1, .parameter_count = 2 };
    }
    else if (shape == 2)
    {
        sizes = (IrSizes){ .instruction_count = 8, .block_count = 1, .slot_count = 1, .parameter_count = 2 };
    }
    else
    {
        sizes = (IrSizes){ .instruction_count = 10, .block_count = 1, .slot_count = 2, .address_count = 1, .parameter_count = 2 };
    }
    bool ok = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ok)
    {
        function->result_type = IR_I64;
        function->parameter_types[0] = IR_I64;
        function->parameter_types[1] = IR_I64;
        function->block_offsets[1] = sizes.instruction_count;
        tier1_emit_test_node(function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE);
        if (shape == 0)
        {
            function->slots[0] = (IrSlot){ .size = 8, .alignment = 8, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            function->slots[1] = (IrSlot){ .size = 256, .alignment = 8, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            function->constants[0].low = 200;
            function->constants[1].low = 200;
            tier1_emit_test_node(function, 2, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 3, IR_STORE, IR_VOID, 2, 1);
            tier1_emit_test_node(function, 4, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 5, IR_LOAD, IR_I64, 4, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 6, IR_ADD, IR_I64, 5, 0);
            tier1_emit_test_node(function, 7, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 8, IR_PTRTOINT, IR_I64, 7, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 9, IR_CONST, IR_I64, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 10, IR_ADD, IR_I64, 8, 9);
            tier1_emit_test_node(function, 11, IR_INTTOPTR, IR_PTR, 10, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 12, IR_STORE, IR_VOID, 11, 6);
            tier1_emit_test_node(function, 13, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 14, IR_PTRTOINT, IR_I64, 13, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 15, IR_CONST, IR_I64, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 16, IR_ADD, IR_I64, 14, 15);
            tier1_emit_test_node(function, 17, IR_INTTOPTR, IR_PTR, 16, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 18, IR_LOAD, IR_I64, 17, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 19, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 20, IR_STORE, IR_VOID, 19, 18);
            tier1_emit_test_node(function, 21, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 22, IR_LOAD, IR_I64, 21, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 23, IR_ADD, IR_I64, 22, 5);
            tier1_emit_test_node(function, 24, IR_RET, IR_VOID, 23, BUSTER_IR_NONE);
            ok = true;
        }
        else if (shape == 1)
        {
            function->slots[0] = (IrSlot){ .size = 40, .alignment = 8, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            function->addresses[0] = (IrAddress){ .offset = 8, .scale = 8 };
            function->constants[0].low = 3;
            tier1_emit_test_node(function, 2, IR_CONST, IR_I64, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 3, IR_AND, IR_I64, 1, 2);
            tier1_emit_test_node(function, 4, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 5, IR_PTRTOINT, IR_I64, 4, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 6, IR_INTTOPTR, IR_PTR, 5, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 7, IR_ADDRESS, IR_PTR, 6, 3);
            function->c[7] = 0;
            tier1_emit_test_node(function, 8, IR_STORE, IR_VOID, 7, 0);
            tier1_emit_test_node(function, 9, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 10, IR_PTRTOINT, IR_I64, 9, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 11, IR_INTTOPTR, IR_PTR, 10, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 12, IR_ADDRESS, IR_PTR, 11, 3);
            function->c[12] = 0;
            tier1_emit_test_node(function, 13, IR_LOAD, IR_I64, 12, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 14, IR_ADD, IR_I64, 13, 1);
            tier1_emit_test_node(function, 15, IR_RET, IR_VOID, 14, BUSTER_IR_NONE);
        }
        else if (shape == 2)
        {
            function->slots[0] = (IrSlot){ .size = 64, .alignment = 32, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            tier1_emit_test_node(function, 2, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 3, IR_STORE, IR_VOID, 2, 1);
            tier1_emit_test_node(function, 4, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 5, IR_LOAD, IR_I64, 4, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 6, IR_ADD, IR_I64, 5, 0);
            tier1_emit_test_node(function, 7, IR_RET, IR_VOID, 6, BUSTER_IR_NONE);
        }
        else
        {
            function->slots[0] = (IrSlot){ .size = 16, .alignment = 8, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            function->slots[1] = (IrSlot){ .size = 8, .alignment = 8, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            function->addresses[0] = (IrAddress){ .offset = 8, .scale = 1 };
            tier1_emit_test_node(function, 2, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 3, IR_ADDRESS, IR_PTR, 2, BUSTER_IR_NONE);
            function->c[3] = 0;
            tier1_emit_test_node(function, 4, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 5, IR_STORE, IR_VOID, 4, 3);
            tier1_emit_test_node(function, 6, IR_STORE, IR_VOID, 3, 1);
            tier1_emit_test_node(function, 7, IR_LOAD, IR_I64, 3, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 8, IR_ADD, IR_I64, 7, 0);
            tier1_emit_test_node(function, 9, IR_RET, IR_VOID, 8, BUSTER_IR_NONE);
        }
    }
    return ok;
}

static void tier1_emit_test_execute_object(Tier1EmitTest* test, uint32_t shape, const IrFunction* function, Tier1EmitResult* emitted)
{
    Tier1SelectCode* code = &emitted->code;
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    const uint64_t bytes = (capacity + 4095) & ~UINT64_C(4095);
    void* executable = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint32_t* offsets = malloc((uint64_t)code->count * sizeof(uint32_t));
    bool ready = executable != MAP_FAILED && offsets != 0;
    tier1_emit_test_expect(test, ready, "object fixture execution buffer");
    if (ready)
    {
        const BranchSizingCheck checked = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
        const BranchSizing sized = checked.error == BRANCH_SIZING_OK ? size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets) : (BranchSizing){ .error = checked.error };
        ready = sized.error == BRANCH_SIZING_OK;
        tier1_emit_test_expect(test, ready, "object fixture branches size");
    }
    if (ready)
    {
        ready = x86_target_validate(test->allowed, code->records, code->count) == code->count && x86_encode(code->records, code->count, executable, capacity, 0, 0) > 0;
        tier1_emit_test_expect(test, ready, "baseline encoder accepts object fixture");
        ready = ready && mprotect(executable, bytes, PROT_READ | PROT_EXEC) == 0;
        tier1_emit_test_expect(test, ready, "object fixture made executable");
    }
    if (ready)
    {
        uint64_t random = UINT64_C(0x1f2b3c4d5e6f7081) + shape;
        for (uint32_t sample = 0; sample < 32; sample += 1)
        {
            const uint64_t a = sample < 4 ? UINT64_MAX << sample : tier1_emit_test_random(&random);
            const uint64_t b = sample < 4 ? UINT64_MAX >> sample : tier1_emit_test_random(&random);
            const uint64_t actual = tier1_emit_test_call(executable, a, b);
            tier1_emit_test_expect(test, actual == tier1_emit_test_object_oracle(shape, a, b), "native object access equals independent oracle");
        }
    }
    if (executable != MAP_FAILED)
    {
        munmap(executable, bytes);
    }
    free(offsets);
}

// Every selected memory operand through a frame object: its record's base is the frame's base register at the
// object's offset plus the walked displacement, with no lea left but the address values the stores keep.
static void tier1_emit_test_objects(Tier1EmitTest* test)
{
    for (uint32_t shape = 0; shape < 4; shape += 1)
    {
        for (uint32_t vector = 0; vector < 2; vector += 1)
        {
            for (uint32_t flags = 0; flags < (shape == 2 ? 2 : 1); flags += 1)
            {
                test->terms = shape;
                IrFunction function;
                const bool created = tier1_emit_test_object_fixture(test, shape, &function);
                tier1_emit_test_expect(test, created, "construct original object IR");
                Tier1SelectPlan plan;
                uint32_t node = BUSTER_IR_NONE;
                const Tier1SelectError selected = created ? tier1_select_plan(&function, 0, &test->output, &test->scratch, vector != 0, &plan, &node) : TIER1_SELECT_ERROR_INVALID;
                tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "select real object IR");
                if (selected == TIER1_SELECT_OK)
                {
                    const Tier1EmitOptions options = {
                        .register_mask = tier1_liveness_register_mask(),
                        .frame_flags = shape == 2 && flags == 1 ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
                        .vector = vector != 0,
                        .rematerialize = true,
                    };
                    Tier1EmitResult emitted;
                    const uint64_t scratch_mark = test->scratch.length;
                    const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node);
                    tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "object pipeline emits through the CFG consumer");
                    if (error == TIER1_EMIT_OK)
                    {
                        tier1_emit_test_expect(test, emitted.general, "a frame object takes the CFG consumer");
                        bool covered = true;
                        for (uint32_t n = 0; n < function.instruction_count; n += 1)
                        {
                            covered &= function.op[n] != IR_STACK_ADDR || plan.covered[n] != 0;
                        }
                        tier1_emit_test_expect(test, covered, "every folded object address leaves no lea");
                        bool object_lea = false;
                        for (uint32_t n = 0; n < function.instruction_count; n += 1)
                        {
                            object_lea |= plan.covered[n] == 0 && plan.patterns[n].rule == TIER1_RULE_STACK_ADDRESS;
                        }
                        tier1_emit_test_expect(test, !object_lea, "no object address keeps its lea");
                        bool placed = true;
                        int32_t low = INT32_MAX;
                        int32_t high = INT32_MIN;
                        for (uint32_t n = 0; n < function.instruction_count; n += 1)
                        {
                            const Tier1SelectPattern* pattern = &plan.patterns[n];
                            const uint32_t rule = pattern->rule;
                            const bool memory = !plan.covered[n] && pattern->stack_object != BUSTER_IR_NONE &&
                                                (rule == TIER1_RULE_LOAD || rule == TIER1_RULE_LOAD_ADDRESS || rule == TIER1_RULE_STORE || rule == TIER1_RULE_STORE_ADDRESS || rule == TIER1_RULE_ADDRESS ||
                                                 rule == TIER1_RULE_LOAD_OPERAND);
                            if (memory)
                            {
                                const uint32_t first = emitted.code.node_first[n];
                                const uint32_t span = emitted.code.node_first[n + 1] - first;
                                int32_t offset = 0;
                                const bool found = span != 0 && tier1_emit_cfg_object(&emitted, pattern->stack_object, &offset);
                                int32_t total = 0;
                                const X86Instruction* record = found && !__builtin_add_overflow(offset, pattern->displacement, &total) ? &emitted.code.records[first + span - 1] : 0;
                                placed &= record != 0 && record->rm == (uint8_t)emitted.frame.base_register && record->displacement == total;
                                low = record != 0 && total < low ? total : low;
                                high = record != 0 && total > high ? total : high;
                            }
                        }
                        tier1_emit_test_expect(test, placed, "each folded memory operand reads the frame base at object plus walked displacement");
                        if (shape == 0)
                        {
                            tier1_emit_test_expect(test, placed && low < 128 && high >= 128, "the emitted displacements straddle the 8-bit edge");
                        }
                        if (shape == 2)
                        {
                            tier1_emit_test_expect(test, emitted.frame.realigned, "the over-aligned object realigns the frame");
                        }
                        tier1_emit_test_expect(test, test->scratch.length == scratch_mark, "object emission returns scratch");
                        tier1_emit_test_execute_object(test, shape, &function, &emitted);
                    }
                }
                arena_pop_to(&test->output, 0);
                arena_pop_to(&test->input, 0);
            }
        }
    }
}

// A walked displacement the frame's object offset pushes past 32 bits: emission refuses the function rather than
// truncating the displacement.
static void tier1_emit_test_object_overflow(Tier1EmitTest* test)
{
    const IrSizes sizes = { .instruction_count = 15, .block_count = 1, .constant_count = 1, .slot_count = 2, .parameter_count = 2 };
    for (uint32_t vector = 0; vector < 2; vector += 1)
    {
        IrFunction function;
        const bool created = ir_create(&test->input, &sizes, &function) == IR_OK;
        tier1_emit_test_expect(test, created, "create overflowing displacement IR");
        if (created)
        {
            function.result_type = IR_I64;
            function.parameter_types[0] = IR_I64;
            function.parameter_types[1] = IR_I64;
            function.block_offsets[1] = 15;
            function.slots[0] = (IrSlot){ .size = 8, .alignment = 8, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            function.slots[1] = (IrSlot){ .size = 8, .alignment = 8, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            function.constants[0].low = UINT64_C(0x7fffffff);
            tier1_emit_test_node(&function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(&function, 2, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(&function, 3, IR_PTRTOINT, IR_I64, 2, BUSTER_IR_NONE);
            tier1_emit_test_node(&function, 4, IR_CONST, IR_I64, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(&function, 5, IR_ADD, IR_I64, 3, 4);
            tier1_emit_test_node(&function, 6, IR_INTTOPTR, IR_PTR, 5, BUSTER_IR_NONE);
            tier1_emit_test_node(&function, 7, IR_LOAD, IR_I64, 6, BUSTER_IR_NONE);
            tier1_emit_test_node(&function, 8, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(&function, 9, IR_PTRTOINT, IR_I64, 8, BUSTER_IR_NONE);
            tier1_emit_test_node(&function, 10, IR_ADD, IR_I64, 9, 4);
            tier1_emit_test_node(&function, 11, IR_INTTOPTR, IR_PTR, 10, BUSTER_IR_NONE);
            tier1_emit_test_node(&function, 12, IR_LOAD, IR_I64, 11, BUSTER_IR_NONE);
            tier1_emit_test_node(&function, 13, IR_ADD, IR_I64, 7, 12);
            tier1_emit_test_node(&function, 14, IR_RET, IR_VOID, 13, BUSTER_IR_NONE);
        }
        Tier1SelectPlan plan;
        uint32_t node = BUSTER_IR_NONE;
        const Tier1SelectError selected = created ? tier1_select_plan(&function, 0, &test->output, &test->scratch, vector != 0, &plan, &node) : TIER1_SELECT_ERROR_INVALID;
        tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "select overflowing displacement IR");
        if (created && selected == TIER1_SELECT_OK)
        {
            const Tier1EmitOptions options = {
                .register_mask = tier1_liveness_register_mask(),
                .frame_flags = BUSTER_TIER1_FRAME_NO_RED_ZONE,
                .vector = vector != 0,
                .rematerialize = true,
            };
            Tier1EmitResult emitted;
            const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node);
            tier1_emit_test_expect(test, error == TIER1_EMIT_FRAME, "a displacement past 32 bits refuses emission");
        }
        arena_pop_to(&test->output, 0);
        arena_pop_to(&test->input, 0);
    }
}

static int run_tier1_emit_tests(int argc, char** argv)
{
    (void)argv;
    int result = 1;
    if (argc != 0)
    {
        fprintf(stderr, "usage: test-tier1-emit\n");
    }
    else if (!x86_encoder_initialize() || !x86_semantics_initialize() || !x86_zen5_initialize())
    {
        fprintf(stderr, "test-tier1-emit: initialization failed\n");
    }
    else
    {
        fflush(0);
        const pid_t child = fork();
        if (child == 0)
        {
            Tier1EmitTest test = {
                .input = arena_reserve(32u << 20),
                .output = arena_reserve(64u << 20),
                .scratch = arena_reserve(32u << 20),
                .allowed = malloc(X86_FORM_COUNT),
            };
            if (test.input.base == 0 || test.output.base == 0 || test.scratch.base == 0 || test.allowed == 0)
            {
                _exit(1);
            }
            const X86Target target = { .architecture = X86_ARCH_BASELINE };
            x86_target_forms(&target, test.allowed);
            tier1_emit_test_pressure(&test);
            tier1_emit_test_failures(&test);
            tier1_emit_test_objects(&test);
            tier1_emit_test_object_overflow(&test);
            tier1_call_tests(&test);
            tier1_narrow_tests(&test);
            tier1_terminal_tests(&test);
            printf("tier1 fragment emission: %" PRIu64 " checks, %" PRIu64 " failures; stores=%" PRIu64 " reloads=%" PRIu64 " rematerializations=%" PRIu64 "\n", test.checks, test.failures, test.stores, test.loads, test.copies);
            arena_release(&test.input);
            arena_release(&test.output);
            arena_release(&test.scratch);
            free(test.allowed);
            fflush(0);
            _exit(test.failures != 0);
        }
        int status = 0;
        if (child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status))
        {
            result = WEXITSTATUS(status);
        }
        else
        {
            fprintf(stderr, "test-tier1-emit: native child failed (status %d)\n", status);
        }
    }
    return result;
}
