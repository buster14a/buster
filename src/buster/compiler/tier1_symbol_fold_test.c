// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2573: a memory operand folds a direct symbol's address, rip-relative, from any block; a GOT slot's address and a
// thread-local's keep their sequences. Included by tier1_emit_test.c. The entry block takes four symbols' addresses:
// symbol 1 reached directly, 2 through its GOT slot, 3 a thread-local of the image (local-exec) and 4 one through its
// offset's GOT slot (initial-exec), and branches on symbol 1's word at 4 (cmp word ptr [rip + symbol + 4], 0x1234 and
// a conditional jump, whose PC32 addend covers the immediate's two bytes). One arm loads symbol 1 + 8 and the other
// three symbols, adds them, stores x into symbol 1 + 8 and the thread-local 3, and the sum into symbol 2; the other
// compares symbol 1's first byte with 3 (cmp byte ptr [rip + symbol], 3) and stores the truth value into symbol 4.
// Both selection paths plan it with the access column: symbol 1's accesses and comparisons fold it, no node computes
// its address, and the others' go through their sequences' registers. Without the column, every symbol folds, and the
// consumer, given the column, refuses the plan. The code is certified and runs natively with its references resolved
// here: the PC32s and the GOT loads against a data page after the code, the thread-locals' against this thread's own.
// A straight-line consumer's plan that claims a folded symbol is refused, which its chain rule keeps from happening
// (tier1_emit_cfg_chain). A block's repeated indexed accesses of one global share its first symbol address and index
// extension (#2601).

#define BUSTER_TIER1_SYMBOL_FOLD_NODES 32

static _Thread_local uint64_t tier1_symbol_fold_local;
static _Thread_local uint64_t tier1_symbol_fold_initial;

// The access column: symbol 0 none, 1 direct, 2 through the GOT, 3 local-exec and 4 initial-exec.
static const uint8_t tier1_symbol_fold_access[5] = {
    BUSTER_TIER1_ACCESS_DIRECT,
    BUSTER_TIER1_ACCESS_DIRECT,
    BUSTER_TIER1_ACCESS_GOT,
    BUSTER_TIER1_ACCESS_LOCAL_EXEC,
    BUSTER_TIER1_ACCESS_INITIAL_EXEC,
};

