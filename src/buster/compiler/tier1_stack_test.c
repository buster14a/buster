// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent native fixtures for arguments and parameters past the registers (#141), included after
// tier1_dropped_test.c: two functions of original IR through tier1_emit_function, which gives them to the CFG consumer,
// by both paths, with constants rematerialized and not, with every register and with four, and with and without a kept
// frame pointer.
//
// - tier1_stack_parameters_function takes twelve parameters of every type an IR parameter may have, six of them on the
//   stack, and C calls it with random 64-bit values in every register and slot: each parameter must read only its own
//   bits (a narrow one's low ones, which the caller leaves dirty above them), the stack ones from the incoming area,
//   past rsp's reservation or rbp, a realigned frame's among them. A fourth fixture of it calls a C function through its
//   first parameter, the others live across the call: in callee-saved registers, which the prologue pushes before its
//   subtraction, past which the incoming area is where the frame keeps no pointer.
// - tier1_stack_calls_function calls three C functions through pointers: one of 16 arguments, ten of them on the stack
//   (two 32-bit ones, a pointer and a constant among them), one of 7, one of them a call's result, and a variadic one
//   whose third to fifth unnamed arguments are on the stack. Each C callee folds its arguments by their positions and
//   notes whether the stack was aligned at its call; the oracle makes the same calls from C.
//
// The code runs in a bounded child (tier1_stack_tests).

#define BUSTER_TIER1_STACK_SAMPLES 32u
#define BUSTER_TIER1_STACK_PARAMETERS 12u
#define BUSTER_TIER1_STACK_ARGUMENTS 16u
#define BUSTER_TIER1_STACK_CONSTANT UINT64_C(0x0123456789abcdef)

// The parameters' types: six in registers, then six on the stack. How each reaches 64 bits: I64 as it is, a pointer
// through ptrtoint, and a narrow one zero-extended (0) or sign-extended (1).
static const uint8_t tier1_stack_parameter_types[BUSTER_TIER1_STACK_PARAMETERS] = { IR_I64, IR_I32, IR_PTR, IR_I64, IR_I16, IR_I64, IR_I32, IR_I64, IR_I16, IR_PTR, IR_I8, IR_I64 };
static const uint8_t tier1_stack_parameter_signed[BUSTER_TIER1_STACK_PARAMETERS] = { 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0 };

// The first call's arguments' types: v4, v7 and v12 truncated to 32 bits, v10 a pointer, the others 64-bit integers.
static uint8_t tier1_stack_argument_type(uint32_t argument)
{
    return argument == 4 || argument == 7 || argument == 12 ? IR_I32 : argument == 10 ? IR_PTR : IR_I64;
}

// Whether any C callee found the stack misaligned at its call: rbp, which __builtin_frame_address reads, is 16 bytes
// aligned where rsp was at the call.
static volatile uint32_t tier1_stack_misaligned;

static void tier1_stack_check_alignment(void)
{
    tier1_stack_misaligned |= ((uintptr_t)__builtin_frame_address(0) & 15) != 0;
}

static uint64_t tier1_stack_fold(const uint64_t* values, uint32_t count, uint64_t seed)
{
    uint64_t h = seed;
    for (uint32_t k = 0; k < count; k += 1)
    {
        h = (h ^ values[k]) * UINT64_C(0x100000001b3) + k;
    }
    return h;
}

static __attribute__((noinline)) uint64_t tier1_stack_callee16(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint32_t a4, uint64_t a5, uint64_t a6, uint32_t a7, uint64_t a8, uint64_t a9, const void* a10, uint64_t a11,
                                                                uint32_t a12, uint64_t a13, uint64_t a14, uint64_t a15)
{
    tier1_stack_check_alignment();
    const uint64_t values[BUSTER_TIER1_STACK_ARGUMENTS] = { a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, (uint64_t)(uintptr_t)a10, a11, a12, a13, a14, a15 };
    return tier1_stack_fold(values, BUSTER_TIER1_STACK_ARGUMENTS, UINT64_C(0x243f6a8885a308d3));
}

// The calling parameters' fixture's callee, called through its first parameter.
static __attribute__((noinline)) uint64_t tier1_stack_callee_one(uint64_t x)
{
    tier1_stack_check_alignment();
    return x * 5 + 3;
}

