// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Upper vector registers (#3625), included by tier1_emit_test.c. Twenty independent vectors are simultaneously live
// beside scalar floating values. Their C oracle uses original bytes, not allocated locations or generated records.
// The variants carry them through a rotating phi cycle, a hostile call and a machine row with a separate temporary.
// The call observes its mixed arguments before overwriting every vector register the output target permits.

#define BUSTER_TIER1_UPPER_VALUES 20u
#define BUSTER_TIER1_UPPER_OUTPUT 1536u
#define BUSTER_TIER1_UPPER_BUFFER 3072u
#define BUSTER_TIER1_UPPER_SCALARS (BUSTER_TIER1_UPPER_VALUES * 64u)

// shape 0: straight pressure; 1: phi rotation; 2: indirect call; 3: machine row with temporary vector role 2.
// All signatures are void(buffer, count, helper); the last two arguments are used only where needed.
static bool tier1_upper_vectors_fixture(Tier1EmitTest* test, uint32_t width, uint32_t shape, IrFunction* function)
{
    const bool loop = shape == 1;
    const bool call = shape == 2;
    const bool row = shape == 3;
    const IrType type = width == 16 ? IR_V4I32 : width == 32 ? IR_V8I32 : IR_V16I32;
    const IrSizes sizes = {
        .instruction_count = 192,
        .block_count = loop ? 3 : 1,
        .extra_count = loop ? 4 * (BUSTER_TIER1_UPPER_VALUES + 1) : 4,
        .constant_count = 2,
        .address_count = 48,
        .call_count = call || row,
        .call_type_count = call ? 4 : row ? 2 : 0,
        .parameter_count = 3,
        .row_count = row,
    };
    const bool created = ir_create(&test->input, &sizes, function) == IR_OK;
    if (created)
    {
        uint32_t next = 0;
        uint32_t addresses = 0;
        uint32_t extra = 0;
        function->result_type = IR_VOID;
        function->parameter_types[0] = IR_PTR;
        function->parameter_types[1] = IR_I64;
        function->parameter_types[2] = IR_PTR;
        for (uint32_t p = 0; p < 3; p += 1)
        {
            tier1_select_test_append(function, &next, IR_PARAM, function->parameter_types[p], p, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        uint32_t scalars[2];
        for (uint32_t s = 0; s < 2; s += 1)
        {
            const uint32_t address = tier1_float_address(function, &next, &addresses, 0, BUSTER_TIER1_UPPER_SCALARS + 8 * s);
            scalars[s] = tier1_select_test_append(function, &next, IR_LOAD, s == 0 ? IR_F64 : IR_F32, address, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        uint32_t values[BUSTER_TIER1_UPPER_VALUES];
        for (uint32_t v = 0; v < BUSTER_TIER1_UPPER_VALUES; v += 1)
        {
            const uint32_t address = tier1_float_address(function, &next, &addresses, 0, 64 * v);
            values[v] = tier1_select_test_append(function, &next, IR_LOAD, type, address, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        uint32_t scalar_result = 1;
        uint32_t row_result = BUSTER_IR_NONE;
        if (loop)
        {
            function->constants[0].low = 0;
            function->constants[1].low = 1;
            const uint32_t zero = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_append(function, &next, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function->block_offsets[1] = next;
            const uint32_t first_phi = next;
            for (uint32_t v = 0; v < BUSTER_TIER1_UPPER_VALUES; v += 1)
            {
                function->extra[extra] = 0;
                function->extra[extra + 1] = values[v];
                function->extra[extra + 2] = 1;
                function->extra[extra + 3] = first_phi + (v + 1) % BUSTER_TIER1_UPPER_VALUES;
                values[v] = tier1_select_test_append(function, &next, IR_PHI, type, extra, 2, BUSTER_IR_NONE);
                extra += 4;
            }
            const uint32_t counter_extra = extra;
            const uint32_t counter = tier1_select_test_append(function, &next, IR_PHI, IR_I64, extra, 2, BUSTER_IR_NONE);
            extra += 4;
            const uint32_t one = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t after = tier1_select_test_append(function, &next, IR_ADD, IR_I64, counter, one, BUSTER_IR_NONE);
            const uint32_t more = tier1_select_test_append(function, &next, IR_ICMP, IR_I1, after, 1, IR_ULT);
            tier1_select_test_append(function, &next, IR_CBR, IR_VOID, more, 1, 2);
            function->extra[counter_extra] = 0;
            function->extra[counter_extra + 1] = zero;
            function->extra[counter_extra + 2] = 1;
            function->extra[counter_extra + 3] = after;
            function->block_offsets[2] = next;
        }
        else if (call)
        {
            function->call_types[0] = IR_I64;
            function->call_types[1] = IR_PTR;
            function->call_types[2] = IR_F32;
            function->call_types[3] = IR_F64;
            function->extra[0] = 1;
            function->extra[1] = 0;
            function->extra[2] = scalars[1];
            function->extra[3] = scalars[0];
            function->calls[0] = (IrCall){
                .argument_count = 4,
                .parameter_count = 4,
                .result_type = IR_I64,
            };
            extra = 4;
            scalar_result = tier1_select_test_append(function, &next, IR_CALL_INDIRECT, IR_I64, 2, 0, BUSTER_IR_NONE);
        }
        else if (row)
        {
            const uint16_t form = width == 16 ? X86_FORM_VPADDD_X_X_XM128_EVEX : width == 32 ? X86_FORM_VPADDD_Y_Y_YM256_EVEX : X86_FORM_VPADDD_Z_Z_ZM512;
            function->rows[0] = (Tier0Operation){
                .records = {
                    { .form = form, .reg = BUSTER_TIER0_ROLE_VECTOR(2), .vvvv = BUSTER_TIER0_ROLE_VECTOR(0), .rm = BUSTER_TIER0_ROLE_VECTOR(1) },
                    { .form = form, .reg = BUSTER_TIER0_ROLE_VECTOR(0), .vvvv = BUSTER_TIER0_ROLE_VECTOR(2), .rm = BUSTER_TIER0_ROLE_VECTOR(1) },
                },
                .record_count = 2,
                .operand_count = 2,
                .operand_classes = { BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR },
                .operand_bytes = { (uint8_t)width, (uint8_t)width },
                .result_class = BUSTER_TYPED_CLASS_VECTOR,
                .result_bytes = (uint8_t)width,
            };
            function->call_types[0] = (uint8_t)type;
            function->call_types[1] = (uint8_t)type;
            function->extra[0] = values[0];
            function->extra[1] = values[1];
            function->calls[0] = (IrCall){
                .argument_count = 2,
                .parameter_count = 2,
                .result_type = (uint8_t)type,
                .row = 1,
            };
            extra = 2;
            row_result = tier1_select_test_append(function, &next, IR_CALL, type, 0, 0, BUSTER_IR_NONE);
        }
        for (uint32_t v = 0; v < BUSTER_TIER1_UPPER_VALUES; v += 1)
        {
            const uint32_t doubled = tier1_select_test_append(function, &next, IR_ADD, type, values[v], values[v], BUSTER_IR_NONE);
            tier1_float_store(function, &next, &addresses, 0, BUSTER_TIER1_UPPER_OUTPUT + 64 * v, doubled);
        }
        tier1_float_store(function, &next, &addresses, 0, BUSTER_TIER1_UPPER_OUTPUT + BUSTER_TIER1_UPPER_SCALARS, scalars[0]);
        tier1_float_store(function, &next, &addresses, 0, BUSTER_TIER1_UPPER_OUTPUT + BUSTER_TIER1_UPPER_SCALARS + 8, scalars[1]);
        tier1_float_store(function, &next, &addresses, 0, BUSTER_TIER1_UPPER_OUTPUT + BUSTER_TIER1_UPPER_SCALARS + 16, scalar_result);
        if (row)
        {
            tier1_float_store(function, &next, &addresses, 0, BUSTER_TIER1_UPPER_OUTPUT + 1344, row_result);
        }
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        function->instruction_count = next;
        function->block_offsets[function->block_count] = next;
        function->address_count = addresses;
        function->extra_count = extra;
    }
    return created;
}

static void tier1_upper_vectors_oracle(const uint8_t* input, uint32_t width, uint32_t shape, uint64_t count, uint8_t* expected)
{
    memcpy(expected, input, BUSTER_TIER1_UPPER_BUFFER);
    for (uint32_t v = 0; v < BUSTER_TIER1_UPPER_VALUES; v += 1)
    {
        const uint32_t source = shape == 1 ? (v + (uint32_t)count - 1) % BUSTER_TIER1_UPPER_VALUES : v;
        for (uint32_t lane = 0; lane < width; lane += 4)
        {
            uint32_t value;
            memcpy(&value, input + 64 * source + lane, sizeof(value));
            value += value;
            memcpy(expected + BUSTER_TIER1_UPPER_OUTPUT + 64 * v + lane, &value, sizeof(value));
        }
    }
    memcpy(expected + BUSTER_TIER1_UPPER_OUTPUT + BUSTER_TIER1_UPPER_SCALARS, input + BUSTER_TIER1_UPPER_SCALARS, 8);
    memcpy(expected + BUSTER_TIER1_UPPER_OUTPUT + BUSTER_TIER1_UPPER_SCALARS + 8, input + BUSTER_TIER1_UPPER_SCALARS + 8, 4);
    const uint64_t result = count + (shape == 2 ? 7 : 0);
    memcpy(expected + BUSTER_TIER1_UPPER_OUTPUT + BUSTER_TIER1_UPPER_SCALARS + 16, &result, sizeof(result));
    if (shape == 2)
    {
        memcpy(expected + BUSTER_TIER1_UPPER_OUTPUT + 1408, input + BUSTER_TIER1_UPPER_SCALARS + 8, 4);
        memcpy(expected + BUSTER_TIER1_UPPER_OUTPUT + 1416, input + BUSTER_TIER1_UPPER_SCALARS, 8);
    }
    if (shape == 3)
    {
        for (uint32_t lane = 0; lane < width; lane += 4)
        {
            uint32_t a;
            uint32_t b;
            memcpy(&a, input + lane, sizeof(a));
            memcpy(&b, input + 64 + lane, sizeof(b));
            const uint32_t result_lane = a + 2 * b;
            memcpy(expected + BUSTER_TIER1_UPPER_OUTPUT + 1344 + lane, &result_lane, sizeof(result_lane));
        }
    }
}

// The independently assembled callee records its scalar arguments, destroys every available vector register and
// returns count + 7. A full zmm write detects a narrowed preservation of either the low or the high vector register.
static uint32_t tier1_upper_vectors_helper(const uint8_t* forms, X86Instruction* records)
{
    const bool high = x86_target_form_allowed(forms, X86_FORM_VPXORD_Z_Z_ZM512);
    const bool vex = x86_target_form_allowed(forms, X86_FORM_VPXOR_Y_Y_YM256);
    uint32_t count = 0;
    records[count++] = (X86Instruction){ .form = X86_FORM_MOVSS_XM32_X, .reg = 0, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = BUSTER_TIER1_UPPER_OUTPUT + 1408 };
    records[count++] = (X86Instruction){ .form = X86_FORM_MOVSD_XM64_X, .reg = 1, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = BUSTER_TIER1_UPPER_OUTPUT + 1416 };
    for (uint32_t x = 0; x < (high ? 32u : 16u); x += 1)
    {
        records[count++] = (X86Instruction){
            .form = high ? X86_FORM_VPXORD_Z_Z_ZM512 : vex ? X86_FORM_VPXOR_Y_Y_YM256 : X86_FORM_PXOR_X_XM128,
            .reg = (uint8_t)x,
            .vvvv = high || vex ? (uint8_t)x : 0,
            .rm = (uint8_t)x,
        };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_LEA_R64_M, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 7 };
    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
    return count;
}

static __attribute__((no_sanitize("function"))) void tier1_upper_vectors_call(const void* executable, uint8_t* buffer, uint64_t count, const void* helper)
{
    void (*function)(uint8_t*, uint64_t, const void*);
    memcpy(&function, &executable, sizeof(function));
    function(buffer, count, helper);
}

static uint32_t tier1_upper_vectors_execute(Tier1EmitTest* test, Tier1EmitResult* emitted, uint32_t width, uint32_t shape)
{
    uint32_t runs = 0;
    void* executable = MAP_FAILED;
    uint64_t mapped = 0;
    X86Instruction helper_records[40];
    const uint32_t helper_count = tier1_upper_vectors_helper(test->allowed, helper_records);
    void* helper = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool helper_ready = helper != MAP_FAILED && x86_target_validate(test->allowed, helper_records, helper_count) == helper_count &&
                              x86_encode(helper_records, helper_count, helper, 4096, 0, 0) > 0 && mprotect(helper, 4096, PROT_READ | PROT_EXEC) == 0;
    tier1_emit_test_expect(test, helper_ready, "upper vectors: the independent hostile callee encodes legally");
    if (helper_ready && tier1_emit_test_load(test, &emitted->code, &executable, &mapped))
    {
        uint64_t random = UINT64_C(0x3625ef159a72bc40);
        for (uint32_t sample = 0; sample < 5; sample += 1)
        {
            uint8_t input[BUSTER_TIER1_UPPER_BUFFER];
            uint8_t expected[BUSTER_TIER1_UPPER_BUFFER];
            uint8_t actual[BUSTER_TIER1_UPPER_BUFFER];
            for (uint32_t b = 0; b < sizeof(input); b += 8)
            {
                const uint64_t value = tier1_emit_test_random(&random);
                memcpy(input + b, &value, sizeof(value));
            }
            const double scalar64 = (double)sample + 0.375;
            const float scalar32 = (float)sample - 0.625f;
            memcpy(input + BUSTER_TIER1_UPPER_SCALARS, &scalar64, sizeof(scalar64));
            memcpy(input + BUSTER_TIER1_UPPER_SCALARS + 8, &scalar32, sizeof(scalar32));
            const uint64_t count = sample + 1;
            tier1_upper_vectors_oracle(input, width, shape, count, expected);
            memcpy(actual, input, sizeof(actual));
            tier1_upper_vectors_call(executable, actual, count, helper);
            tier1_emit_test_expect(test, memcmp(expected, actual, sizeof(actual)) == 0, "upper vectors: native bytes equal the independent C oracle");
            runs += 1;
        }
    }
    if (executable != MAP_FAILED)
    {
        munmap(executable, mapped);
    }
    if (helper != MAP_FAILED)
    {
        munmap(helper, 4096);
    }
    return runs;
}

static void tier1_upper_vectors_refusals(Tier1EmitTest* test, const Tier1SelectPlan* plan)
{
    const uint64_t output_mark = test->output.length;
    const uint64_t scratch_mark = test->scratch.length;
    for (uint32_t failure = 0; failure < 3; failure += 1)
    {
        Arena output = test->output;
        Arena scratch = test->scratch;
        Tier1EmitOptions options = {
            .register_mask = tier1_liveness_register_mask(),
            .rematerialize = true,
            .certify = true,
        };
        if (failure == 0)
        {
            // The only requested register is the parallel-move scratch, never an allocated value's home.
            options.vector_register_mask = 1u << 15;
        }
        else if (failure == 1)
        {
            output.capacity = output_mark + 1;
        }
        else
        {
            scratch.capacity = scratch_mark + 1;
        }
        Tier1EmitResult emitted;
        memset(&emitted, 0xa5, sizeof(emitted));
        Tier1EmitResult before;
        memcpy(&before, &emitted, sizeof(before));
        uint32_t failed = BUSTER_IR_NONE;
        const Tier1EmitError error = tier1_emit_function(plan, &options, test->allowed, &output, &scratch, &emitted, &failed);
        tier1_emit_test_expect(test, failure == 0 ? error != TIER1_EMIT_OK : error == TIER1_EMIT_MEMORY, "upper vectors: reserved registers and exhausted arenas refuse emission");
        tier1_emit_test_expect(test, output.length == output_mark && scratch.length == scratch_mark && memcmp(&emitted, &before, sizeof(emitted)) == 0,
                               "upper vectors: refusal restores both arenas and leaves the result unchanged");
    }
}

static void tier1_upper_vectors_tests(Tier1EmitTest* test)
{
    const uint64_t checks_before = test->checks;
    const uint64_t failures_before = test->failures;
    const uint8_t* native_before = test->native_allowed;
    uint8_t* host_forms = malloc(X86_FORM_COUNT);
    const X86Target host = x86_target_native();
    if (host_forms != NULL)
    {
        x86_target_forms(&host, host_forms);
        test->native_allowed = host_forms;
    }
    tier1_emit_test_expect(test, host_forms != NULL, "upper vectors: native target form storage");
    uint32_t qualified = 0;
    uint32_t restricted = 0;
    uint32_t saves = 0;
    uint32_t loops = 0;
    uint32_t register31 = 0;
    uint32_t register31_saves = 0;
    uint32_t native_runs = 0;
    for (uint32_t target_index = 0; host_forms != NULL && target_index < 4; target_index += 1)
    {
        X86Target target = { .architecture = X86_ARCH_BASELINE };
        if (target_index != 0)
        {
            x86_target_parse(target_index == 3 ? "x86-64-v4" : "x86-64-v3", 9, &target);
        }
        if (target_index == 2)
        {
            target.features |= BUSTER_X86_FEATURE(AVX512F);
            target.architecture = X86_ARCH_OTHER;
        }
        x86_target_forms(&target, test->allowed);
        for (uint32_t width = 16; width <= 64; width *= 2)
        {
            const bool legal_width = target_index == 0 ? width == 16 : target_index == 1 ? width != 64 : true;
            const bool high_legal = target_index == 3 || (target_index == 2 && width == 64);
            for (uint32_t shape = 0; legal_width && shape < 4; shape += 1)
            {
                if (shape == 3 && !high_legal)
                {
                    continue;
                }
                IrFunction function;
                const bool created = tier1_upper_vectors_fixture(test, width, shape, &function);
                IrDominance dominance = { 0 };
                const bool valid = created && ir_verify(&function, &test->scratch, 0).error == IR_OK && tier1_terminal_dominance(test, &function, &dominance);
                tier1_emit_test_expect(test, valid, "upper vectors: pressure fixture verifies");
                Tier1SelectPlan plan;
                uint32_t failed = BUSTER_IR_NONE;
                const bool selected = valid && tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &failed) == TIER1_SELECT_OK;
                tier1_emit_test_expect(test, selected, "upper vectors: pressure fixture selects");
                if (selected && target_index == 3 && shape == 0)
                {
                    tier1_upper_vectors_refusals(test, &plan);
                }
                const uint64_t mark = test->output.length;
                uint32_t low_spills = 0;
                for (uint32_t mode = 0; selected && mode < (target_index == 3 ? 3u : 2u); mode += 1)
                {
                    const Tier1EmitOptions options = {
                        .register_mask = tier1_liveness_register_mask(),
                        .vector_register_mask = mode == 0 ? 0x7fffu : mode == 2 ? 0x80007fffu : 0,
                        .frame_flags = mode != 0 ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
                        .rematerialize = true,
                        .certify = true,
                    };
                    Tier1EmitResult emitted = { 0 };
                    const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &failed);
                    tier1_emit_test_expect(test, error == TIER1_EMIT_OK, "upper vectors: pressure fixture emits and certifies");
                    if (error != TIER1_EMIT_OK)
                    {
                        fprintf(stderr, "  upper vectors target=%u width=%u shape=%u mode=%u error=%u node=%u\n", target_index, width, shape, mode, error, failed);
                    }
                    else
                    {
                        uint32_t high = 0;
                        uint32_t high_mask = 0;
                        bool scalar_low = true;
                        for (uint32_t interval = 0; interval < emitted.cfg.intervals.count; interval += 1)
                        {
                            const uint32_t node = emitted.value_nodes[emitted.cfg.intervals.values[interval]];
                            for (uint32_t fragment = emitted.cfg.allocation.fragment_offsets[interval]; fragment < emitted.cfg.allocation.fragment_offsets[interval + 1]; fragment += 1)
                            {
                                const bool upper = emitted.cfg.intervals.classes[interval] == BUSTER_TIER1_ALLOCATE_VECTOR && emitted.cfg.allocation.kinds[fragment] == BUSTER_TIER1_ALLOCATE_REGISTER && emitted.cfg.allocation.registers[fragment] >= 16;
                                high += upper && ir_vector_type(function.type[node]);
                                if (upper && emitted.cfg.allocation.registers[fragment] < 32)
                                {
                                    high_mask |= 1u << emitted.cfg.allocation.registers[fragment];
                                }
                                scalar_low &= !upper || ir_vector_type(function.type[node]);
                            }
                        }
                        tier1_emit_test_expect(test, scalar_low, "upper vectors: scalar floating values retain legal low registers");
                        tier1_emit_test_expect(test, emitted.certified && emitted.code_checked && emitted.code_reason == TIER1_CERT_EMITTED_OK, "upper vectors: allocation and emitted code are certified");
                        tier1_emit_test_expect(test, x86_target_validate(test->allowed, emitted.code.records, emitted.code.count) == emitted.code.count && x86_validate(emitted.code.records, emitted.code.count) == emitted.code.count,
                                               "upper vectors: every register and form is legal for the output target");
                        if (mode == 0)
                        {
                            low_spills = emitted.spill_stores + emitted.spill_loads;
                            tier1_emit_test_expect(test, high == 0 && low_spills != 0, "upper vectors: the constrained allocation really spills without high registers");
                        }
                        else if (mode == 2)
                        {
                            tier1_emit_test_expect(test, high_mask == 0x80000000u, "upper vectors: the highest register is an actual allocated value's home");
                            register31 += high_mask == 0x80000000u;
                            if (shape == 2)
                            {
                                const bool saved31 = (emitted.prepared.vector_save_mask & 0x80000000u) != 0;
                                tier1_emit_test_expect(test, saved31, "upper vectors: register 31 survives a call's full-width clobber");
                                register31_saves += saved31;
                            }
                        }
                        else if (high_legal)
                        {
                            tier1_emit_test_expect(test, high != 0 && emitted.spill_stores + emitted.spill_loads < low_spills, "upper vectors: high allocation removes avoidable vector spills");
                            qualified += high != 0;
                            if (shape == 2)
                            {
                                const bool saved_high = (emitted.prepared.vector_save_mask & 0xffff0000u) != 0;
                                tier1_emit_test_expect(test, saved_high, "upper vectors: the hostile call saves allocated high registers");
                                saves += saved_high;
                            }
                            if (shape == 1)
                            {
                                uint32_t scratch_moves = 0;
                                const uint32_t scratch = BUSTER_TIER1_EMIT_VECTOR_LOCATION + BUSTER_TIER1_SELECT_FLOAT_SCRATCH;
                                for (uint32_t move = 0; move < emitted.cfg.move_count; move += 1)
                                {
                                    scratch_moves += emitted.cfg.move_sources[move] == scratch || emitted.cfg.move_destinations[move] == scratch;
                                }
                                tier1_emit_test_expect(test, scratch_moves != 0, "upper vectors: the high-register phi cycle uses the reserved move scratch");
                                loops += scratch_moves != 0;
                            }
                        }
                        else
                        {
                            tier1_emit_test_expect(test, high == 0, "upper vectors: legacy, VEX and narrow foundation values stay below 16");
                            restricted += 1;
                        }
                        native_runs += tier1_upper_vectors_execute(test, &emitted, width, shape);
                    }
                    arena_pop_to(&test->output, mark);
                }
                arena_pop_to(&test->output, 0);
                arena_pop_to(&test->input, 0);
            }
        }
    }
    tier1_emit_test_expect(test, qualified == 16 && restricted == 15 && saves == 4 && loops == 4 && register31 == 12 && register31_saves == 3,
                           "upper vectors: every width, target boundary, call, loop and register 31 was exercised");
    printf("tier1 upper vectors: %" PRIu64 " checks, %" PRIu64 " failures; qualified=%u restricted=%u high-call-saves=%u phi-cycles=%u register31=%u register31-saves=%u native-runs=%u\n",
           test->checks - checks_before, test->failures - failures_before, qualified, restricted, saves, loops, register31, register31_saves, native_runs);
    const X86Target baseline = { .architecture = X86_ARCH_BASELINE };
    x86_target_forms(&baseline, test->allowed);
    test->native_allowed = native_before;
    free(host_forms);
}