static bool tier1_symbol_fold_function(Tier1EmitTest* test, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = BUSTER_TIER1_SYMBOL_FOLD_NODES,
        .block_count = 4,
        .extra_count = 4,
        .constant_count = 2,
        .address_count = 2,
        .parameter_count = 1,
    };
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = IR_I64;
        function->parameter_types[0] = IR_I64;
        function->symbol_count = 5;
        function->constants[0].low = 3;
        function->constants[1].low = 0x1234;
        function->addresses[0] = (IrAddress){ .scale = 1, .offset = 8 };
        function->addresses[1] = (IrAddress){ .scale = 1, .offset = 4 };
        uint32_t next = 0;
        const uint32_t x = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        uint32_t symbols[5] = { BUSTER_IR_NONE };
        for (uint32_t s = 1; s < 5; s += 1)
        {
            symbols[s] = tier1_select_test_append(function, &next, IR_SYMBOL_ADDR, IR_PTR, s, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t word_address = tier1_select_test_append(function, &next, IR_ADDRESS, IR_PTR, symbols[1], BUSTER_IR_NONE, 1);
        const uint32_t word = tier1_select_test_append(function, &next, IR_LOAD, IR_I16, word_address, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t widened_word = tier1_select_test_append(function, &next, IR_ZEXT, IR_I32, word, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t key = tier1_select_test_append(function, &next, IR_CONST, IR_I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t route = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, widened_word, key, IR_NE);
        tier1_select_test_append(function, &next, IR_CBR, IR_VOID, route, 1, 2);
        function->block_offsets[1] = next;
        const uint32_t member = tier1_select_test_append(function, &next, IR_ADDRESS, IR_PTR, symbols[1], BUSTER_IR_NONE, 0);
        const uint32_t direct = tier1_select_test_append(function, &next, IR_LOAD, IR_I64, member, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t got = tier1_select_test_append(function, &next, IR_LOAD, IR_I64, symbols[2], BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t local = tier1_select_test_append(function, &next, IR_LOAD, IR_I64, symbols[3], BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t initial = tier1_select_test_append(function, &next, IR_LOAD, IR_I64, symbols[4], BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t first = tier1_select_test_append(function, &next, IR_ADD, IR_I64, direct, got, BUSTER_IR_NONE);
        const uint32_t second = tier1_select_test_append(function, &next, IR_ADD, IR_I64, first, local, BUSTER_IR_NONE);
        const uint32_t sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, second, initial, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_STORE, IR_VOID, member, x, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_STORE, IR_VOID, symbols[2], sum, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_STORE, IR_VOID, symbols[3], x, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[2] = next;
        const uint32_t byte = tier1_select_test_append(function, &next, IR_LOAD, IR_I8, symbols[1], BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t widened = tier1_select_test_append(function, &next, IR_ZEXT, IR_I32, byte, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t three = tier1_select_test_append(function, &next, IR_CONST, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t equal = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, widened, three, IR_EQ);
        const uint32_t truth = tier1_select_test_append(function, &next, IR_ZEXT, IR_I64, equal, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_STORE, IR_VOID, symbols[4], truth, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[3] = next;
        // The phi's operands, (predecessor block, value) pairs.
        function->extra[0] = 1;
        function->extra[1] = sum;
        function->extra[2] = 2;
        function->extra[3] = truth;
        const uint32_t result = tier1_select_test_append(function, &next, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, result, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->block_offsets[4] = next;
        ready = next == function->instruction_count;
    }
    return ready;
}

// Whether `pattern` folds symbol 1 at `displacement`, rip-relative, reading no base or index.
static bool tier1_symbol_fold_folded(const Tier1SelectPattern* pattern, uint32_t first, int32_t displacement)
{
    return pattern->rip && pattern->stack_object == 1 && pattern->displacement == displacement && pattern->values[first] == BUSTER_IR_NONE && pattern->values[first + 1] == BUSTER_IR_NONE;
}

// The plan's shape with the access column: node 10 is the entry's branch on symbol 1's word at 4, 12 and 19 symbol 1's
// load and store at 8, 26 its byte comparison (or the truth value 27 that takes it), 13, 14 and 15 the other symbols'
// loads, 20, 21 and 28 their stores.
static bool tier1_symbol_fold_shape(const Tier1SelectPlan* plan)
{
    const Tier1SelectPattern* patterns = plan->patterns;
    const uint32_t compare = plan->covered[26] ? 27 : 26;
    bool shape = plan->covered[1] && plan->covered[5] && plan->covered[11] && !plan->covered[2] && !plan->covered[3] && !plan->covered[4];
    shape = shape && patterns[10].rule == TIER1_RULE_BRANCH_COMPARE && patterns[10].compare_memory && patterns[10].compare_immediate && patterns[10].width == 2 &&
            patterns[10].immediate == 0x1234 && tier1_symbol_fold_folded(&patterns[10], 1, 4);
    shape = shape && patterns[12].rule == TIER1_RULE_LOAD_ADDRESS && tier1_symbol_fold_folded(&patterns[12], 0, 8);
    shape = shape && patterns[19].rule == TIER1_RULE_STORE_ADDRESS && tier1_symbol_fold_folded(&patterns[19], 0, 8) && patterns[19].values[2] == 0;
    shape = shape && patterns[compare].compare_memory && patterns[compare].compare_immediate && patterns[compare].width == 1 && patterns[compare].immediate == 3 &&
            tier1_symbol_fold_folded(&patterns[compare], 1, 0);
    const uint32_t accesses[6] = { 13, 14, 15, 20, 21, 28 };
    const uint32_t bases[6] = { 2, 3, 4, 2, 3, 4 };
    for (uint32_t k = 0; k < 6; k += 1)
    {
        shape = shape && !patterns[accesses[k]].rip && patterns[accesses[k]].stack_object == BUSTER_IR_NONE && patterns[accesses[k]].values[0] == bases[k];
    }
    // No node reads symbol 1's address or its members': none is an effective use.
    for (uint32_t use = 0; use < plan->effective_use_count; use += 1)
    {
        shape = shape && plan->effective_use_values[use] != 1 && plan->effective_use_values[use] != 5 && plan->effective_use_values[use] != 11;
    }
    return shape;
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_symbol_fold_call(const void* code, uint64_t x)
{
    uint64_t (*function)(uint64_t);
    memcpy(&function, &code, sizeof(function));
    return function(x);
}

// The code, sized and encoded at the start of a mapping whose last page is the data: symbol 1's 16 bytes at its start,
// symbol 2's 8 at 64, its GOT slot at 128 and symbol 4's offset's GOT slot at 136. Each reference is resolved as a
// linker would: a PC32 or a GOT slot's load to the target plus its addend less the instruction's end, a TPOFF32 to the
// thread-local's offset from the thread pointer. Every marked record has one reference, in order. Returns the mapping
// (MAP_FAILED where a step fails) and its size and data page.
static uint8_t* tier1_symbol_fold_load(Tier1EmitTest* test, const Tier1EmitResult* emitted, uint64_t* bytes, uint8_t** data)
{
    const Tier1SelectCode* code = &emitted->code;
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    const uint64_t code_bytes = (capacity + 4095) & ~UINT64_C(4095);
    *bytes = code_bytes + 4096;
    uint8_t* mapping = mmap(0, *bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint32_t* offsets = malloc((uint64_t)code->count * sizeof(uint32_t));
    // The encoder's vector path stores each group's fixup ends 16 at a time (BUSTER_X86_ENCODER_FIXUP_CAPACITY): sized for
    // the references alone, its last store ran past the allocation into the next chunk of the heap.
    uint32_t* ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(emitted->reference_count) * sizeof(uint32_t));
    bool ready = mapping != MAP_FAILED && offsets != 0 && ends != 0;
    tier1_emit_test_expect(test, ready, "a folded symbol's code and data mapping");
    *data = ready ? mapping + code_bytes : 0;
    if (ready)
    {
        const BranchSizingCheck checked = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
        ready = checked.error == BRANCH_SIZING_OK && size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets).error == BRANCH_SIZING_OK;
        uint32_t marked = 0;
        for (uint32_t r = 0; r < code->count; r += 1)
        {
            marked += (code->records[r].form & BUSTER_X86_FORM_FIXUP) != 0;
        }
        ready = ready && marked == emitted->reference_count && x86_target_validate(test->allowed, code->records, code->count) == code->count &&
                x86_encode(code->records, code->count, mapping, capacity, 0, ends) > 0;
        tier1_emit_test_expect(test, ready, "a folded symbol's code sizes and encodes with one reference per marked record");
    }
    uint64_t thread = 0;
    __asm__("mov %%fs:0, %0" : "=r"(thread));
    const uint64_t address = (uint64_t)(uintptr_t)*data;
    for (uint32_t k = 0; ready && k < emitted->reference_count; k += 1)
    {
        const LinkReference* reference = &emitted->references[k];
        const uint32_t distance = x86_fixup_distance(code->records[reference->instruction].form, reference->field);
        const uint64_t end = (uint64_t)(uintptr_t)mapping + ends[k];
        uint64_t target = 0;
        int64_t value = 0;
        switch (reference->kind)
        {
        case BUSTER_LINK_FIXUP_PC32:
            target = reference->target == 1 ? address : 0;
            value = (int64_t)(target + (uint64_t)reference->addend - end);
            break;
        case BUSTER_LINK_FIXUP_LOAD_GOT:
            target = reference->target == 2 ? address + 128 : 0;
            value = (int64_t)(target + (uint64_t)reference->addend - end);
            break;
        case BUSTER_LINK_FIXUP_GOTTPOFF:
            target = reference->target == 4 ? address + 136 : 0;
            value = (int64_t)(target + (uint64_t)reference->addend - end);
            break;
        case BUSTER_LINK_FIXUP_TPOFF32:
            target = reference->target == 3 ? (uint64_t)(uintptr_t)&tier1_symbol_fold_local : 0;
            value = (int64_t)(target + (uint64_t)reference->addend - thread);
            break;
        default:
            break;
        }
        ready = target != 0 && value == (int64_t)(int32_t)value && distance <= ends[k];
        tier1_emit_test_expect(test, ready, "each reference of a folded symbol's code resolves to its target within 32 bits");
        if (ready)
        {
            const int32_t field = (int32_t)value;
            memcpy(mapping + ends[k] - distance, &field, sizeof(field));
        }
    }
    ready = ready && mprotect(mapping, code_bytes, PROT_READ | PROT_EXEC) == 0;
    tier1_emit_test_expect(test, ready, "a folded symbol's code made executable");
    free(offsets);
    free(ends);
    if (!ready && mapping != MAP_FAILED)
    {
        munmap(mapping, *bytes);
    }
    return ready ? mapping : MAP_FAILED;
}

static void tier1_symbol_fold_execute(Tier1EmitTest* test, const Tier1EmitResult* emitted)
{
    uint64_t bytes = 0;
    uint8_t* data = 0;
    uint8_t* mapping = tier1_symbol_fold_load(test, emitted, &bytes, &data);
    if (mapping != MAP_FAILED && tier1_emit_test_native(test, emitted->code.records, emitted->code.count))
    {
        uint64_t thread = 0;
        __asm__("mov %%fs:0, %0" : "=r"(thread));
        const uint64_t initial = (uint64_t)(uintptr_t)&tier1_symbol_fold_initial - thread;
        const uint64_t samples[] = { 0, 0, 1, UINT64_MAX, UINT64_C(0x8000000000000000), UINT64_C(0x0123456789abcdef), 7, 9 };
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(samples); s += 1)
        {
            const uint64_t x = samples[s];
            // The word at 4 routes: 0x1234 to the comparison's arm, its neighbor to the other.
            const bool compares = (s & 1) == 0;
            const uint64_t word = (uint64_t)(compares ? 0x1234 : 0x1235) << 32;
            uint64_t symbol[2] = { word | (s == 0 ? 3 : UINT64_C(0x5a5a0000005a5a00) | s), UINT64_C(0x1111111111111111) * (s + 1) };
            const uint64_t got = UINT64_C(0x2222) << s;
            const uint64_t slot = (uint64_t)(uintptr_t)(data + 64);
            memcpy(data, symbol, sizeof(symbol));
            memcpy(data + 64, &got, sizeof(got));
            memcpy(data + 128, &slot, sizeof(slot));
            memcpy(data + 136, &initial, sizeof(initial));
            tier1_symbol_fold_local = UINT64_C(0x3333) * (s + 1);
            tier1_symbol_fold_initial = UINT64_C(0x4444) * (s + 1);
            const uint64_t local = tier1_symbol_fold_local;
            const uint64_t before = tier1_symbol_fold_initial;
            const uint64_t sum = symbol[1] + got + local + before;
            const uint64_t truth = (symbol[0] & 0xff) == 3;
            const uint64_t expected = !compares ? sum : truth;
            const uint64_t actual = tier1_symbol_fold_call(mapping, x);
            uint64_t after[2];
            uint64_t got_after = 0;
            memcpy(after, data, sizeof(after));
            memcpy(&got_after, data + 64, sizeof(got_after));
            const bool stored = !compares ? after[0] == symbol[0] && after[1] == x && got_after == sum && tier1_symbol_fold_local == x && tier1_symbol_fold_initial == before :
                                         after[0] == symbol[0] && after[1] == symbol[1] && got_after == got && tier1_symbol_fold_local == local && tier1_symbol_fold_initial == truth;
            tier1_emit_test_expect(test, actual == expected && stored, "folded, GOT and thread-local symbols load, compare and store where C does");
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, bytes);
    }
}

// A straight-line function, p's i64 returned, whose load's plan is made to claim symbol 1 rip-relative beside its base:
// the straight-line consumer refuses it rather than emit a rip-relative record without a reference.
static void tier1_symbol_fold_straight(Tier1EmitTest* test)
{
    const IrSizes sizes = {
        .instruction_count = 3,
        .block_count = 1,
        .parameter_count = 1,
    };
    IrFunction function;
    bool ready = ir_create(&test->input, &sizes, &function) == IR_OK;
    if (ready)
    {
        function.result_type = IR_I64;
        function.parameter_types[0] = IR_PTR;
        function.symbol_count = 2;
        uint32_t next = 0;
        const uint32_t pointer = tier1_select_test_append(&function, &next, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t loaded = tier1_select_test_append(&function, &next, IR_LOAD, IR_I64, pointer, BUSTER_IR_NONE, BUSTER_IR_NONE);
        tier1_select_test_append(&function, &next, IR_RET, IR_VOID, loaded, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function.block_offsets[1] = next;
    }
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    ready = ready && tier1_select_plan(&function, 0, &test->output, &test->scratch, true, &plan, &node) == TIER1_SELECT_OK && tier1_emit_cfg_chain(&plan);
    tier1_emit_test_expect(test, ready, "a straight-line load selects for the straight-line consumer");
    const Tier1EmitOptions options = {
        .register_mask = tier1_liveness_register_mask(),
        .vector = true,
    };
    Tier1EmitResult emitted;
    const Tier1EmitError plain = ready ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    tier1_emit_test_expect(test, plain == TIER1_EMIT_OK, "the straight-line load emits");
    if (ready)
    {
        plan.patterns[1].stack_object = 1;
        plan.patterns[1].rip = true;
    }
    const Tier1EmitError claimed = ready ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_OK;
    tier1_emit_test_expect(test, claimed == TIER1_EMIT_UNSUPPORTED, "the straight-line consumer refuses a folded symbol it has no reference for");
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

// #2601: three indexed accesses of one global in one block, each made by the lowering with its own symbol's address and
// its own sign extension of the index i: t[i] += v, then t[i + 8] read, the sum of both returned. Every pattern reads
// the block's first address and extension, so neither repeats in the code: no node computes the later ones.
static bool tier1_symbol_fold_repeated_function(Tier1EmitTest* test, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 17,
        .block_count = 1,
        .address_count = 2,
        .parameter_count = 2,
    };
    bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = IR_I64;
        function->parameter_types[0] = IR_I32;
        function->parameter_types[1] = IR_I64;
        function->symbol_count = 2;
        function->addresses[0] = (IrAddress){ .scale = 8, .offset = 0 };
        function->addresses[1] = (IrAddress){ .scale = 8, .offset = 64 };
        uint32_t next = 0;
        const uint32_t index = tier1_select_test_append(function, &next, IR_PARAM, IR_I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t value = tier1_select_test_append(function, &next, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        uint32_t loaded = BUSTER_IR_NONE;
        uint32_t sum = BUSTER_IR_NONE;
        for (uint32_t access = 0; access < 3; access += 1)
        {
            const uint32_t table = tier1_select_test_append(function, &next, IR_SYMBOL_ADDR, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t extended = tier1_select_test_append(function, &next, IR_SEXT, IR_I64, index, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t element = tier1_select_test_append(function, &next, IR_ADDRESS, IR_PTR, table, extended, access == 2 ? 1 : 0);
            if (access == 0)
            {
                loaded = tier1_select_test_append(function, &next, IR_LOAD, IR_I64, element, BUSTER_IR_NONE, BUSTER_IR_NONE);
                sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, loaded, value, BUSTER_IR_NONE);
            }
            else if (access == 1)
            {
                tier1_select_test_append(function, &next, IR_STORE, IR_VOID, element, sum, BUSTER_IR_NONE);
            }
            else
            {
                const uint32_t other = tier1_select_test_append(function, &next, IR_LOAD, IR_I64, element, BUSTER_IR_NONE, BUSTER_IR_NONE);
                const uint32_t result = tier1_select_test_append(function, &next, IR_ADD, IR_I64, sum, other, BUSTER_IR_NONE);
                tier1_select_test_append(function, &next, IR_RET, IR_VOID, result, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
        }
        function->block_offsets[1] = next;
        ready = next == function->instruction_count;
    }
    return ready;
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_symbol_fold_call_two(const void* code, uint64_t a, uint64_t b)
{
    uint64_t (*function)(uint64_t, uint64_t);
    memcpy(&function, &code, sizeof(function));
    return function(a, b);
}

static void tier1_symbol_fold_repeated_tests(Tier1EmitTest* test)
{
    for (uint32_t vector = 0; vector < 2; vector += 1)
    {
        IrFunction function;
        const bool created = tier1_symbol_fold_repeated_function(test, &function);
        const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK && ir_verify(&function, &test->scratch, 0).error == IR_OK;
        tier1_emit_test_expect(test, valid, "the repeated indexed accesses' fixture is valid IR");
        Tier1SelectPlan plan;
        uint32_t node = BUSTER_IR_NONE;
        const bool planned = valid && tier1_select_plan(&function, 0, &test->output, &test->scratch, vector != 0, &plan, &node) == TIER1_SELECT_OK;
        // Nodes 2 and 3 are the first address and extension, 7 and 8 the second's, 11 and 12 the third's; 5, 10 and 14
        // the load, the store and the second load.
        bool shared = planned && !plan.covered[2] && !plan.covered[3] && plan.covered[7] && plan.covered[8] && plan.covered[11] && plan.covered[12];
        const uint32_t accesses[3] = { 5, 10, 14 };
        for (uint32_t k = 0; shared && k < 3; k += 1)
        {
            const Tier1SelectPattern* pattern = &plan.patterns[accesses[k]];
            shared = pattern->values[0] == 2 && pattern->values[1] == 3 && pattern->displacement == (k == 2 ? 64 : 0) && !pattern->rip;
        }
        tier1_emit_test_expect(test, shared, "every indexed access reads its block's first symbol address and index extension");
        tier1_emit_test_expect(test, planned && tier1_select_test_plan_twin(&function, 0, &plan, &test->output, &test->scratch), "both selection paths share the first address and extension");
        const Tier1EmitOptions options = {
            .register_mask = tier1_liveness_register_mask(),
            .vector = vector != 0,
            .rematerialize = true,
            .certify = true,
        };
        Tier1EmitResult emitted;
        const Tier1EmitError error = shared ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
        const bool certified = error == TIER1_EMIT_OK && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
        tier1_emit_test_expect(test, certified, "the repeated indexed accesses' code certifies");
        uint32_t leas = 0;
        for (uint32_t r = 0; certified && r < emitted.code.count; r += 1)
        {
            const X86Instruction* record = &emitted.code.records[r];
            leas += (record->form & BUSTER_X86_FORM_NUMBER) == X86_FORM_LEA_R64_M && (record->mode & BUSTER_X86_MODE_RIP) != 0;
        }
        tier1_emit_test_expect(test, !certified || leas == 1, "the table's address is one lea");
        uint64_t bytes = 0;
        uint8_t* data = 0;
        uint8_t* mapping = certified ? tier1_symbol_fold_load(test, &emitted, &bytes, &data) : MAP_FAILED;
        if (mapping != MAP_FAILED && tier1_emit_test_native(test, emitted.code.records, emitted.code.count))
        {
            for (uint32_t i = 0; i < 8; i += 1)
            {
                uint64_t table[16];
                for (uint32_t k = 0; k < 16; k += 1)
                {
                    table[k] = UINT64_C(0x0101010101010101) * (k + 1);
                }
                memcpy(data, table, sizeof(table));
                const uint64_t v = UINT64_C(0x123456789) << i;
                const uint64_t sum = table[i] + v;
                const uint64_t actual = tier1_symbol_fold_call_two(mapping, i, v);
                uint64_t after[16];
                memcpy(after, data, sizeof(after));
                table[i] = sum;
                tier1_emit_test_expect(test, actual == sum + table[i + 8] && memcmp(after, table, sizeof(table)) == 0, "the repeated indexed accesses load, store and return what C does");
            }
        }
        if (mapping != MAP_FAILED)
        {
            munmap(mapping, bytes);
        }
        arena_pop_to(&test->output, 0);
        arena_pop_to(&test->scratch, 0);
        arena_pop_to(&test->input, 0);
    }
}

static void tier1_symbol_fold_tests(Tier1EmitTest* test)
{
    for (uint32_t variant = 0; variant < 4; variant += 1)
    {
        const bool vector = (variant & 1) != 0;
        const bool pointer = (variant & 2) != 0;
        IrFunction function;
        const bool created = tier1_symbol_fold_function(test, &function);
        const bool valid = created && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK && ir_verify(&function, &test->scratch, 0).error == IR_OK;
        tier1_emit_test_expect(test, valid, "the folded symbols' fixture is valid IR");
        IrDominance dominance;
        const bool dominated = valid && tier1_terminal_dominance(test, &function, vector, &dominance);
        const Tier1EmitOptions options = {
            .register_mask = tier1_liveness_register_mask(),
            .vector = vector,
            .rematerialize = true,
            .certify = true,
            .frame_flags = pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
            .symbol_access = tier1_symbol_fold_access,
            .symbol_count = BUSTER_ARRAY_LENGTH(tier1_symbol_fold_access),
        };
        // Without the column every symbol is direct, and folds: the consumer, given the column, refuses each that isn't.
        Tier1SelectPlan all;
        uint32_t node = BUSTER_IR_NONE;
        const bool unreached = dominated && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector, &all, &node) == TIER1_SELECT_OK;
        const bool every = unreached && all.patterns[13].rip && all.patterns[13].stack_object == 2 && all.patterns[14].rip && all.patterns[15].rip && all.covered[2];
        tier1_emit_test_expect(test, every, "without the access column every symbol folds");
        Tier1EmitResult refused;
        const Tier1EmitError mismatch = every ? tier1_emit_function(&all, &options, test->allowed, &test->output, &test->scratch, &refused, &node) : TIER1_EMIT_OK;
        tier1_emit_test_expect(test, mismatch == TIER1_EMIT_INVALID, "the consumer refuses a folded symbol its column reaches indirectly");
        Tier1SelectPlan plan;
        const bool planned = dominated && tier1_select_plan_access(&function, &dominance, tier1_symbol_fold_access, BUSTER_ARRAY_LENGTH(tier1_symbol_fold_access), &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
        const bool shaped = planned && tier1_symbol_fold_shape(&plan) && plan.symbol_access == tier1_symbol_fold_access;
        tier1_emit_test_expect(test, shaped, "a direct symbol folds into its accesses and comparison from another block; GOT and thread-local ones don't");
        if (planned && !shaped && test->failures < 20)
        {
            for (uint32_t n = 0; n < function.instruction_count; n += 1)
            {
                const Tier1SelectPattern* p = &plan.patterns[n];
                fprintf(stderr, "  symbol fold variant=%u node=%u covered=%u rule=%u rip=%u object=%u disp=%d values=%u,%u,%u memory=%u\n", variant, n, plan.covered[n], p->rule, p->rip, p->stack_object, p->displacement,
                        p->values[0], p->values[1], p->values[2], p->compare_memory);
            }
        }
        tier1_emit_test_expect(test, planned && tier1_select_test_plan_twin(&function, &dominance, &plan, &test->output, &test->scratch), "both selection paths fold the same symbols");
        Tier1EmitResult emitted;
        const Tier1EmitError error = shaped ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
        const bool certified = error == TIER1_EMIT_OK && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK;
        tier1_emit_test_expect(test, certified, "the folded symbols' allocation and emitted code certify");
        if (!certified && test->failures < 20)
        {
            fprintf(stderr, "  symbol fold error=%u node=%u variant=%u reason=%u\n", error, node, variant, certified ? 0 : emitted.code_reason);
        }
        if (certified)
        {
            // Symbol 1's four references, each on its record's displacement field: the load and the store at 8, the
            // byte comparison at 0 with its immediate byte after the field, and the branch's word comparison at 4 with
            // its two.
            uint32_t direct = 0;
            uint32_t compares = 0;
            uint32_t sequences = 0;
            for (uint32_t k = 0; k < emitted.reference_count; k += 1)
            {
                const LinkReference* reference = &emitted.references[k];
                const X86Instruction* record = &emitted.code.records[reference->instruction];
                const bool rip = (record->mode & BUSTER_X86_MODE_RIP) != 0 && record->displacement == 0;
                const bool pc32 = reference->kind == BUSTER_LINK_FIXUP_PC32 && reference->target == 1 && reference->field == X86_FIXUP_DISPLACEMENT && rip;
                const bool byte = reference->addend == 0 && (record->form & BUSTER_X86_FORM_NUMBER) == X86_FORM_CMP_RM8_IMM8 && x86_fixup_distance(record->form, X86_FIXUP_DISPLACEMENT) == 5;
                const bool word = reference->addend == 4 && (record->form & BUSTER_X86_FORM_NUMBER) == X86_FORM_CMP_RM16_IMM16 && x86_fixup_distance(record->form, X86_FIXUP_DISPLACEMENT) == 6;
                const bool compare = pc32 && (byte || word);
                direct += pc32 && (reference->addend == 8 || compare);
                compares += compare;
                sequences += (reference->kind == BUSTER_LINK_FIXUP_LOAD_GOT && reference->target == 2) || (reference->kind == BUSTER_LINK_FIXUP_TPOFF32 && reference->target == 3) ||
                             (reference->kind == BUSTER_LINK_FIXUP_GOTTPOFF && reference->target == 4);
            }
            const bool referenced = direct == 4 && compares == 2 && sequences + direct == emitted.reference_count && sequences >= 3;
            tier1_emit_test_expect(test, referenced, "a folded symbol's PC32s are its accesses', and the others' sequences keep theirs");
            if (!referenced && test->failures < 20)
            {
                fprintf(stderr, "  symbol fold references variant=%u direct=%u compares=%u sequences=%u of %u\n", variant, direct, compares, sequences, emitted.reference_count);
            }
            tier1_symbol_fold_execute(test, &emitted);
        }
        arena_pop_to(&test->output, 0);
        arena_pop_to(&test->scratch, 0);
        arena_pop_to(&test->input, 0);
    }
    tier1_symbol_fold_straight(test);
    tier1_symbol_fold_repeated_tests(test);
}