static __attribute__((noinline)) uint64_t tier1_stack_callee7(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    tier1_stack_check_alignment();
    const uint64_t values[7] = { a0, a1, a2, a3, a4, a5, a6 };
    return tier1_stack_fold(values, 7, UINT64_C(0x13198a2e03707344));
}

static __attribute__((noinline)) uint64_t tier1_stack_callee_variadic(uint64_t count, ...)
{
    tier1_stack_check_alignment();
    uint64_t values[8] = { 0 };
    __builtin_va_list arguments;
    __builtin_va_start(arguments, count);
    for (uint32_t k = 0; k < count && k < 8; k += 1)
    {
        values[k] = __builtin_va_arg(arguments, uint64_t);
    }
    __builtin_va_end(arguments);
    return tier1_stack_fold(values, count < 8 ? (uint32_t)count : 8, UINT64_C(0xa4093822299f31d0));
}

// f(p0, ..., p11) = h, from h = e11 and then h = h * 31 + ek for k from 10 down to 0, ek parameter k at 64 bits. Each
// extension comes just before its step, so that every parameter is live from the entry to its own step, the stack ones
// first. With `realigned`, p0 goes through a 64-byte object aligned to 64 at the entry and comes back for the last
// step: the object realigns the frame, whose incoming area rbp then reaches. With `calls`, e0 is instead the result of
// a call through p0 of p3, the call at the entry: every other parameter lives across it.
static bool tier1_stack_parameters_function(Arena* input, bool realigned, bool calls, IrFunction* function)
{
    uint32_t extensions = 0;
    for (uint32_t k = 0; k < BUSTER_TIER1_STACK_PARAMETERS; k += 1)
    {
        extensions += tier1_stack_parameter_types[k] != IR_I64;
    }
    const IrSizes sizes = {
        .instruction_count = BUSTER_TIER1_STACK_PARAMETERS + 1 + extensions + 2 * (BUSTER_TIER1_STACK_PARAMETERS - 1) + (realigned ? 3 : 0) + (calls ? 2 : 0) + 1,
        .block_count = 1,
        .extra_count = calls ? 1 : 0,
        .constant_count = 1,
        .slot_count = realigned ? 1 : 0,
        .call_count = calls ? 1 : 0,
        .call_type_count = calls ? 1 : 0,
        .parameter_count = BUSTER_TIER1_STACK_PARAMETERS,
    };
    const bool created = ir_create(input, &sizes, function) == IR_OK;
    if (created)
    {
        function->block_offsets[1] = (uint32_t)sizes.instruction_count;
        function->result_type = IR_I64;
        function->incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = BUSTER_ABI_INTEGER_ARGUMENTS, .stack_bytes = BUSTER_ABI_SLOT_BYTES * (BUSTER_TIER1_STACK_PARAMETERS - BUSTER_ABI_INTEGER_ARGUMENTS) };
        function->constants[0].low = 31;
        if (realigned)
        {
            function->slots[0] = (IrSlot){ .size = 64, .alignment = 64, .type = IR_I64, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
        }
        uint32_t next = 0;
        for (uint32_t k = 0; k < BUSTER_TIER1_STACK_PARAMETERS; k += 1)
        {
            function->parameter_types[k] = tier1_stack_parameter_types[k];
            tier1_select_test_append(function, &next, IR_PARAM, tier1_stack_parameter_types[k], k, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t constant = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        uint32_t object = BUSTER_IR_NONE;
        if (realigned)
        {
            object = tier1_select_test_append(function, &next, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_select_test_append(function, &next, IR_STORE, IR_VOID, object, 0, BUSTER_IR_NONE);
        }
        uint32_t returned = BUSTER_IR_NONE;
        if (calls)
        {
            const uint32_t callee = tier1_select_test_append(function, &next, IR_INTTOPTR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function->extra[0] = 3;
            function->call_types[0] = IR_I64;
            function->calls[0] = (IrCall){ .argument_first = 0, .argument_count = 1, .type_first = 0, .parameter_count = 1, .result_type = IR_I64 };
            returned = tier1_select_test_append(function, &next, IR_CALL_INDIRECT, IR_I64, callee, 0, BUSTER_IR_NONE);
        }
        uint32_t h = BUSTER_IR_NONE;
        for (uint32_t step = 0; step < BUSTER_TIER1_STACK_PARAMETERS; step += 1)
        {
            const uint32_t k = BUSTER_TIER1_STACK_PARAMETERS - 1 - step;
            const uint8_t type = tier1_stack_parameter_types[k];
            uint32_t extended = k == 0 && realigned ? tier1_select_test_append(function, &next, IR_LOAD, IR_I64, object, BUSTER_IR_NONE, BUSTER_IR_NONE) : k == 0 && calls ? returned : k;
            if (type == IR_PTR)
            {
                extended = tier1_select_test_append(function, &next, IR_PTRTOINT, IR_I64, k, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            else if (type != IR_I64)
            {
                extended = tier1_select_test_append(function, &next, tier1_stack_parameter_signed[k] ? IR_SEXT : IR_ZEXT, IR_I64, k, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            if (h == BUSTER_IR_NONE)
            {
                h = extended;
            }
            else
            {
                const uint32_t product = tier1_select_test_append(function, &next, IR_MUL, IR_I64, h, constant, BUSTER_IR_NONE);
                h = tier1_select_test_append(function, &next, IR_ADD, IR_I64, product, extended, BUSTER_IR_NONE);
            }
        }
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, h, BUSTER_IR_NONE, BUSTER_IR_NONE);
        assert(next == sizes.instruction_count);
    }
    return created;
}

// What tier1_stack_parameters_function returns for the 64-bit values in its parameters' registers and slots, with `calls`
// e0 its callee's result of p3.
static uint64_t tier1_stack_parameters_oracle(const uint64_t* parameters, bool calls)
{
    uint64_t extended[BUSTER_TIER1_STACK_PARAMETERS];
    for (uint32_t k = 0; k < BUSTER_TIER1_STACK_PARAMETERS; k += 1)
    {
        const uint64_t value = parameters[k];
        const uint8_t type = tier1_stack_parameter_types[k];
        const bool sign = tier1_stack_parameter_signed[k] != 0;
        extended[k] = type == IR_I32 ? (sign ? (uint64_t)(int64_t)(int32_t)(uint32_t)value : (uint32_t)value) : type == IR_I16 ? (sign ? (uint64_t)(int64_t)(int16_t)(uint16_t)value : (uint16_t)value)
                      : type == IR_I8 ? (sign ? (uint64_t)(int64_t)(int8_t)(uint8_t)value : (uint8_t)value) : value;
    }
    extended[0] = calls ? tier1_stack_callee_one(parameters[3]) : extended[0];
    uint64_t h = extended[BUSTER_TIER1_STACK_PARAMETERS - 1];
    for (uint32_t step = 1; step < BUSTER_TIER1_STACK_PARAMETERS; step += 1)
    {
        h = h * 31 + extended[BUSTER_TIER1_STACK_PARAMETERS - 1 - step];
    }
    return h;
}

// g(f16, f7, fv, x, y), from v0 = x, v1 = y and vk = v(k-1) + v(k-2) for odd k, v(k-1) ^ v(k-2) for even k, all live at
// the first call:
//   c16 = f16(v0, ..., v15), with v4, v7 and v12 truncated to I32, v10 a pointer and v9 the constant;
//   c7 = f7(y, x, v2, v3, c16, v5, x);
//   cv = fv(8, v0, ..., v7), variadic: the count its one fixed parameter, whose type alone call_types has;
// returning (c16 * 3 + c7) ^ cv ^ x, x live across the three calls.
static bool tier1_stack_calls_function(Arena* input, IrFunction* function)
{
    const IrSizes sizes = {
        .instruction_count = 5 + 14 + 3 + 1 + 3 + 3 + 4 + 1,
        .block_count = 1,
        .extra_count = BUSTER_TIER1_STACK_ARGUMENTS + 7 + 9,
        .constant_count = 3,
        .call_count = 3,
        .call_type_count = BUSTER_TIER1_STACK_ARGUMENTS + 7 + 1,
        .parameter_count = 5,
    };
    const bool created = ir_create(input, &sizes, function) == IR_OK;
    if (created)
    {
        function->block_offsets[1] = (uint32_t)sizes.instruction_count;
        function->result_type = IR_I64;
        function->incoming = (AbiCall){ .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .integer_registers = 5 };
        function->constants[0].low = BUSTER_TIER1_STACK_CONSTANT;
        function->constants[1].low = 8;
        function->constants[2].low = 3;
        uint32_t next = 0;
        for (uint32_t k = 0; k < 5; k += 1)
        {
            const uint8_t type = k < 3 ? IR_PTR : IR_I64;
            function->parameter_types[k] = type;
            tier1_select_test_append(function, &next, IR_PARAM, type, k, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        uint32_t v[BUSTER_TIER1_STACK_ARGUMENTS];
        v[0] = 3;
        v[1] = 4;
        for (uint32_t k = 2; k < BUSTER_TIER1_STACK_ARGUMENTS; k += 1)
        {
            v[k] = tier1_select_test_append(function, &next, (k & 1) != 0 ? IR_ADD : IR_XOR, IR_I64, v[k - 1], v[k - 2], BUSTER_IR_NONE);
        }
        const uint32_t constant = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t k = 0; k < BUSTER_TIER1_STACK_ARGUMENTS; k += 1)
        {
            const uint8_t type = tier1_stack_argument_type(k);
            uint32_t argument = k == 9 ? constant : v[k];
            if (type == IR_I32)
            {
                argument = tier1_select_test_append(function, &next, IR_TRUNC, IR_I32, v[k], BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            else if (type == IR_PTR)
            {
                argument = tier1_select_test_append(function, &next, IR_INTTOPTR, IR_PTR, v[k], BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            function->extra[k] = argument;
            function->call_types[k] = type;
        }
        function->calls[0] = (IrCall){ .argument_first = 0, .argument_count = BUSTER_TIER1_STACK_ARGUMENTS, .type_first = 0, .parameter_count = BUSTER_TIER1_STACK_ARGUMENTS, .result_type = IR_I64 };
        const uint32_t c16 = tier1_select_test_append(function, &next, IR_CALL_INDIRECT, IR_I64, 0, 0, BUSTER_IR_NONE);
        const uint32_t seven[7] = { 4, 3, v[2], v[3], c16, v[5], 3 };
        for (uint32_t k = 0; k < 7; k += 1)
        {
            function->extra[BUSTER_TIER1_STACK_ARGUMENTS + k] = seven[k];
            function->call_types[BUSTER_TIER1_STACK_ARGUMENTS + k] = IR_I64;
        }
        function->calls[1] = (IrCall){ .argument_first = BUSTER_TIER1_STACK_ARGUMENTS, .argument_count = 7, .type_first = BUSTER_TIER1_STACK_ARGUMENTS, .parameter_count = 7, .result_type = IR_I64 };
        const uint32_t c7 = tier1_select_test_append(function, &next, IR_CALL_INDIRECT, IR_I64, 1, 1, BUSTER_IR_NONE);
        const uint32_t count = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t variadic_first = BUSTER_TIER1_STACK_ARGUMENTS + 7;
        function->extra[variadic_first] = count;
        for (uint32_t k = 0; k < 8; k += 1)
        {
            function->extra[variadic_first + 1 + k] = v[k];
        }
        function->call_types[variadic_first] = IR_I64;
        function->calls[2] = (IrCall){ .argument_first = variadic_first, .argument_count = 9, .type_first = variadic_first, .parameter_count = 1, .result_type = IR_I64, .variadic = true };
        const uint32_t cv = tier1_select_test_append(function, &next, IR_CALL_INDIRECT, IR_I64, 2, 2, BUSTER_IR_NONE);
        const uint32_t three = tier1_select_test_append(function, &next, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t product = tier1_select_test_append(function, &next, IR_MUL, IR_I64, c16, three, BUSTER_IR_NONE);
        const uint32_t sum = tier1_select_test_append(function, &next, IR_ADD, IR_I64, product, c7, BUSTER_IR_NONE);
        const uint32_t mixed = tier1_select_test_append(function, &next, IR_XOR, IR_I64, sum, cv, BUSTER_IR_NONE);
        const uint32_t result = tier1_select_test_append(function, &next, IR_XOR, IR_I64, mixed, 3, BUSTER_IR_NONE);
        tier1_select_test_append(function, &next, IR_RET, IR_VOID, result, BUSTER_IR_NONE, BUSTER_IR_NONE);
        assert(next == sizes.instruction_count);
    }
    return created;
}

// What tier1_stack_calls_function returns, from C's own calls of the same callees.
static uint64_t tier1_stack_calls_oracle(uint64_t x, uint64_t y)
{
    uint64_t v[BUSTER_TIER1_STACK_ARGUMENTS];
    v[0] = x;
    v[1] = y;
    for (uint32_t k = 2; k < BUSTER_TIER1_STACK_ARGUMENTS; k += 1)
    {
        v[k] = (k & 1) != 0 ? v[k - 1] + v[k - 2] : v[k - 1] ^ v[k - 2];
    }
    const uint64_t c16 = tier1_stack_callee16(v[0], v[1], v[2], v[3], (uint32_t)v[4], v[5], v[6], (uint32_t)v[7], v[8], BUSTER_TIER1_STACK_CONSTANT, (const void*)(uintptr_t)v[10], v[11], (uint32_t)v[12], v[13], v[14], v[15]);
    const uint64_t c7 = tier1_stack_callee7(y, x, v[2], v[3], c16, v[5], x);
    const uint64_t cv = tier1_stack_callee_variadic(8, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    return ((c16 * 3 + c7) ^ cv) ^ x;
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_stack_call_parameters(const void* code, const uint64_t* p)
{
    uint64_t (*function)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    memcpy(&function, &code, sizeof(function));
    return function(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]);
}

static __attribute__((no_sanitize("function"))) uint64_t tier1_stack_call_calls(const void* code, uint64_t x, uint64_t y)
{
    uint64_t (*function)(const void*, const void*, const void*, uint64_t, uint64_t);
    memcpy(&function, &code, sizeof(function));
    const void* callee16 = (const void*)tier1_stack_callee16;
    const void* callee7 = (const void*)tier1_stack_callee7;
    const void* variadic = (const void*)tier1_stack_callee_variadic;
    return function(callee16, callee7, variadic, x, y);
}

// One compile of fixture 0 (the parameters), 1 (the parameters in a realigned frame), 2 (the calls) or 3 (the
// parameters with a call), by one path and one set of options, run on the samples.
static void tier1_stack_compile(Tier1EmitTest* test, uint32_t fixture, bool vector, bool rematerialize, uint32_t register_mask, bool keep_pointer)
{
    IrFunction function;
    const bool parameters = fixture != 2;
    const bool created = parameters ? tier1_stack_parameters_function(&test->input, fixture == 1, fixture == 3, &function) : tier1_stack_calls_function(&test->input, &function);
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const bool planned = created && tier1_select_plan(&function, 0, &test->output, &test->scratch, vector, &plan, &node) == TIER1_SELECT_OK;
    tier1_emit_test_expect(test, planned, "the stack arguments' fixture selects");
    const Tier1EmitOptions options = {
        .register_mask = register_mask,
        .vector = vector,
        .rematerialize = rematerialize,
        .frame_flags = keep_pointer ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
        .symbol_count = function.symbol_count,
    };
    Tier1EmitResult emitted;
    const uint64_t scratch_mark = test->scratch.length;
    const Tier1EmitError error = planned ? tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
    tier1_emit_test_expect(test, error == TIER1_EMIT_OK && test->scratch.length == scratch_mark, "the stack arguments' fixture emits through the CFG consumer");
    if (error != TIER1_EMIT_OK && planned && test->failures < 20)
    {
        fprintf(stderr, "  stack emission error=%u node=%u fixture=%u vector=%u remat=%u mask=%x pointer=%u\n", error, node, fixture, vector, rematerialize, register_mask, keep_pointer);
    }
    if (error == TIER1_EMIT_OK)
    {
        tier1_emit_test_expect(test, (fixture == 1 || keep_pointer) == emitted.frame.frame_pointer, "a realigned frame or a kept frame pointer gives the incoming area its base");
        tier1_emit_test_expect(test, fixture != 3 || emitted.frame.saved_registers != 0, "the parameters live across fixture 3's call are in callee-saved registers, which its prologue pushes");
        void* executable = MAP_FAILED;
        uint64_t bytes = 0;
        tier1_stack_misaligned = 0;
        if (tier1_emit_test_load(test, &emitted.code, &executable, &bytes))
        {
            uint64_t random = UINT64_C(0x3a1f72c9e05b6d84) ^ ((uint64_t)register_mask << 8) ^ (vector ? 1 : 0) ^ (rematerialize ? 2 : 0) ^ (keep_pointer ? 4 : 0) ^ ((uint64_t)fixture << 40);
            for (uint32_t sample = 0; sample < BUSTER_TIER1_STACK_SAMPLES; sample += 1)
            {
                uint64_t expected = 0;
                uint64_t actual = 0;
                if (parameters)
                {
                    uint64_t values[BUSTER_TIER1_STACK_PARAMETERS];
                    for (uint32_t k = 0; k < BUSTER_TIER1_STACK_PARAMETERS; k += 1)
                    {
                        values[k] = sample == 0 ? UINT64_MAX - k : sample == 1 ? k + 1 : tier1_emit_test_random(&random);
                    }
                    values[0] = fixture == 3 ? (uint64_t)(uintptr_t)tier1_stack_callee_one : values[0];
                    expected = tier1_stack_parameters_oracle(values, fixture == 3);
                    actual = tier1_stack_call_parameters(executable, values);
                }
                else
                {
                    const uint64_t x = sample == 0 ? UINT64_MAX : tier1_emit_test_random(&random);
                    const uint64_t y = sample == 0 ? 1 : tier1_emit_test_random(&random);
                    expected = tier1_stack_calls_oracle(x, y);
                    actual = tier1_stack_call_calls(executable, x, y);
                }
                if (actual != expected && test->failures < 20)
                {
                    fprintf(stderr, "  stack fixture=%u vector=%u remat=%u mask=%x pointer=%u actual=%016" PRIx64 " expected=%016" PRIx64 "\n", fixture, vector, rematerialize, register_mask, keep_pointer, actual, expected);
                }
                tier1_emit_test_expect(test, actual == expected, "arguments and parameters past the registers reach their stack slots natively");
            }
            tier1_emit_test_expect(test, tier1_stack_misaligned == 0, "every call keeps the stack 16 bytes aligned past its outgoing arguments");
        }
        if (executable != MAP_FAILED)
        {
            munmap(executable, bytes);
        }
    }
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->input, 0);
}

static void tier1_stack_cases(Tier1EmitTest* test)
{
    const uint32_t all = tier1_liveness_register_mask();
    // The fewest registers both fixtures can take: the six argument registers, which the parameters' definitions and the
    // calls' register arguments take at once, and rax. The other values go to their slots: every stack parameter but
    // one at the entry, and most of the calls' stack arguments.
    uint32_t few = 1u << X86_REGISTER_RAX;
    for (uint32_t k = 0; k < BUSTER_ABI_INTEGER_ARGUMENTS; k += 1)
    {
        few |= 1u << tier1_select_parameter_registers[k];
    }
    test->terms = 0;
    test->width = 8;
    test->selects = false;
    for (uint32_t fixture = 0; fixture < 4; fixture += 1)
    {
        for (uint32_t variant = 0; variant < 16; variant += 1)
        {
            tier1_stack_compile(test, fixture, (variant & 1) != 0, (variant & 2) != 0, (variant & 4) != 0 ? few : all, (variant & 8) != 0);
        }
    }
}

// The cases in a bounded child, as tier1_variadic_tests runs its own: a misplaced slot can fault or loop, which must
// fail this suite, not stop it. The counter page survives a normal exit.
static void tier1_stack_tests(Tier1EmitTest* test)
{
    uint64_t* report = mmap(0, 3 * sizeof(uint64_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    const pid_t child = report != MAP_FAILED ? fork() : -1;
    if (child == 0)
    {
        signal(SIGALRM, SIG_DFL);
        alarm(60);
        const uint64_t checks = test->checks;
        const uint64_t failures = test->failures;
        tier1_stack_cases(test);
        report[0] = test->checks - checks;
        report[1] = test->failures - failures;
        report[2] = UINT64_C(0x51c4e0b7a2d93f68);
        fflush(0);
        _exit(report[1] != 0);
    }
    int status = 0;
    pid_t waited = -1;
    if (child > 0)
    {
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
    }
    const bool finished = child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) <= 1 && report[2] == UINT64_C(0x51c4e0b7a2d93f68);
    tier1_emit_test_expect(test, finished, "bounded stack arguments' child completes without signal or timeout");
    if (finished)
    {
        test->checks += report[0];
        test->failures += report[1];
        tier1_emit_test_expect(test, WEXITSTATUS(status) == (report[1] != 0), "stack arguments' child status agrees with shared test counters");
    }
    else
    {
        fprintf(stderr, "tier1 stack arguments' child failed (status=%d)\n", status);
    }
    if (report != MAP_FAILED)
    {
        munmap(report, 3 * sizeof(uint64_t));
    }
}
