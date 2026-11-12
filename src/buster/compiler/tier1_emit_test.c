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
    // The floating fixtures' compiles whose calls save an xmm register (#1116's check of their rows).
    uint32_t vector_saves;
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

// The code with its branches sized, encoded into `bytes` of new pages and made executable: whether it runs natively
// here. The caller unmaps `executable` (MAP_FAILED where nothing was mapped).
static bool tier1_emit_test_load(Tier1EmitTest* test, Tier1SelectCode* code, void** executable, uint64_t* bytes)
{
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    *bytes = (capacity + 4095) & ~UINT64_C(4095);
    *executable = mmap(0, *bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint32_t* offsets = malloc((uint64_t)code->count * sizeof(uint32_t));
    bool ready = *executable != MAP_FAILED && offsets != 0;
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
        // #181: and a complete Zen 5 row (x86_zen5_rows.c), which the model's consumers need.
        tier1_emit_test_expect(test, x86_zen5_missing(code->records, code->count, BUSTER_X86_COST_ALL_COLUMNS) == 0, "every frame and body record has a complete Zen 5 row");
        ready = x86_target_validate(test->allowed, code->records, code->count) == code->count && x86_encode(code->records, code->count, *executable, capacity, 0, 0) > 0;
        tier1_emit_test_expect(test, ready, "baseline encoder accepts complete frame and body");
        ready = ready && mprotect(*executable, *bytes, PROT_READ | PROT_EXEC) == 0;
        tier1_emit_test_expect(test, ready, "native code made executable");
    }
    free(offsets);
    return ready && tier1_emit_test_native(test, code->records, code->count);
}

static void tier1_emit_test_execute(Tier1EmitTest* test, const IrFunction* function, Tier1EmitResult* emitted)
{
    void* executable = MAP_FAILED;
    uint64_t bytes = 0;
    if (tier1_emit_test_load(test, &emitted->code, &executable, &bytes))
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
                                .certify = true,
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
            Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .vector = true, .rematerialize = true, .certify = true };
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
            const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .vector = true, .certify = true };
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
        const Tier1EmitOptions options = { .vector = true, .certify = true };
        Tier1EmitResult output;
        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &output, &node);
        tier1_emit_test_expect(test, error == TIER1_EMIT_OK && output.code.count == 0 && output.allocation.spill_count == 0, "empty pipeline accepts null columns");
    }
    else
    {
        tier1_emit_test_expect(test, false, "empty fixture selected");
    }
}

// The call-save row that emitted record `r` stores to or loads from, as a bit of the rows' set, with *load saying which;
// 0 for any other record. The rows are a general register's (bits 0 to 15: tier1_emit_call_save's mov), an xmm
// register's (bits 16 to 31: a floating value's movsd, #141, or in a function with vectors the widest one's move, #141)
// and an opmask register's (bits 32 to 39: a mask's kmov of its width, #179), the latter two the CFG consumer's. Each
// row is the frame's, apart from every spill slot, so its displacement tells it from a spill of the same register.
static uint64_t tier1_emit_test_save_row(const Tier1EmitResult* emitted, uint32_t r, bool* load)
{
    const X86Instruction* record = &emitted->code.records[r];
    const uint16_t form = record->form & BUSTER_X86_FORM_NUMBER;
    const Tier1EmitPrepared* prepared = &emitted->prepared;
    const bool memory = record->mode == BUSTER_X86_MEMORY_BASE && record->rm == emitted->frame.base_register;
    const uint32_t reg = record->reg;
    uint64_t row = 0;
    *load = false;
    if (memory && reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS && ((prepared->call_save_mask >> reg) & 1) != 0 && (form == X86_FORM_MOV_RM64_R64 || form == X86_FORM_MOV_R64_RM64) &&
        record->displacement == tier1_emit_call_save(emitted, reg, false).displacement)
    {
        row = 1ull << reg;
        *load = form == X86_FORM_MOV_R64_RM64;
    }
    const uint32_t vector_row = emitted->general && reg < 16 && ((prepared->vector_save_mask >> reg) & 1) != 0 ? tier1_emit_cfg_vector_save_row(emitted, reg) : UINT32_MAX;
    const Tier1SelectFloatForms* wide = prepared->vector_bytes != 0 ? tier1_select_class_forms(prepared->vector_bytes, tier1_emit_encoding(prepared)) : NULL;
    const uint16_t vector_store = wide != NULL ? wide->store : X86_FORM_MOVSD_XM64_X;
    const uint16_t vector_load = wide != NULL ? wide->load : X86_FORM_MOVSD_X_XM64;
    if (memory && vector_row < emitted->frame.count && (form == vector_store || form == vector_load) && record->displacement == emitted->frame.offset[vector_row])
    {
        row = 1ull << (16 + reg);
        *load = form == vector_load;
    }
    const uint32_t mask_row = emitted->general && reg < 8 && ((prepared->mask_save_mask >> reg) & 1) != 0 ? tier1_emit_cfg_mask_save_row(emitted, reg) : UINT32_MAX;
    for (uint32_t width = 1; memory && mask_row < emitted->frame.count && width <= 8; width *= 2)
    {
        const Tier1SelectMaskForms* forms = tier1_select_mask_forms(width);
        const bool kmov = (form == forms->store || form == forms->load) && record->displacement == emitted->frame.offset[mask_row];
        row = kmov ? 1ull << (32 + reg) : row;
        *load = kmov ? form == forms->load : *load;
    }
    return row;
}

// The general registers' call-save stores among records [first, end).
static uint32_t tier1_emit_test_save_stores(const Tier1EmitResult* emitted, uint32_t first, uint32_t end)
{
    uint32_t stores = 0;
    for (uint32_t r = first; r < end && r < emitted->code.count; r += 1)
    {
        bool load = false;
        const uint64_t row = tier1_emit_test_save_row(emitted, r, &load);
        stores += row != 0 && row < (1ull << 16) && !load;
    }
    return stores;
}

