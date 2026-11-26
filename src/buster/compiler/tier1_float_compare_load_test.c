// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2829: scalar floating comparisons with a memory operand, included by tier1_emit_test.c after
// tier1_float_select_emit_test.c. Generated IR covers predicates, consumers, addresses and motion barriers;
// a register UCOMIS hardware oracle checks native truth values and MXCSR independently of the selected form.
// The readable input ends at a guard page, so an F32 comparison cannot silently widen its four-byte read.

typedef struct Tier1FloatCompareLoadCase Tier1FloatCompareLoadCase;
struct Tier1FloatCompareLoadCase
{
    uint32_t width;
    uint32_t predicate;
    uint32_t consumer;
    uint32_t address;
    uint32_t scale;
    uint32_t barrier;
    bool left;
    bool after;
};

enum
{
    TIER1_FLOAT_COMPARE_LOAD_PLAIN,
    TIER1_FLOAT_COMPARE_LOAD_VOLATILE,
    TIER1_FLOAT_COMPARE_LOAD_ATOMIC,
    TIER1_FLOAT_COMPARE_LOAD_SHARED,
    TIER1_FLOAT_COMPARE_LOAD_STORE,
    TIER1_FLOAT_COMPARE_LOAD_READ,
    TIER1_FLOAT_COMPARE_LOAD_FENCE,
    TIER1_FLOAT_COMPARE_LOAD_CALL,
    TIER1_FLOAT_COMPARE_LOAD_BLOCK,
};