// Whether the records store to some xmm call-save row where the frame has any (prepared's vector_save_mask), and to some
// opmask one where it has any (mask_save_mask): so tier1_emit_test_saves_loaded reads those rows' stores, not none. Not
// every row: the masks count each fragment that covers a call's definition phase, a floating result's among them, which
// its call doesn't store.
static bool tier1_emit_test_saves_seen(const Tier1EmitResult* emitted)
{
    uint64_t seen = 0;
    for (uint32_t r = 0; r < emitted->code.count; r += 1)
    {
        bool load = false;
        const uint64_t row = tier1_emit_test_save_row(emitted, r, &load);
        seen |= load ? 0 : row;
    }
    const bool vectors = emitted->prepared.vector_save_mask == 0 || ((seen >> 16) & 0xffff) != 0;
    const bool masks = emitted->prepared.mask_save_mask == 0 || ((seen >> 32) & 0xff) != 0;
    return !emitted->general || (vectors && masks);
}

// Whether the general registers' call-save rows are exactly those the records store to (#177): each row the frame has is
// one a call or a fixed register's sequence stores a value in, and none goes unused, where every caller-saved register's
// had a row in every function that calls. A store to a register with no row the emitter refuses itself
// (TIER1_EMIT_ALLOCATION).
static bool tier1_emit_test_general_rows_used(const Tier1EmitResult* emitted)
{
    uint64_t stored = 0;
    for (uint32_t r = 0; r < emitted->code.count; r += 1)
    {
        bool load = false;
        const uint64_t row = tier1_emit_test_save_row(emitted, r, &load);
        stored |= !load && row < (1ull << 16) ? row : 0;
    }
    return stored == emitted->prepared.call_save_mask;
}

// Whether every call-save store is loaded back (#1116), of a general, an xmm or an opmask register: a call owes its
// general registers' loads until something needs them, and loads the others back right after it, but each value it
// stores lives past it, so a load of that register from its row follows before the code leaves the straight-line run it
// is in (a branch, a return, a trap or an indirect jump). A store whose load is lost is the failure owing loads could
// introduce; this checks each store, not a total.
static bool tier1_emit_test_saves_loaded(const Tier1EmitResult* emitted)
{
    uint64_t pending = 0;
    bool loaded = true;
    uint32_t branch = 0;
    for (uint32_t r = 0; r < emitted->code.count; r += 1)
    {
        const uint16_t form = emitted->code.records[r].form & BUSTER_X86_FORM_NUMBER;
        while (branch < emitted->code.branch_count && emitted->code.branches[branch] < r)
        {
            branch += 1;
        }
        const bool leaves = form == X86_FORM_RET || form == X86_FORM_UD2 || form == X86_FORM_JMP_RM64 || (branch < emitted->code.branch_count && emitted->code.branches[branch] == r);
        loaded &= !leaves || pending == 0;
        pending = leaves ? 0 : pending;
        bool load = false;
        const uint64_t row = tier1_emit_test_save_row(emitted, r, &load);
        pending = load ? pending & ~row : pending | row;
    }
    return loaded && pending == 0;
}

// The call-save rows' helpers (Tier1EmitSaves) by both paths, on random rows, owners and masks: the vector path's
// compares and blends must give the scalar twin's masks and rows. A third of the rows hold their owners, so `kept` sees
// both answers, and some owners are none.
static void tier1_emit_test_saves_twins(Tier1EmitTest* test)
{
    uint64_t random = UINT64_C(0x5be2b9d0c41f7a63);
    bool same = true;
    for (uint32_t round = 0; round < 4096; round += 1)
    {
        Tier1EmitSaves saves[2];
        Tier1EmitTransitions transitions = { 0 };
        tier1_emit_saves_reset(&saves[0], false);
        tier1_emit_saves_reset(&saves[1], true);
        same &= memcmp(saves[0].rows, saves[1].rows, sizeof(saves[0].rows)) == 0;
        for (uint32_t reg = 0; reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
        {
            const uint64_t bits = tier1_emit_test_random(&random);
            transitions.owners[reg] = (bits & 7) == 7 ? BUSTER_IR_NONE : (uint32_t)(bits >> 8) & 63;
            const uint32_t row = (bits >> 16) % 3 == 0 ? transitions.owners[reg] : (uint32_t)(bits >> 24) & 63;
            saves[0].rows[reg] = row;
            saves[1].rows[reg] = row;
        }
        const uint64_t masks = tier1_emit_test_random(&random);
        const uint32_t saved = (uint32_t)masks & 0xffff;
        const uint32_t fixed = (uint32_t)(masks >> 16) & 0xffff;
        same &= tier1_emit_kept(&saves[0], &transitions, saved) == tier1_emit_kept(&saves[1], &transitions, saved);
        tier1_emit_saved(&saves[0], &transitions, saved);
        tier1_emit_saved(&saves[1], &transitions, saved);
        tier1_emit_fixed_rows(&saves[0], fixed);
        tier1_emit_fixed_rows(&saves[1], fixed);
        same &= saves[0].owed == saves[1].owed && memcmp(saves[0].rows, saves[1].rows, sizeof(saves[0].rows)) == 0;
    }
    tier1_emit_test_expect(test, same, "the call-save rows' vector helpers give their scalar twins' masks and rows");
}

// #2538: the calls before each position by both paths, on random sorted call positions over random lengths (a call at the
// last position, and none at all, among them): the vector prefix sum must give the scalar running count's column.
static void tier1_emit_test_calls_before_twins(Tier1EmitTest* test)
{
    uint64_t random = UINT64_C(0x2538a1b2c3d4e5f6);
    uint32_t calls[256];
    uint32_t before[2][1024];
    bool same = true;
    for (uint32_t round = 0; round < 2048; round += 1)
    {
        const uint32_t positions = 1 + (uint32_t)(tier1_emit_test_random(&random) % 1000);
        const uint32_t density = 1 + (uint32_t)(tier1_emit_test_random(&random) % 16);
        uint32_t count = 0;
        for (uint32_t q = 0; q < positions && count < 256; q += 1)
        {
            const bool call = (tier1_emit_test_random(&random) % density == 0 && round % 7 != 0) || (round % 5 == 0 && q + 1 == positions);
            calls[count] = q;
            count += call;
        }
        memset(before, 0xa5, sizeof(before));
        tier1_emit_cfg_calls_before_vector(calls, count, positions, before[0]);
        tier1_emit_cfg_calls_before_scalar(calls, count, positions, before[1]);
        same &= memcmp(before[0], before[1], ((uint64_t)positions + 1) * sizeof(uint32_t)) == 0 && before[1][positions] == count;
        same &= before[0][positions + 1] == 0xa5a5a5a5u;
    }
    tier1_emit_test_expect(test, same, "the calls before each position: the vector prefix sum gives its scalar twin's column");
}

#include "tier1_call_test.c"
#include "tier1_narrow_test.c"
#include "tier1_terminal_test.c"
#include "tier1_divide_test.c"
#include "tier1_labels_test.c"
#include "tier1_variadic_test.c"
#include "tier1_dropped_test.c"
#include "tier1_code_certificate_test.c"
#include "tier1_stack_test.c"
#include "tier1_float_test.c"
#include "tier1_induction_test.c"
#include "tier1_storage_test.c"
#include "tier1_branch_layout_test.c"
#include "tier1_address_sink_test.c"
#include "tier1_hot_recipes_test.c"
#include "tier1_symbol_fold_test.c"

// Frame objects' addresses in their users' memory operands (#982): the selected plan's folded loads, stores and
// addresses take the frame's base register at the object's offset plus the walked displacement, filled at emission as
// the object's lea's is. Shape 0 keeps two objects (8 and 256 bytes) with accesses at offsets on both sides of the
// 8-bit displacement's edge, through stores, loads and an addition fused into a memory operand; shape 1 reaches one
// object through an indexed address the accesses fold whole; shape 2 reads an over-aligned object, which realigns the
// frame; shape 3 keeps an address value the stores share; shape 4 reaches one object from the entry block past the
// walk's block (a store and a load through two single-use addresses, verified as a round trip). Shape 5 shares one
// entry address across those accesses; shape 6 also stores the shared address itself and reads it back to use it;
// shape 7 shares an address in a nonentry block and realigns the frame (#1217). Shape 8 escapes an indexed address
// whose object's final offset is nonzero in a realigned frame: its lea must preserve that offset when the frame's
// base and displacement are resolved before expansion. Memory operands fold shared addresses independently; an
// escaping value retains its lea. Shapes 9 and 11 fold a qword load into a materialized comparison and a branch; shapes 10 and 12
// compare a promoted byte and signed word directly in an over-aligned object (#1218). Every access must execute
// natively to its oracle's value.
static uint64_t tier1_emit_test_object_oracle(uint32_t shape, uint64_t a, uint64_t b)
{
    uint64_t result = 0;
    if (shape == 0)
    {
        result = a + 2 * b;
    }
    else if (shape < 9)
    {
        result = a + b;
    }
    else if (shape == 9)
    {
        result = a < b;
    }
    else if (shape == 10)
    {
        result = (uint8_t)a > 127;
    }
    else if (shape == 11)
    {
        result = a < b ? a : b;
    }
    else
    {
        result = (int16_t)a < -17 ? a : b;
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
    else if (shape == 3)
    {
        sizes = (IrSizes){ .instruction_count = 10, .block_count = 1, .slot_count = 2, .address_count = 1, .parameter_count = 2 };
    }
    else if (shape == 4)
    {
        sizes = (IrSizes){ .instruction_count = 9, .block_count = 2, .slot_count = 1, .parameter_count = 2 };
    }
    else if (shape == 6)
    {
        sizes = (IrSizes){ .instruction_count = 10, .block_count = 1, .slot_count = 2, .parameter_count = 2 };
    }
    else if (shape == 8)
    {
        sizes = (IrSizes){ .instruction_count = 13, .block_count = 1, .slot_count = 2, .constant_count = 1, .address_count = 1, .parameter_count = 2 };
    }
    else if (shape < 9)
    {
        sizes = (IrSizes){ .instruction_count = 8, .block_count = 2, .slot_count = 1, .parameter_count = 2 };
    }
    else
    {
        const bool narrow = shape == 10 || shape == 12;
        const bool branch = shape >= 11;
        sizes = (IrSizes){ .instruction_count = (narrow ? 14u : 11u) + branch, .block_count = branch ? 3u : 1u, .constant_count = narrow, .slot_count = 1, .address_count = 1, .parameter_count = 2 };
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
        else if (shape == 3)
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
        else if (shape == 4)
        {
            // The object in the entry block past the walk's block, as the producer's preheader does: one store and one
            // load through two single-use addresses of the same object, both folded, verified as a round trip.
            function->slots[0] = (IrSlot){ .size = 8, .alignment = 8, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            function->block_offsets[1] = 5;
            function->block_offsets[2] = sizes.instruction_count;
            tier1_emit_test_node(function, 2, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 3, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 4, IR_BR, IR_VOID, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 5, IR_STORE, IR_VOID, 2, 1);
            tier1_emit_test_node(function, 6, IR_LOAD, IR_I64, 3, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 7, IR_ADD, IR_I64, 6, 0);
            tier1_emit_test_node(function, 8, IR_RET, IR_VOID, 7, BUSTER_IR_NONE);
        }
        else if (shape == 6)
        {
            function->slots[0] = (IrSlot){ .size = 8, .alignment = 8, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            function->slots[1] = (IrSlot){ .size = 8, .alignment = 8, .type = IR_PTR, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            tier1_emit_test_node(function, 2, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 3, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 4, IR_STORE, IR_VOID, 3, 2);
            tier1_emit_test_node(function, 5, IR_LOAD, IR_PTR, 3, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 6, IR_STORE, IR_VOID, 5, 1);
            tier1_emit_test_node(function, 7, IR_LOAD, IR_I64, 2, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 8, IR_ADD, IR_I64, 7, 0);
            tier1_emit_test_node(function, 9, IR_RET, IR_VOID, 8, BUSTER_IR_NONE);
        }
        else if (shape == 8)
        {
            // The 32-byte-aligned pointer object precedes the indexed object's 8-byte alignment class in the layout.
            function->slots[0] = (IrSlot){ .size = 32, .alignment = 32, .type = IR_PTR, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            function->slots[1] = (IrSlot){ .size = 32, .alignment = 8, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            function->constants[0].low = 7;
            function->addresses[0] = (IrAddress){ .scale = 1 };
            tier1_emit_test_node(function, 2, IR_CONST, IR_I64, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 3, IR_AND, IR_I64, 1, 2);
            tier1_emit_test_node(function, 4, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 5, IR_ADDRESS, IR_PTR, 4, 3);
            function->c[5] = 0;
            tier1_emit_test_node(function, 6, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 7, IR_STORE, IR_VOID, 6, 5);
            tier1_emit_test_node(function, 8, IR_LOAD, IR_PTR, 6, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 9, IR_STORE, IR_VOID, 8, 0);
            tier1_emit_test_node(function, 10, IR_LOAD, IR_I64, 8, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 11, IR_ADD, IR_I64, 10, 1);
            tier1_emit_test_node(function, 12, IR_RET, IR_VOID, 11, BUSTER_IR_NONE);
        }
        else if (shape < 9)
        {
            function->slots[0] = (IrSlot){ .size = 8, .alignment = shape == 7 ? 32 : 8, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            function->block_offsets[1] = shape == 7 ? 3 : 4;
            function->block_offsets[2] = sizes.instruction_count;
            const uint32_t object = shape == 7 ? 3 : 2;
            const uint32_t branch = shape == 7 ? 2 : 3;
            tier1_emit_test_node(function, object, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, branch, IR_BR, IR_VOID, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 4, IR_STORE, IR_VOID, object, 1);
            tier1_emit_test_node(function, 5, IR_LOAD, IR_I64, object, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 6, IR_ADD, IR_I64, 5, 0);
            tier1_emit_test_node(function, 7, IR_RET, IR_VOID, 6, BUSTER_IR_NONE);
        }
        else
        {
            const bool narrow = shape == 10 || shape == 12;
            const bool branch = shape >= 11;
            const IrType type = shape == 10 ? IR_I8 : shape == 12 ? IR_I16 : IR_I64;
            const IrType promoted = shape == 10 ? IR_I32 : IR_I64;
            function->slots[0] = (IrSlot){ .size = narrow ? 64u : 16u, .alignment = narrow ? 32u : 8u, .type = (uint8_t)type, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            function->addresses[0] = (IrAddress){ .offset = 8, .scale = 1 };
            uint32_t next = 2;
            const uint32_t stored = narrow ? next++ : 0;
            if (narrow)
            {
                tier1_emit_test_node(function, stored, IR_TRUNC, type, 0, BUSTER_IR_NONE);
            }
            const uint32_t store_object = next++;
            const uint32_t store_address = next++;
            tier1_emit_test_node(function, store_object, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, store_address, IR_ADDRESS, IR_PTR, store_object, BUSTER_IR_NONE);
            function->c[store_address] = 0;
            tier1_emit_test_node(function, next++, IR_STORE, IR_VOID, store_address, stored);
            const uint32_t load_object = next++;
            const uint32_t load_address = next++;
            const uint32_t load = next++;
            tier1_emit_test_node(function, load_object, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, load_address, IR_ADDRESS, IR_PTR, load_object, BUSTER_IR_NONE);
            function->c[load_address] = 0;
            tier1_emit_test_node(function, load, IR_LOAD, type, load_address, BUSTER_IR_NONE);
            uint32_t left = load;
            uint32_t right = 1;
            if (narrow)
            {
                left = next++;
                right = next++;
                tier1_emit_test_node(function, left, shape == 10 ? IR_ZEXT : IR_SEXT, promoted, load, BUSTER_IR_NONE);
                tier1_emit_test_node(function, right, IR_CONST, promoted, 0, BUSTER_IR_NONE);
                function->constants[0].low = shape == 10 ? 127 : (uint64_t)-17;
            }
            const uint32_t compared = next++;
            tier1_emit_test_node(function, compared, IR_ICMP, IR_I1, left, right);
            function->c[compared] = shape == 10 ? IR_GT : shape == 12 ? IR_LT : IR_ULT;
            if (branch)
            {
                const uint32_t jumped = next++;
                tier1_emit_test_node(function, jumped, IR_CBR, IR_VOID, compared, 1);
                function->c[jumped] = 2;
                function->block_offsets[1] = next;
                tier1_emit_test_node(function, next++, IR_RET, IR_VOID, 0, BUSTER_IR_NONE);
                function->block_offsets[2] = next;
                tier1_emit_test_node(function, next++, IR_RET, IR_VOID, 1, BUSTER_IR_NONE);
                function->block_offsets[3] = next;
            }
            else
            {
                const uint32_t extended = next++;
                tier1_emit_test_node(function, extended, IR_ZEXT, IR_I64, compared, BUSTER_IR_NONE);
                tier1_emit_test_node(function, next++, IR_RET, IR_VOID, extended, BUSTER_IR_NONE);
            }
            ok = next == sizes.instruction_count;
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
        static const uint64_t compare_edges[] = { 0, 127, 128, 255, 32767, 32768, 65518, 65519, 65520, UINT64_MAX };
        uint64_t random = UINT64_C(0x1f2b3c4d5e6f7081) + shape;
        for (uint32_t sample = 0; sample < 32; sample += 1)
        {
            const uint64_t a = shape >= 9 && sample < sizeof(compare_edges) / sizeof(compare_edges[0]) ? compare_edges[sample] :
                              sample < 4 ? UINT64_MAX << sample : tier1_emit_test_random(&random);
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
    for (uint32_t shape = 0; shape < 13; shape += 1)
    {
        for (uint32_t vector = 0; vector < 2; vector += 1)
        {
            for (uint32_t flags = 0; flags < (shape >= 5 ? 4 : shape == 2 ? 2 : 1); flags += 1)
            {
                test->terms = shape;
                IrFunction function = { 0 };
                const bool created = tier1_emit_test_object_fixture(test, shape, &function);
                tier1_emit_test_expect(test, created, "construct original object IR");
                Tier1SelectPlan plan;
                uint32_t node = BUSTER_IR_NONE;
                // Every non-entry block is an immediate child of entry: the preheader's one successor or the two
                // comparison exits. Cross-block uses, including the exits' returned parameters, need this proof.
                uint32_t dominance_enter[3] = { 0, 1, 2 };
                uint32_t dominance_leave[3] = { function.block_count, 2, 3 };
                const IrDominance dominance = { .enter = dominance_enter, .leave = dominance_leave, .block_count = function.block_count };
                const Tier1SelectError selected = created ? tier1_select_plan(&function, function.block_count > 1 ? &dominance : 0, &test->output, &test->scratch, vector != 0, &plan, &node) : TIER1_SELECT_ERROR_INVALID;
                tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "select real object IR");
                if (selected == TIER1_SELECT_OK)
                {
                    Tier1EmitDiagnostic diagnostic = { 0 };
                    const Tier1EmitOptions options = {
                        .certify = true,
                        .diagnostic = &diagnostic,
                        .register_mask = tier1_liveness_register_mask(),
                        .frame_flags = (flags & 1) != 0 ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
                        .vector = vector != 0,
                        .rematerialize = flags < 2,
                    };
                    Tier1EmitResult emitted;
                    const uint64_t scratch_mark = test->scratch.length;
                    const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node);
                    if (error != TIER1_EMIT_OK && test->failures < 20)
                    {
                        fprintf(stderr, "tier1-emit: object emission (shape=%u, vector=%u, frame_flags=0x%x, error=%u, error_node=%u, cause=%u, cause_error=%u)\n",
                                shape, vector, options.frame_flags, (unsigned)error, node, (unsigned)diagnostic.cause, diagnostic.error);
                    }
                    tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "object pipeline emits through the CFG consumer");
                    if (error == TIER1_EMIT_OK)
                    {
                        tier1_emit_test_expect(test, emitted.general, "a frame object takes the CFG consumer");
                        bool covered = true;
                        for (uint32_t n = 0; n < function.instruction_count; n += 1)
                        {
                            covered &= function.op[n] != IR_STACK_ADDR || (plan.covered[n] != 0) == !(shape == 6 && n == 2);
                        }
                        tier1_emit_test_expect(test, covered, "every folded object address leaves no lea");
                        uint32_t object_leas = 0;
                        for (uint32_t n = 0; n < function.instruction_count; n += 1)
                        {
                            object_leas += plan.covered[n] == 0 && plan.patterns[n].rule == TIER1_RULE_STACK_ADDRESS;
                        }
                        tier1_emit_test_expect(test, object_leas == (shape == 6 ? 1u : 0u), "only an escaping object address keeps its lea");
                        if (shape == 3)
                        {
                            tier1_emit_test_expect(test, !plan.covered[3] && plan.patterns[6].stack_object == 0 &&
                                                           (plan.covered[7] ? plan.patterns[8].stack_object : plan.patterns[7].stack_object) == 0,
                                                   "shared displaced address remains for its escape while both memory users fold it");
                        }
                        if (shape == 6)
                        {
                            tier1_emit_test_expect(test, plan.patterns[4].stack_object == 1 && plan.patterns[5].stack_object == 1 &&
                                                           (plan.covered[7] ? plan.patterns[8].stack_object : plan.patterns[7].stack_object) == 0,
                                                   "shared frame address remains for a pointer store and its folded memory uses keep their own objects");
                        }
                        if (shape == 8)
                        {
                            int32_t offset = 0;
                            const uint32_t first = emitted.code.node_first[5];
                            const uint32_t end = emitted.code.node_first[6];
                            const X86Instruction* record = end > first ? &emitted.code.records[end - 1] : 0;
                            tier1_emit_test_expect(test, !plan.covered[5] && plan.patterns[5].stack_object == 1 && plan.patterns[5].displacement == 0 &&
                                                           tier1_emit_cfg_object(&function, &emitted, 1, &offset) && offset != 0 && record != 0 &&
                                                           record->form == X86_FORM_LEA_R64_M && record->mode == BUSTER_X86_MEMORY_BASE_INDEX(0) &&
                                                           record->rm == (uint8_t)emitted.frame.base_register && record->displacement == offset,
                                                   "an escaping indexed frame address keeps its lea and the frame's nonzero final displacement");
                        }
                        bool placed = true;
                        uint32_t compared = 0;
                        int32_t low = INT32_MAX;
                        int32_t high = INT32_MIN;
                        for (uint32_t n = 0; n < function.instruction_count; n += 1)
                        {
                            const Tier1SelectPattern* pattern = &plan.patterns[n];
                            const uint32_t rule = pattern->rule;
                            const bool memory = !plan.covered[n] && pattern->stack_object != BUSTER_IR_NONE &&
                                                (rule == TIER1_RULE_LOAD || rule == TIER1_RULE_LOAD_ADDRESS || rule == TIER1_RULE_STORE || rule == TIER1_RULE_STORE_ADDRESS || rule == TIER1_RULE_ADDRESS ||
                                                 rule == TIER1_RULE_LOAD_OPERAND || pattern->compare_memory);
                            if (memory)
                            {
                                const uint32_t first = emitted.code.node_first[n];
                                const uint32_t span = emitted.code.node_first[n + 1] - first;
                                int32_t offset = 0;
                                const bool found = span != 0 && tier1_emit_cfg_object(&function, &emitted, pattern->stack_object, &offset);
                                int32_t total = 0;
                                const X86Instruction* record = found && !__builtin_add_overflow(offset, pattern->displacement, &total) ? &emitted.code.records[first + span - 1] : 0;
                                if (pattern->compare_memory && record != 0)
                                {
                                    record = 0;
                                    uint32_t accesses = 0;
                                    bool consumed = false;
                                    for (uint32_t r = first; r < first + span; r += 1)
                                    {
                                        const X86Instruction* candidate = &emitted.code.records[r];
                                        const X86Effects* effects = x86_effects(candidate->form);
                                        if (effects->operation == X86_OPERATION_CMP && (candidate->mode & BUSTER_X86_MODE_MEMORY) != 0)
                                        {
                                            record = candidate;
                                            accesses += 1;
                                            placed &= effects->width == (shape == 10 ? 1 : shape == 12 ? 2 : 8);
                                        }
                                        consumed |= record != 0 && effects->operation == (shape >= 11 ? X86_OPERATION_JCC : X86_OPERATION_SETCC);
                                    }
                                    compared += 1;
                                    placed &= accesses == 1 && consumed && (rule == TIER1_RULE_BRANCH_COMPARE) == (shape >= 11);
                                }
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
                        if (shape == 2 || shape == 7 || shape == 8 || shape == 10 || shape == 12)
                        {
                            tier1_emit_test_expect(test, emitted.frame.realigned, "the over-aligned object realigns the frame");
                        }
                        if (shape >= 9)
                        {
                            bool folded = true;
                            for (uint32_t n = 0; n < function.instruction_count; n += 1)
                            {
                                folded &= function.op[n] != IR_LOAD || plan.covered[n] != 0;
                            }
                            tier1_emit_test_expect(test, compared == 1 && folded, "one frame-object CMP covers the load before its boolean result or branch");
                            tier1_emit_test_expect(test, emitted.frame.frame_pointer == (shape == 10 || shape == 12 || (flags & 1) != 0), "comparison fixtures keep the requested or realignment frame pointer");
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

// Frame objects' lifetimes (#177, tier1_emit_cfg_lifetimes), on IR with lifetime markers, each object 64 bytes. Shape 0:
// two objects whose lifetimes don't meet, which share one frame row; shape 1: two whose lifetimes overlap, which don't;
// shape 2: an object that one branch ends and the other keeps, read after the join, beside one started there, which may
// not take its row; shape 3: an object that only markers refer to (as SROA and the rebuild keep one), whose lifetime
// ends before the other's starts, so that the two share a row. Each returns a + b (shape 2 where b isn't zero, the path
// that keeps the first object) natively.
static bool tier1_emit_test_lifetime_fixture(Tier1EmitTest* test, uint32_t shape, IrFunction* function)
{
    const uint32_t instructions = shape == 2 ? 18 : shape == 3 ? 11 : 14;
    const IrSizes sizes = { .instruction_count = instructions, .block_count = shape == 2 ? 3 : 1, .constant_count = shape == 2 ? 1 : 0, .slot_count = 2, .parameter_count = 2 };
    const bool ok = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ok)
    {
        function->result_type = IR_I64;
        function->parameter_types[0] = IR_I64;
        function->parameter_types[1] = IR_I64;
        function->slots[0] = (IrSlot){ .size = 64, .alignment = 8, .type = IR_VOID, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
        function->slots[1] = (IrSlot){ .size = 64, .alignment = 8, .type = IR_VOID, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
        tier1_emit_test_node(function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE);
        tier1_emit_test_node(function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE);
        if (shape == 0)
        {
            function->block_offsets[1] = 14;
            tier1_emit_test_node(function, 2, IR_LIFETIME, IR_VOID, 0, 0);
            tier1_emit_test_node(function, 3, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 4, IR_STORE, IR_VOID, 3, 0);
            tier1_emit_test_node(function, 5, IR_LOAD, IR_I64, 3, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 6, IR_LIFETIME, IR_VOID, 0, 1);
            tier1_emit_test_node(function, 7, IR_LIFETIME, IR_VOID, 1, 0);
            tier1_emit_test_node(function, 8, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 9, IR_STORE, IR_VOID, 8, 1);
            tier1_emit_test_node(function, 10, IR_LOAD, IR_I64, 8, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 11, IR_LIFETIME, IR_VOID, 1, 1);
            tier1_emit_test_node(function, 12, IR_ADD, IR_I64, 5, 10);
            tier1_emit_test_node(function, 13, IR_RET, IR_VOID, 12, BUSTER_IR_NONE);
        }
        else if (shape == 1)
        {
            function->block_offsets[1] = 14;
            tier1_emit_test_node(function, 2, IR_LIFETIME, IR_VOID, 0, 0);
            tier1_emit_test_node(function, 3, IR_LIFETIME, IR_VOID, 1, 0);
            tier1_emit_test_node(function, 4, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 5, IR_STORE, IR_VOID, 4, 0);
            tier1_emit_test_node(function, 6, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 7, IR_STORE, IR_VOID, 6, 1);
            tier1_emit_test_node(function, 8, IR_LOAD, IR_I64, 4, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 9, IR_LOAD, IR_I64, 6, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 10, IR_LIFETIME, IR_VOID, 0, 1);
            tier1_emit_test_node(function, 11, IR_LIFETIME, IR_VOID, 1, 1);
            tier1_emit_test_node(function, 12, IR_ADD, IR_I64, 8, 9);
            tier1_emit_test_node(function, 13, IR_RET, IR_VOID, 12, BUSTER_IR_NONE);
        }
        else if (shape == 3)
        {
            function->block_offsets[1] = 11;
            tier1_emit_test_node(function, 2, IR_LIFETIME, IR_VOID, 0, 0);
            tier1_emit_test_node(function, 3, IR_LIFETIME, IR_VOID, 0, 1);
            tier1_emit_test_node(function, 4, IR_LIFETIME, IR_VOID, 1, 0);
            tier1_emit_test_node(function, 5, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 6, IR_STORE, IR_VOID, 5, 0);
            tier1_emit_test_node(function, 7, IR_LOAD, IR_I64, 5, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 8, IR_LIFETIME, IR_VOID, 1, 1);
            tier1_emit_test_node(function, 9, IR_ADD, IR_I64, 7, 1);
            tier1_emit_test_node(function, 10, IR_RET, IR_VOID, 9, BUSTER_IR_NONE);
        }
        else
        {
            // b0: the first object started and stored, then to b1 where b is zero and to b2 otherwise; b1 ends it; b2
            // starts the second, stores b there and reads both.
            function->block_offsets[1] = 8;
            function->block_offsets[2] = 10;
            function->block_offsets[3] = 18;
            function->constants[0].low = 0;
            tier1_emit_test_node(function, 2, IR_LIFETIME, IR_VOID, 0, 0);
            tier1_emit_test_node(function, 3, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 4, IR_STORE, IR_VOID, 3, 0);
            tier1_emit_test_node(function, 5, IR_CONST, IR_I64, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 6, IR_ICMP, IR_I1, 1, 5);
            function->c[6] = IR_EQ;
            tier1_emit_test_node(function, 7, IR_CBR, IR_VOID, 6, 1);
            function->c[7] = 2;
            tier1_emit_test_node(function, 8, IR_LIFETIME, IR_VOID, 0, 1);
            tier1_emit_test_node(function, 9, IR_BR, IR_VOID, 2, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 10, IR_LIFETIME, IR_VOID, 1, 0);
            tier1_emit_test_node(function, 11, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 12, IR_STORE, IR_VOID, 11, 1);
            tier1_emit_test_node(function, 13, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 14, IR_LOAD, IR_I64, 13, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 15, IR_LOAD, IR_I64, 11, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 16, IR_ADD, IR_I64, 14, 15);
            tier1_emit_test_node(function, 17, IR_RET, IR_VOID, 16, BUSTER_IR_NONE);
        }
    }
    return ok;
}

static void tier1_emit_test_lifetimes(Tier1EmitTest* test)
{
    for (uint32_t shape = 0; shape < 4; shape += 1)
    {
        for (uint32_t vector = 0; vector < 2; vector += 1)
        {
            IrFunction function;
            const bool created = tier1_emit_test_lifetime_fixture(test, shape, &function);
            tier1_emit_test_expect(test, created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK, "construct lifetime IR");
            Tier1SelectPlan plan;
            uint32_t node = BUSTER_IR_NONE;
            // Shape 2's dominator tree: the entry block over both of the others, whose values they read.
            uint32_t dominance_enter[3] = { 0, 1, 2 };
            uint32_t dominance_leave[3] = { 3, 2, 3 };
            const IrDominance tree = { .enter = dominance_enter, .leave = dominance_leave, .block_count = 3 };
            const Tier1SelectError selected = created ? tier1_select_plan(&function, shape == 2 ? &tree : 0, &test->output, &test->scratch, vector != 0, &plan, &node) :
                                                        TIER1_SELECT_ERROR_INVALID;
            tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "select lifetime IR");
            if (selected == TIER1_SELECT_OK)
            {
                const Tier1EmitOptions options = {
                    .certify = true,
                    .register_mask = tier1_liveness_register_mask(),
                    .vector = vector != 0,
                    .rematerialize = true,
                };
                Tier1EmitResult emitted;
                const uint64_t scratch_mark = test->scratch.length;
                const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node);
                tier1_emit_test_expect(test, error == TIER1_EMIT_OK && emitted.general, "lifetime markers take the CFG consumer");
                if (error == TIER1_EMIT_OK)
                {
                    int32_t first = 0;
                    int32_t second = 0;
                    const bool found = tier1_emit_cfg_object(&function, &emitted, 0, &first) && tier1_emit_cfg_object(&function, &emitted, 1, &second);
                    const bool apart = shape == 0 || shape == 3;
                    tier1_emit_test_expect(test, found && (apart ? first == second : first != second && (first - second >= 64 || second - first >= 64)),
                                           shape == 3 ? "an object only markers refer to shares its row by its markers" :
                                           apart ? "objects whose lifetimes don't meet share a frame row" : "objects whose lifetimes meet take rows of their own");
                    tier1_emit_test_expect(test, test->scratch.length == scratch_mark, "lifetime emission returns scratch");
                    Tier1SelectCode* code = &emitted.code;
                    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
                    const uint64_t bytes = (capacity + 4095) & ~UINT64_C(4095);
                    void* executable = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                    uint32_t* offsets = malloc((uint64_t)code->count * sizeof(uint32_t));
                    bool ready = executable != MAP_FAILED && offsets != 0;
                    if (ready)
                    {
                        const BranchSizingCheck checked = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
                        ready = checked.error == BRANCH_SIZING_OK && size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets).error == BRANCH_SIZING_OK;
                        ready = ready && x86_target_validate(test->allowed, code->records, code->count) == code->count && x86_encode(code->records, code->count, executable, capacity, 0, 0) > 0;
                        ready = ready && mprotect(executable, bytes, PROT_READ | PROT_EXEC) == 0;
                    }
                    tier1_emit_test_expect(test, ready, "lifetime fixture encodes and maps");
                    uint64_t random = UINT64_C(0x5a17e7e5d1ce0001) + shape;
                    for (uint32_t sample = 0; ready && sample < 32; sample += 1)
                    {
                        const uint64_t a = tier1_emit_test_random(&random);
                        const uint64_t b = tier1_emit_test_random(&random) | 1;
                        tier1_emit_test_expect(test, tier1_emit_test_call(executable, a, b) == a + b, "native lifetime fixture returns a + b");
                    }
                    if (executable != MAP_FAILED)
                    {
                        munmap(executable, bytes);
                    }
                    free(offsets);
                }
            }
            arena_pop_to(&test->output, 0);
            arena_pop_to(&test->input, 0);
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
                .certify = true,
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

// #989's review: a node's sequence at its room and past it, in a consumer. `node` takes no-ops before its records
// (Tier1SelectPattern.nops), one more each time: while its sequence fits its room, the function emits with that many
// more records and runs as before (the pressure fixture's oracle, or with `branching` that of
// tier1_emit_test_overlong's CFG); past it, the function fails at the node as an invalid one, its arenas and result as
// they were. Returns how many no-ops fit.
static uint32_t tier1_emit_test_overlong_node(Tier1EmitTest* test, Tier1SelectPlan* plan, const IrFunction* function, const Tier1EmitOptions* options, uint32_t node, uint32_t base, bool branching)
{
    uint32_t fitted = 0;
    bool refused = false;
    for (uint32_t nops = 0; nops <= BUSTER_TIER1_SELECT_RECORDS_PER_NODE; nops += 1)
    {
        plan->patterns[node].nops = (uint8_t)nops;
        const uint64_t mark = test->output.length;
        const uint64_t scratch_mark = test->scratch.length;
        Tier1EmitResult result;
        memset(&result, 0xa5, sizeof(result));
        Tier1EmitResult before;
        memcpy(&before, &result, sizeof(result));
        uint32_t failed = BUSTER_IR_NONE;
        const Tier1EmitError error = tier1_emit_function(plan, options, test->allowed, &test->output, &test->scratch, &result, &failed);
        if (error == TIER1_EMIT_OK)
        {
            tier1_emit_test_expect(test, !refused && result.code.count == base + nops && result.general == branching, "a node's no-ops up to its room are its records");
            fitted += refused ? 0 : 1;
            if (!branching)
            {
                tier1_emit_test_execute(test, function, &result);
            }
            void* executable = MAP_FAILED;
            uint64_t bytes = 0;
            if (branching && tier1_emit_test_load(test, &result.code, &executable, &bytes))
            {
                uint64_t random = UINT64_C(0x6c0d2b9e17a4f385) + nops;
                for (uint32_t sample = 0; sample < 32; sample += 1)
                {
                    const uint64_t a = sample < 4 ? UINT64_MAX >> sample : tier1_emit_test_random(&random);
                    const uint64_t b = sample < 2 ? sample : tier1_emit_test_random(&random);
                    const uint64_t s = a + b;
                    tier1_emit_test_expect(test, tier1_emit_test_call(executable, a, b) == (s < b ? s ^ a : s - b), "a node's sequence at its room runs through the CFG consumer");
                }
            }
            if (executable != MAP_FAILED)
            {
                munmap(executable, bytes);
            }
        }
        else
        {
            tier1_emit_test_expect(test, error == TIER1_EMIT_INVALID && failed == node && test->output.length == mark && test->scratch.length == scratch_mark && memcmp(&result, &before, sizeof(result)) == 0,
                                   "a node's sequence past its room fails the function at the node, its arenas and result as they were");
            refused = true;
        }
        arena_pop_to(&test->output, mark);
    }
    tier1_emit_test_expect(test, refused, "a node's sequence past its room is refused");
    plan->patterns[node].nops = 0;
    return fitted;
}

// #989's review: both consumers refuse a node's sequence past its room. The straight-line consumer on the pressure
// fixture and the CFG consumer on s = a + b, then s < b (unsigned) ? s ^ a : s - b, whose branch's comparison is its
// sequence's conditional jump's (its comparison, the jump and the jump to the other block: three records): every node
// that writes records, by both paths.
static void tier1_emit_test_overlong(Tier1EmitTest* test)
{
    test->terms = 3;
    test->width = 8;
    test->selects = true;
    for (uint32_t branching = 0; branching < 2; branching += 1)
    {
        IrFunction function;
        bool ready = false;
        if (branching == 0)
        {
            ready = tier1_emit_test_fixture(test, &function);
        }
        else
        {
            const IrSizes sizes = { .instruction_count = 9, .block_count = 3, .parameter_count = 2 };
            ready = ir_create(&test->input, &sizes, &function) == IR_OK;
            if (ready)
            {
                function.result_type = IR_I64;
                function.parameter_types[0] = IR_I64;
                function.parameter_types[1] = IR_I64;
                function.block_offsets[1] = 5;
                function.block_offsets[2] = 7;
                function.block_offsets[3] = 9;
                tier1_emit_test_node(&function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE);
                tier1_emit_test_node(&function, 1, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE);
                tier1_emit_test_node(&function, 2, IR_ADD, IR_I64, 0, 1);
                tier1_emit_test_node(&function, 3, IR_ICMP, IR_I1, 2, 1);
                function.c[3] = IR_ULT;
                tier1_emit_test_node(&function, 4, IR_CBR, IR_VOID, 3, 1);
                function.c[4] = 2;
                tier1_emit_test_node(&function, 5, IR_XOR, IR_I64, 2, 0);
                tier1_emit_test_node(&function, 6, IR_RET, IR_VOID, 5, BUSTER_IR_NONE);
                tier1_emit_test_node(&function, 7, IR_SUB, IR_I64, 2, 1);
                tier1_emit_test_node(&function, 8, IR_RET, IR_VOID, 7, BUSTER_IR_NONE);
                ready = ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK;
            }
        }
        tier1_emit_test_expect(test, ready, "overlong fixture is valid IR");
        for (uint32_t vector = 0; ready && vector < 2; vector += 1)
        {
            IrDominance dominance;
            const bool dominated = branching == 0 || tier1_terminal_dominance(test, &function, vector != 0, &dominance);
            Tier1SelectPlan plan;
            uint32_t node = BUSTER_IR_NONE;
            const bool planned = dominated && tier1_select_plan(&function, branching != 0 ? &dominance : 0, &test->output, &test->scratch, vector != 0, &plan, &node) == TIER1_SELECT_OK;
            tier1_emit_test_expect(test, planned, "overlong fixture selects");
            const Tier1EmitOptions options = { .register_mask = tier1_liveness_register_mask(), .vector = vector != 0, .certify = true };
            Tier1EmitResult base;
            const bool based = planned && tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &base, &node) == TIER1_EMIT_OK && base.general == (branching != 0);
            tier1_emit_test_expect(test, based, "overlong fixture emits through its consumer");
            uint32_t swept = 0;
            for (uint32_t n = 0; based && n < function.instruction_count; n += 1)
            {
                const uint32_t op = function.op[n];
                const uint32_t rule = plan.patterns[n].rule;
                if (!plan.covered[n] && op != IR_PARAM && op != IR_PHI && op != IR_BR && op != IR_CALL && op != IR_CALL_INDIRECT && tier1_select_fixed_registers(rule) == 0)
                {
                    const uint32_t fitted = tier1_emit_test_overlong_node(test, &plan, &function, &options, n, base.code.count, branching != 0);
                    tier1_emit_test_expect(test, op != IR_CBR || (plan.patterns[n].rule == TIER1_RULE_BRANCH_COMPARE && fitted == BUSTER_TIER1_SELECT_RECORDS_PER_NODE - 2),
                                           "a branch's comparison, conditional jump and jump take three records of its room");
                    swept += 1;
                }
            }
            tier1_emit_test_expect(test, swept >= (branching != 0 ? 5u : 7u), "every node that writes records is swept");
            arena_pop_to(&test->output, 0);
        }
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
            tier1_emit_test_lifetimes(&test);
            tier1_emit_test_object_overflow(&test);
            tier1_emit_test_saves_twins(&test);
            tier1_emit_test_calls_before_twins(&test);
            tier1_call_tests(&test);
            tier1_narrow_tests(&test);
            tier1_terminal_tests(&test);
            tier1_divide_tests(&test);
            tier1_labels_tests(&test);
            tier1_variadic_tests(&test);
            tier1_dropped_tests(&test);
            tier1_code_certificate_tests(&test);
            tier1_stack_tests(&test);
            tier1_float_tests(&test);
            tier1_induction_tests(&test);
            tier1_storage_tests(&test);
            tier1_storage_phi_tests(&test);
            tier1_storage_expression_tests(&test);
            tier1_storage_narrow_arithmetic_tests(&test);
            tier1_storage_scoped_tests(&test);
            tier1_storage_loop_state_tests(&test);
            tier1_storage_midpoint_tests(&test);
            tier1_storage_parameter_tests(&test);
            tier1_branch_layout_tests(&test);
            tier1_address_sink_tests(&test);
            tier1_hot_recipes_tests(&test);
            tier1_symbol_fold_tests(&test);
            tier1_emit_test_overlong(&test);
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