static void tier1_float_compare_load_barrier(IrFunction* function, uint32_t* next, const Tier1FloatCompareLoadCase* item, uint32_t address, uint32_t other)
{
    if (item->barrier == TIER1_FLOAT_COMPARE_LOAD_STORE)
    {
        tier1_select_test_append(function, next, IR_STORE, IR_VOID, address, other, BUSTER_IR_NONE);
    }
    else if (item->barrier == TIER1_FLOAT_COMPARE_LOAD_READ)
    {
        const uint32_t read = tier1_select_test_append(function, next, IR_LOAD, IR_I8, address, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->flags[read] = BUSTER_IR_VOLATILE;
    }
    else if (item->barrier == TIER1_FLOAT_COMPARE_LOAD_FENCE)
    {
        tier1_select_test_append(function, next, IR_FENCE, IR_VOID, BUSTER_IR_FENCE_SEQ_CST, BUSTER_IR_NONE, BUSTER_IR_NONE);
    }
    else if (item->barrier == TIER1_FLOAT_COMPARE_LOAD_CALL)
    {
        tier1_select_test_append(function, next, IR_CALL, IR_VOID, 1, 0, BUSTER_IR_NONE);
    }
}

static bool tier1_float_compare_load_function(Tier1EmitTest* test, const Tier1FloatCompareLoadCase* item, IrFunction* function, uint32_t* comparison, uint32_t* root, uint32_t* load)
{
    const bool branch = item->consumer == 1;
    const bool separated = item->barrier == TIER1_FLOAT_COMPARE_LOAD_BLOCK;
    const IrType type = item->width == 4 ? IR_F32 : IR_F64;
    const IrSizes sizes =
    {
        .instruction_count = 32,
        .block_count = (branch ? 3u : 1u) + separated,
        .parameter_count = 5,
        .address_count = item->address == 1,
        .slot_count = item->address == 2,
        .call_count = item->barrier == TIER1_FLOAT_COMPARE_LOAD_CALL,
    };
    const bool created = ir_create(&test->input, &sizes, function) == IR_OK;
    if (created)
    {
        function->result_type = IR_I64;
        function->parameter_types[0] = IR_PTR;
        for (uint32_t p = 1; p < 5; p += 1)
        {
            function->parameter_types[p] = IR_I64;
        }
        uint32_t next = 0;
        for (uint32_t p = 0; p < 5; p += 1)
        {
            tier1_select_test_append(function, &next, IR_PARAM, (IrType)function->parameter_types[p], p, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t bits = item->width == 4 ? tier1_select_test_append(function, &next, IR_TRUNC, IR_I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE) : 1;
        const uint32_t other = tier1_select_test_append(function, &next, IR_BITCAST, type, bits, BUSTER_IR_NONE, BUSTER_IR_NONE);
        uint32_t address = 0;
        if (item->address == 1)
        {
            function->addresses[0] = (IrAddress)
            {
                .offset = -16,
                .scale = item->scale,
            };
            address = tier1_select_test_append(function, &next, IR_ADDRESS, IR_PTR, 0, 2, 0);
        }
        else if (item->address == 2)
        {
            function->slots[0] = (IrSlot)
            {
                .size = item->width,
                .alignment = 16,
                .type = (uint8_t)type,
                .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN,
            };
            const uint32_t initial = tier1_select_test_append(function, &next, IR_LOAD, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            address = tier1_select_test_append(function, &next, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_append(function, &next, IR_STORE, IR_VOID, address, initial, BUSTER_IR_NONE);
        }
        const bool atomic = item->barrier == TIER1_FLOAT_COMPARE_LOAD_ATOMIC;
        const IrType load_type = atomic ? (item->width == 4 ? IR_I32 : IR_I64) : type;
        *load = tier1_select_test_append(function, &next, IR_LOAD, load_type, address, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->flags[*load] = item->barrier == TIER1_FLOAT_COMPARE_LOAD_VOLATILE ? BUSTER_IR_VOLATILE : item->barrier == TIER1_FLOAT_COMPARE_LOAD_ATOMIC ? BUSTER_IR_ATOMIC : 0;
        function->alignment_log2[*load] = item->width == 4 ? 2 : 3;
        // Atomic floating C storage arrives as an atomic integer read followed by a bitcast.
        const uint32_t value = atomic ? tier1_select_test_append(function, &next, IR_BITCAST, type, *load, BUSTER_IR_NONE, BUSTER_IR_NONE) : *load;
        if (separated)
        {
            tier1_select_test_append(function, &next, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function->block_offsets[1] = next;
        }
        if (item->barrier == TIER1_FLOAT_COMPARE_LOAD_CALL)
        {
            function->symbol_count = 2;
            function->calls[0].result_type = IR_VOID;
        }
        if (!item->after)
        {
            tier1_float_compare_load_barrier(function, &next, item, address, other);
        }
        const bool shared = item->barrier == TIER1_FLOAT_COMPARE_LOAD_SHARED;
        const uint32_t a = item->left || shared ? value : other;
        const uint32_t b = item->left && !shared ? other : value;
        *comparison = tier1_select_test_append(function, &next, IR_FCMP, IR_I1, a, b, item->predicate);
        if (item->after)
        {
            tier1_float_compare_load_barrier(function, &next, item, address, other);
        }
        // A truth copy makes the branch immediately consume its condition even when the comparison preceded a
        // barrier. The fold must then check the branch's position, not only the original comparison's position.
        const bool copy = (item->address == 1 || item->after) && item->consumer != 0;
        const uint32_t truth = copy ? tier1_select_test_append(function, &next, IR_COPY, IR_I1, *comparison, BUSTER_IR_NONE, BUSTER_IR_NONE) : *comparison;
        *root = *comparison;
        if (branch)
        {
            const uint32_t first = separated ? 2 : 1;
            *root = tier1_select_test_append(function, &next, IR_CBR, IR_VOID, truth, first, first + 1);
            function->block_offsets[first] = next;
            tier1_select_test_append(function, &next, IR_RET, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function->block_offsets[first + 1] = next;
            tier1_select_test_append(function, &next, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        else
        {
            uint32_t result;
            if (item->consumer == 2)
            {
                result = tier1_select_test_append(function, &next, IR_SELECT, IR_I64, truth, 3, 4);
                *root = result;
            }
            else
            {
                result = tier1_select_test_append(function, &next, IR_ZEXT, IR_I64, truth, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            tier1_select_test_append(function, &next, IR_RET, IR_VOID, result, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        function->instruction_count = next;
        function->block_offsets[function->block_count] = next;
    }
    return created;
}

// Read all three flags from an explicit register comparison. Predicate evaluation never performs another floating
// operation, so quiet/signaling NaNs, denormals and existing sticky flags are observed exactly as the hardware sets them.
static bool tier1_float_compare_load_oracle(uint32_t width, uint32_t predicate, uint64_t a, uint64_t b)
{
    uint8_t below;
    uint8_t equal;
    uint8_t unordered;
    if (width == 4)
    {
        float left;
        float right;
        memcpy(&left, &a, sizeof(left));
        memcpy(&right, &b, sizeof(right));
        __asm__ volatile ("ucomiss %4, %3; setb %0; sete %1; setp %2" : "=qm"(below), "=qm"(equal), "=qm"(unordered) : "x"(left), "x"(right) : "cc", "memory");
    }
    else
    {
        double left;
        double right;
        memcpy(&left, &a, sizeof(left));
        memcpy(&right, &b, sizeof(right));
        __asm__ volatile ("ucomisd %4, %3; setb %0; sete %1; setp %2" : "=qm"(below), "=qm"(equal), "=qm"(unordered) : "x"(left), "x"(right) : "cc", "memory");
    }
    const bool holds = predicate == IR_EQ ? equal && !unordered : predicate == IR_NE ? !equal || unordered :
                       predicate == IR_LT ? below && !unordered : predicate == IR_LE ? (below || equal) && !unordered :
                       predicate == IR_GT ? !below && !equal : !below;
    return holds;
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_float_compare_load_call(const void* code, const void* address, uint64_t bits, uint64_t index, uint64_t yes, uint64_t no)
{
    uint64_t (*function)(const void*, uint64_t, uint64_t, uint64_t, uint64_t);
    memcpy(&function, &code, sizeof(function));
    return function(address, bits, index, yes, no);
}

static void tier1_float_compare_load_execute(Tier1EmitTest* test, const Tier1FloatCompareLoadCase* item, Tier1SelectCode* code, uint8_t* edge)
{
    static const uint64_t samples[2][9] =
    {
        {
            0,
            UINT64_C(0x80000000),
            UINT64_C(0x3f800000),
            UINT64_C(0xc0000000),
            UINT64_C(0x7f800000),
            UINT64_C(0xff800000),
            UINT64_C(0x7fc01234),
            UINT64_C(0x7f801234),
            1,
        },
        {
            0,
            UINT64_C(0x8000000000000000),
            UINT64_C(0x3ff0000000000000),
            UINT64_C(0xc000000000000000),
            UINT64_C(0x7ff0000000000000),
            UINT64_C(0xfff0000000000000),
            UINT64_C(0x7ff8000000001234),
            UINT64_C(0x7ff0000000001234),
            1,
        },
    };
    const uint32_t saved = _mm_getcsr();
    const uint64_t yes = UINT64_C(0x1020304050607080);
    const uint64_t no = UINT64_C(0xf1e2d3c4b5a69788);
    void* executable = MAP_FAILED;
    uint64_t bytes = 0;
    if (tier1_emit_test_load_unmeasured(test, code, &executable, &bytes))
    {
        uint8_t* const input = edge - item->width;
        const void* const address = item->address == 1 ? input + 16 - 3 * item->scale : input;
        for (uint32_t mode = 0; mode < 2; mode += 1)
        {
            const uint32_t environment = (saved | _MM_MASK_MASK) & ~(uint32_t)(_MM_EXCEPT_MASK | _MM_ROUND_MASK | _MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK);
            const uint32_t controls = environment | (mode != 0 ? _MM_ROUND_DOWN | _MM_EXCEPT_DIV_ZERO | _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON : 0);
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(samples[0]); i += 1)
            {
                for (uint32_t j = 0; j < BUSTER_ARRAY_LENGTH(samples[0]); j += 1)
                {
                    const uint64_t memory = samples[item->width == 8][i];
                    const uint64_t other = samples[item->width == 8][j];
                    memcpy(input, &memory, item->width);
                    _mm_setcsr(controls);
                    const bool truth = tier1_float_compare_load_oracle(item->width, item->predicate, item->left ? memory : other, item->left ? other : memory);
                    const uint32_t expected_csr = _mm_getcsr();
                    _mm_setcsr(controls);
                    const uint64_t result = tier1_float_compare_load_call(executable, address, other, 3, yes, no);
                    const uint32_t actual_csr = _mm_getcsr();
                    _mm_setcsr(saved);
                    const uint64_t expected = item->consumer == 0 ? (uint64_t)truth : truth ? yes : no;
                    if ((result != expected || actual_csr != expected_csr) && test->failures < 20)
                    {
                        fprintf(stderr, "  compare-load width=%u pred=%u consumer=%u address=%u left=%u barrier=%u after=%u sample=%u,%u csr=%x/%x result=%" PRIx64 "/%" PRIx64 "\n",
                                item->width, item->predicate, item->consumer, item->address, item->left, item->barrier, item->after, i, j, actual_csr, expected_csr, result, expected);
                    }
                    tier1_emit_test_expect(test, result == expected && actual_csr == expected_csr, "memory comparison preserves hardware truth and MXCSR at an exact-width guard boundary");
                }
            }
        }
    }
    _mm_setcsr(saved);
    if (executable != MAP_FAILED)
    {
        munmap(executable, bytes);
    }
}

static void tier1_float_compare_load_case(Tier1EmitTest* test, const Tier1FloatCompareLoadCase* item, uint8_t* edge, bool execute)
{
    IrFunction function;
    uint32_t comparison = BUSTER_IR_NONE;
    uint32_t root = BUSTER_IR_NONE;
    uint32_t load = BUSTER_IR_NONE;
    const bool created = tier1_float_compare_load_function(test, item, &function, &comparison, &root, &load);
    tier1_emit_test_expect(test, created, "create generated floating comparison load IR");
    const IrCheck verified = created ? ir_verify(&function, &test->scratch, 0) : (IrCheck){ .error = IR_ERROR_MEMORY };
    const bool valid = verified.error == IR_OK;
    if (!valid && test->failures < 20)
    {
        fprintf(stderr, "  compare-load IR width=%u pred=%u consumer=%u address=%u left=%u barrier=%u after=%u error=%u instruction=%u detail=%u\n",
                item->width, item->predicate, item->consumer, item->address, item->left, item->barrier, item->after, verified.error, verified.instruction, verified.detail);
    }
    tier1_emit_test_expect(test, valid, "floating comparison load fixture is valid IR");
    IrDominance dominance;
    const bool dominated = valid && tier1_terminal_dominance(test, &function, &dominance);
    for (uint32_t cover = 0; dominated && cover < 2; cover += 1)
    {
        Tier1SelectPlan plan;
        uint32_t failed = BUSTER_IR_NONE;
        const Tier1SelectError selected = cover == 0 ? tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &failed) :
                                                   tier1_select_test_plan_munch(&function, &dominance, &test->output, &test->scratch, &plan, &failed);
        tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "both covers select floating comparison load candidates");
        if (selected == TIER1_SELECT_OK)
        {
            const uint32_t actual = plan.patterns[root].rule == TIER1_RULE_FLOAT_BRANCH_COMPARE || plan.patterns[root].rule == TIER1_RULE_FLOAT_SELECT_COMPARE ? root : comparison;
            const bool position = item->predicate == IR_EQ || item->predicate == IR_NE || item->left == (item->predicate == IR_LT || item->predicate == IR_LE);
            const bool barrier_after_comparison = item->after && actual == comparison && item->barrier >= TIER1_FLOAT_COMPARE_LOAD_STORE && item->barrier <= TIER1_FLOAT_COMPARE_LOAD_CALL;
            const bool folds = position && (item->barrier == TIER1_FLOAT_COMPARE_LOAD_PLAIN || barrier_after_comparison);
            const bool observed = plan.patterns[actual].compare_memory;
            if (observed != folds && test->failures < 20)
            {
                fprintf(stderr, "  compare-load cover=%u width=%u pred=%u consumer=%u address=%u left=%u barrier=%u after=%u rule=%u memory=%u/%u\n",
                        cover, item->width, item->predicate, item->consumer, item->address, item->left, item->barrier, item->after, plan.patterns[actual].rule, observed, folds);
            }
            tier1_emit_test_expect(test, observed == folds, "only a legal operand folds at the final comparison consumer");
            tier1_emit_test_expect(test, (plan.covered[load] != 0) == folds, "the load has one owner and survives every refused fold");
            if (execute && cover == 0)
            {
                const Tier1EmitOptions options =
                {
                    .register_mask = tier1_liveness_register_mask(),
                    .rematerialize = true,
                    .certify = true,
                    .frame_flags = item->address == 2 ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
                };
                Tier1EmitResult emitted;
                const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
                tier1_emit_test_expect(test, error == TIER1_EMIT_OK && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK, "floating memory comparison allocation and emitted code certify");
                if (error == TIER1_EMIT_OK)
                {
                    uint32_t memories = 0;
                    for (uint32_t r = 0; r < emitted.code.count; r += 1)
                    {
                        const X86Instruction record = emitted.code.records[r];
                        const bool compare = record.form == X86_FORM_UCOMISS_X_XM32 || record.form == X86_FORM_UCOMISD_X_XM64 ||
                                             record.form == X86_FORM_VUCOMISS_X_XM32 || record.form == X86_FORM_VUCOMISD_X_XM64;
                        memories += compare && (record.mode & BUSTER_X86_MODE_MEMORY) != 0;
                    }
                    tier1_emit_test_expect(test, memories == (uint32_t)folds, "emission uses precisely the planned scalar memory compare");
                    tier1_float_compare_load_execute(test, item, &emitted.code, edge);
                }
            }
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    arena_pop_to(&test->input, 0);
}

// The GPR holding the boolean may also hold the memory address. Clearing it before the compare would change the read;
// test both the base and index aliases explicitly, independently of allocator choices.
static void tier1_float_compare_load_aliases(Tier1EmitTest* test, uint8_t* edge)
{
    static const uint8_t destinations[] =
    {
        X86_REGISTER_RDI,
        X86_REGISTER_RDX,
        X86_REGISTER_RAX,
    };
    for (uint32_t width = 4; width <= 8; width += 4)
    {
        for (uint32_t predicate = IR_EQ; predicate <= IR_GE; predicate += 1)
        {
            for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(destinations); d += 1)
            {
                const Tier1SelectHoles holes =
                {
                    .rule = TIER1_RULE_FLOAT_COMPARE,
                    .width = (uint8_t)width,
                    .destination = destinations[d],
                    .left = 16,
                    .condition = (uint8_t)predicate,
                    .compare_memory = true,
                    .memory =
                    {
                        .rm = X86_REGISTER_RDI,
                        .index = X86_REGISTER_RDX,
                        .mode = BUSTER_X86_MEMORY_BASE_INDEX(3),
                        .displacement = -24,
                    },
                };
                const Tier1SelectRecords expanded = tier1_select_expand(&holes);
                X86Instruction records[16];
                uint32_t count = 0;
                records[count++] = tier1_select_register_record(tier1_select_float_forms(width)->from_general, 0, X86_REGISTER_RSI);
                memcpy(records + count, expanded.records, expanded.count * sizeof(*records));
                count += expanded.count;
                records[count++] = tier1_select_move(8, X86_REGISTER_RAX, holes.destination);
                records[count++] = (X86Instruction)
                {
                    .form = X86_FORM_RET,
                };
                Tier1SelectCode code =
                {
                    .records = records,
                    .count = count,
                };
                const Tier1FloatCompareLoadCase item =
                {
                    .width = width,
                    .predicate = predicate,
                    .left = predicate == IR_LT || predicate == IR_LE,
                };
                tier1_float_compare_load_execute(test, &item, &code, edge);
            }
        }
    }
}

static void tier1_float_compare_load_tests(Tier1EmitTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* const mapping = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    tier1_emit_test_expect(test, guarded, "floating comparisons have an exact-width guarded input");
    if (guarded)
    {
        test->terms = 5;
        test->selects = false;
        for (uint32_t width = 4; width <= 8; width += 4)
        {
            test->width = width;
            for (uint32_t predicate = IR_EQ; predicate <= IR_GE; predicate += 1)
            {
                for (uint32_t consumer = 0; consumer < 3; consumer += 1)
                {
                    for (uint32_t address = 0; address < 3; address += 1)
                    {
                        for (uint32_t left = 0; left < 2; left += 1)
                        {
                            const Tier1FloatCompareLoadCase item =
                            {
                                .width = width,
                                .predicate = predicate,
                                .consumer = consumer,
                                .address = address,
                                .scale = 1u << ((predicate + consumer) & 3),
                                .left = left != 0,
                            };
                            tier1_float_compare_load_case(test, &item, mapping + page, true);
                        }
                    }
                }
            }
            for (uint32_t consumer = 0; consumer < 3; consumer += 1)
            {
                for (uint32_t barrier = TIER1_FLOAT_COMPARE_LOAD_VOLATILE; barrier <= TIER1_FLOAT_COMPARE_LOAD_BLOCK; barrier += 1)
                {
                    for (uint32_t after = 0; after <= (uint32_t)(consumer != 0); after += 1)
                    {
                        const Tier1FloatCompareLoadCase item =
                        {
                            .width = width,
                            .predicate = IR_GT,
                            .consumer = consumer,
                            .scale = 1,
                            .barrier = barrier,
                            .after = after != 0,
                        };
                        const bool execute = barrier == TIER1_FLOAT_COMPARE_LOAD_STORE && after != 0;
                        tier1_float_compare_load_case(test, &item, mapping + page, execute);
                    }
                }
            }
        }
        tier1_float_compare_load_aliases(test, mapping + page);
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
}
