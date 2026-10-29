// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original fixtures for #788: calls with by-value aggregates' ABI records (#131) between interpreted functions and to
// natives clang compiled, and variadic definitions' anchors (#148). The interpreted callees' expected values come from
// the same arithmetic in C, the natives' from calling them directly, so that clang's code is the convention's oracle; the
// records are written by hand from the psABI's classification. Included by ir_interpreter_test.c, before its runner.

// A function built an instruction at a time into columns of room enough, whose counts are then the instructions,
// constants, addresses and calls' fixed parameter types made.
typedef struct IrInterpreterAbiBuilder IrInterpreterAbiBuilder;
struct IrInterpreterAbiBuilder
{
    IrFunction* function;
    uint32_t instructions;
    uint32_t constants;
    uint32_t addresses;
    uint32_t types;
};

#define BUSTER_IR_INTERPRETER_ABI_ROOM 256u

static bool ir_interpreter_abi_create(Arena* memory, IrFunction* function, IrSizes sizes, IrType result_type, IrInterpreterAbiBuilder* builder)
{
    sizes.instruction_count = BUSTER_IR_INTERPRETER_ABI_ROOM;
    sizes.block_count = 1;
    sizes.constant_count = BUSTER_IR_INTERPRETER_ABI_ROOM;
    sizes.address_count = BUSTER_IR_INTERPRETER_ABI_ROOM;
    const bool created = ir_create(memory, &sizes, function) == IR_OK;
    if (created)
    {
        function->result_type = result_type;
        function->symbol_count = 128;
    }
    *builder = (IrInterpreterAbiBuilder){ .function = function };
    return created;
}

static uint32_t ir_interpreter_abi_emit(IrInterpreterAbiBuilder* builder, IrOp op, IrType type, uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t id = builder->instructions++;
    ir_interpreter_test_instruction(builder->function, id, op, type, a, b, c);
    return id;
}

static uint32_t ir_interpreter_abi_constant(IrInterpreterAbiBuilder* builder, IrType type, uint64_t low, uint64_t high)
{
    const uint32_t constant = builder->constants++;
    builder->function->constants[constant] = (IrConstant){ .low = low, .high = high };
    return ir_interpreter_abi_emit(builder, IR_CONST, type, constant, BUSTER_IR_NONE, BUSTER_IR_NONE);
}

static uint32_t ir_interpreter_abi_offset(IrInterpreterAbiBuilder* builder, uint32_t base, int64_t offset)
{
    const uint32_t address = builder->addresses++;
    builder->function->addresses[address] = (IrAddress){ .offset = offset, .scale = 1 };
    return ir_interpreter_abi_emit(builder, IR_ADDRESS, IR_PTR, base, BUSTER_IR_NONE, address);
}

static void ir_interpreter_abi_store(IrInterpreterAbiBuilder* builder, uint32_t base, int64_t offset, uint32_t value)
{
    const uint32_t address = offset == 0 ? base : ir_interpreter_abi_offset(builder, base, offset);
    ir_interpreter_abi_emit(builder, IR_STORE, IR_VOID, address, value, BUSTER_IR_NONE);
}

static uint32_t ir_interpreter_abi_load(IrInterpreterAbiBuilder* builder, IrType type, uint32_t base, int64_t offset)
{
    const uint32_t address = offset == 0 ? base : ir_interpreter_abi_offset(builder, base, offset);
    return ir_interpreter_abi_emit(builder, IR_LOAD, type, address, BUSTER_IR_NONE, BUSTER_IR_NONE);
}

// A call of `symbol` with `count` arguments, the first `named` of them its fixed parameters, as descriptor `descriptor`
// whose argument and type slices start at `first`; `abi` 1 + its records' index or 0, and `into` its aggregate result's
// destination or BUSTER_IR_NONE.
static uint32_t ir_interpreter_abi_call(IrInterpreterAbiBuilder* builder, uint32_t descriptor, uint32_t symbol, const uint32_t* arguments, uint32_t count, uint32_t named, uint32_t first, IrType result_type, uint32_t abi, uint32_t into)
{
    IrFunction* function = builder->function;
    for (uint32_t p = 0; p < count; p += 1)
    {
        function->extra[first + p] = arguments[p];
    }
    for (uint32_t p = 0; p < named; p += 1)
    {
        function->call_types[first + p] = function->type[arguments[p]];
    }
    builder->types = first + named > builder->types ? first + named : builder->types;
    function->calls[descriptor] = (IrCall){
        .argument_first = first,
        .argument_count = count,
        .type_first = first,
        .parameter_count = named,
        .result_type = (uint8_t)result_type,
        .variadic = named != count,
        .abi = abi,
    };
    return ir_interpreter_abi_emit(builder, IR_CALL, result_type, symbol, descriptor, into);
}

static void ir_interpreter_abi_finish(IrInterpreterAbiBuilder* builder)
{
    builder->function->instruction_count = builder->instructions;
    builder->function->constant_count = builder->constants;
    builder->function->address_count = builder->addresses;
    builder->function->call_type_count = builder->types;
    builder->function->block_offsets[1] = builder->instructions;
}

static AbiLocation ir_interpreter_abi_registers(uint8_t first, uint8_t second)
{
    const AbiLocation result = {
        .registers = {
            first,
            second,
        },
        .kind = BUSTER_ABI_LOCATION_REGISTERS,
    };
    return result;
}

static AbiLocation ir_interpreter_abi_stack(uint32_t offset)
{
    const AbiLocation result = {
        .stack_offset = offset,
        .registers = {
            BUSTER_ABI_REGISTER_NONE,
            BUSTER_ABI_REGISTER_NONE,
        },
        .kind = BUSTER_ABI_LOCATION_STACK,
    };
    return result;
}

static AbiReturn ir_interpreter_abi_return(uint8_t kind, uint8_t part_bytes, uint8_t first, uint8_t second)
{
    const AbiReturn result = {
        .kind = kind,
        .part_bytes = part_bytes,
        .registers = {
            first,
            second,
            BUSTER_ABI_REGISTER_NONE,
            BUSTER_ABI_REGISTER_NONE,
        },
    };
    return result;
}

#define BUSTER_IR_INTERPRETER_ABI_XMM(n) ((uint8_t)(BUSTER_ABI_REGISTER_VECTOR + (n)))
#define BUSTER_IR_INTERPRETER_ABI_NONE BUSTER_ABI_REGISTER_NONE

static IrSlot ir_interpreter_abi_slot(uint64_t size, uint8_t flags)
{
    const IrSlot result = {
        .size = size,
        .alignment = 16,
        .type = IR_VOID,
        .flags = (uint8_t)(BUSTER_IR_SLOT_ADDRESS_TAKEN | flags),
    };
    return result;
}

// The records fixture's caller from x: p keeps x and x + 1 (r wrote its copy of p), q is r's result, cc h's, and bb is
// h's argument as it was, folded in that order.
static uint64_t ir_interpreter_abi_expected(uint64_t x)
{
    const uint64_t loads[] = { x + 5, (x + 1) * 2, x + x, 1 + x, x };
    uint64_t result = x;
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(loads); k += 1)
    {
        result = result * 31 ^ loads[k];
    }
    return result;
}

// Calls with records between interpreted functions: struct P { long a, b; } r(struct P s, long x), returned in rax and
// rdx from its object's bytes, which writes its copy of s; struct B { long v[4]; } h(long x, struct B b), returned
// through a hidden pointer, its parameter 0; and a caller of both from x, whose own s keeps its bytes. Then each break of
// the records' agreement, a result room smaller than the result, and an entry with records of its own.
static void ir_interpreter_test_records(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
    const uint64_t mark = memory->length;
    IrFunction registers = { 0 };
    IrFunction hidden = { 0 };
    IrFunction caller = { 0 };
    IrInterpreterAbiBuilder r;
    IrInterpreterAbiBuilder h;
    IrInterpreterAbiBuilder f;
    const AbiReturn pair = ir_interpreter_abi_return(BUSTER_ABI_RESULT_REGISTERS, 8, BUSTER_ABI_RAX, BUSTER_ABI_RDX);
    const AbiReturn in_memory = ir_interpreter_abi_return(BUSTER_ABI_RESULT_MEMORY, 0, BUSTER_IR_INTERPRETER_ABI_NONE, BUSTER_IR_INTERPRETER_ABI_NONE);
    bool created = ir_interpreter_abi_create(memory, &registers, (IrSizes){ .slot_count = 1, .parameter_count = 2, .passed_count = 2 }, IR_PTR, &r);
    created = created && ir_interpreter_abi_create(memory, &hidden, (IrSizes){ .parameter_count = 3, .passed_count = 3 }, IR_I64, &h);
    created = created && ir_interpreter_abi_create(memory, &caller, (IrSizes){ .slot_count = 4, .parameter_count = 1, .extra_count = 4, .call_count = 2, .call_type_count = 4, .abi_call_count = 2, .passed_count = 4 }, IR_I64, &f);
    ir_interpreter_test_expect(tally, created, "records fixture allocation");
    if (created)
    {
        registers.parameter_types[0] = IR_PTR;
        registers.parameter_types[1] = IR_I64;
        registers.slots[0] = ir_interpreter_abi_slot(16, 0);
        registers.parameter_passed = 1;
        registers.passed[0] = (IrPassed){ .location = ir_interpreter_abi_registers(BUSTER_ABI_RDI, BUSTER_ABI_RSI), .size = 16 };
        registers.passed[1] = (IrPassed){ .location = ir_interpreter_abi_registers(BUSTER_ABI_RDX, BUSTER_IR_INTERPRETER_ABI_NONE) };
        registers.result_size = 16;
        registers.result_abi = pair;
        const uint32_t s = ir_interpreter_abi_emit(&r, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t x = ir_interpreter_abi_emit(&r, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t t = ir_interpreter_abi_emit(&r, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_store(&r, t, 0, ir_interpreter_abi_emit(&r, IR_ADD, IR_I64, ir_interpreter_abi_load(&r, IR_I64, s, 0), x, BUSTER_IR_NONE));
        ir_interpreter_abi_store(&r, t, 8, ir_interpreter_abi_emit(&r, IR_MUL, IR_I64, ir_interpreter_abi_load(&r, IR_I64, s, 8), ir_interpreter_abi_constant(&r, IR_I64, 2, 0), BUSTER_IR_NONE));
        ir_interpreter_abi_store(&r, s, 0, ir_interpreter_abi_constant(&r, IR_I64, 99, 0));
        ir_interpreter_abi_emit(&r, IR_RET, IR_VOID, t, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&r);

        hidden.parameter_types[0] = IR_I64;
        hidden.parameter_types[1] = IR_I64;
        hidden.parameter_types[2] = IR_PTR;
        hidden.parameter_passed = 1;
        hidden.passed[0] = (IrPassed){ .location = ir_interpreter_abi_registers(BUSTER_ABI_RDI, BUSTER_IR_INTERPRETER_ABI_NONE) };
        hidden.passed[1] = (IrPassed){ .location = ir_interpreter_abi_registers(BUSTER_ABI_RSI, BUSTER_IR_INTERPRETER_ABI_NONE) };
        hidden.passed[2] = (IrPassed){ .location = ir_interpreter_abi_stack(0), .size = 32 };
        hidden.result_abi = in_memory;
        const uint32_t d = ir_interpreter_abi_emit(&h, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t hx = ir_interpreter_abi_emit(&h, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t b = ir_interpreter_abi_emit(&h, IR_PARAM, IR_PTR, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t into = ir_interpreter_abi_emit(&h, IR_INTTOPTR, IR_PTR, d, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t k = 0; k < 4; k += 1)
        {
            ir_interpreter_abi_store(&h, into, 8 * k, ir_interpreter_abi_emit(&h, IR_ADD, IR_I64, ir_interpreter_abi_load(&h, IR_I64, b, 8 * k), hx, BUSTER_IR_NONE));
        }
        ir_interpreter_abi_emit(&h, IR_RET, IR_VOID, d, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&h);

        caller.parameter_types[0] = IR_I64;
        for (uint32_t slot = 0; slot < 4; slot += 1)
        {
            caller.slots[slot] = ir_interpreter_abi_slot(slot < 2 ? 16 : 32, 0);
        }
        caller.abi_calls[0] = (IrAbiCall){ .call = { .stack_alignment_log2 = 4, .integer_registers = 3, .result = pair }, .passed = 0, .result_size = 16 };
        caller.abi_calls[1] = (IrAbiCall){ .call = { .stack_bytes = 32, .stack_alignment_log2 = 4, .integer_registers = 2, .result = in_memory }, .passed = 2, .result_size = 32 };
        caller.passed[0] = registers.passed[0];
        caller.passed[1] = registers.passed[1];
        caller.passed[2] = hidden.passed[1];
        caller.passed[3] = hidden.passed[2];
        const uint32_t fx = ir_interpreter_abi_emit(&f, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        uint32_t slots[4];
        for (uint32_t slot = 0; slot < 4; slot += 1)
        {
            slots[slot] = ir_interpreter_abi_emit(&f, IR_STACK_ADDR, IR_PTR, slot, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const uint32_t one = ir_interpreter_abi_constant(&f, IR_I64, 1, 0);
        const uint32_t next = ir_interpreter_abi_emit(&f, IR_ADD, IR_I64, fx, one, BUSTER_IR_NONE);
        const uint32_t five = ir_interpreter_abi_constant(&f, IR_I64, 5, 0);
        ir_interpreter_abi_store(&f, slots[0], 0, fx);
        ir_interpreter_abi_store(&f, slots[0], 8, next);
        const uint32_t first_arguments[] = { slots[0], five };
        const uint32_t first_call = ir_interpreter_abi_call(&f, 0, 20, first_arguments, 2, 2, 0, IR_VOID, 1, slots[1]);
        const uint32_t elements[] = { fx, next, five, one };
        for (uint32_t k = 0; k < 4; k += 1)
        {
            ir_interpreter_abi_store(&f, slots[2], 8 * k, elements[k]);
        }
        const uint32_t second_arguments[] = { fx, slots[2] };
        ir_interpreter_abi_call(&f, 1, 21, second_arguments, 2, 2, 2, IR_VOID, 2, slots[3]);
        const uint32_t kept = ir_interpreter_abi_load(&f, IR_I64, slots[0], 0);
        const uint32_t loads[] = {
            ir_interpreter_abi_load(&f, IR_I64, slots[1], 0),
            ir_interpreter_abi_load(&f, IR_I64, slots[1], 8),
            ir_interpreter_abi_load(&f, IR_I64, slots[3], 0),
            ir_interpreter_abi_load(&f, IR_I64, slots[3], 24),
            ir_interpreter_abi_load(&f, IR_I64, slots[2], 0),
        };
        uint32_t folded = kept;
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(loads); k += 1)
        {
            folded = ir_interpreter_abi_emit(&f, IR_MUL, IR_I64, folded, ir_interpreter_abi_constant(&f, IR_I64, 31, 0), BUSTER_IR_NONE);
            folded = ir_interpreter_abi_emit(&f, IR_XOR, IR_I64, folded, loads[k], BUSTER_IR_NONE);
        }
        ir_interpreter_abi_emit(&f, IR_RET, IR_VOID, folded, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&f);

        const IrInterpreterFunction bindings[] = {
            { .symbol = 20, .function = &registers },
            { .symbol = 21, .function = &hidden },
            { .symbol = 22, .function = &caller },
        };
        const IrInterpreterProgram program = { .functions = bindings, .function_count = 3 };
        const uint64_t inputs[] = { 0, 7, UINT64_C(0x8000000000000001), UINT64_MAX };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(inputs); i += 1)
        {
            const InterpreterValue argument = { .low = inputs[i] };
            ir_interpreter_test_run(tally, &program, 22, &argument, 1, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = ir_interpreter_abi_expected(inputs[i]) },
                                    "aggregates by value copied for their callees, a result in registers and one through a hidden pointer");
        }
        const InterpreterValue argument = { .low = 3 };
        // The records must agree: a callee without records of its own, an aggregate of another size, a call without room
        // for its callee's result; and an entry can't take records.
        registers.parameter_passed = 0;
        registers.result_size = 0;
        ir_interpreter_test_run(tally, &program, 22, &argument, 1, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "a call with records to a callee without");
        registers.parameter_passed = 1;
        registers.result_size = 16;
        registers.passed[0].size = 8;
        ir_interpreter_test_run(tally, &program, 22, &argument, 1, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "an aggregate argument of another size than its parameter's");
        registers.passed[0].size = 16;
        hidden.result_abi = pair;
        ir_interpreter_test_run(tally, &program, 22, &argument, 1, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "a result through a hidden pointer to a callee that returns none");
        hidden.result_abi = in_memory;
        const InterpreterValue entry_arguments[] = { { .low = 0 }, { .low = 0 } };
        ir_interpreter_test_run(tally, &program, 20, entry_arguments, 2, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "an entry with records of its own");
        // Room for 8 of the 16 bytes r returns: q's second word stays undefined.
        caller.abi_calls[0].result_size = 8;
        ir_interpreter_test_run(tally, &program, 22, &argument, 1, 0, execution, INTERPRETER_UNDEFINED, (InterpreterValue){ 0 }, "a result in registers copies at most its call's room");
        caller.abi_calls[0].result_size = 16;
        // Without room to copy an argument, and with the destination undefined.
        caller.c[first_call] = BUSTER_IR_NONE;
        ir_interpreter_test_expect(tally, ir_verify(&caller, execution, 0).error != IR_OK, "a result in registers needs its destination");
        caller.c[first_call] = slots[1];
        ir_interpreter_test_run(tally, &program, 22, &argument, 1, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = ir_interpreter_abi_expected(3) }, "the fixture runs again after its refusals");
    }
    // long e(struct E e, long x), struct E an empty record, whose 1-byte object the convention IGNOREs: a call without
    // records passes whatever value it has for it, here an integer that is no address, and e gets a fresh object.
    IrFunction empty = { 0 };
    IrFunction empty_caller = { 0 };
    IrInterpreterAbiBuilder e;
    IrInterpreterAbiBuilder g;
    created = ir_interpreter_abi_create(memory, &empty, (IrSizes){ .parameter_count = 2, .passed_count = 2 }, IR_I64, &e);
    created = created && ir_interpreter_abi_create(memory, &empty_caller, (IrSizes){ .parameter_count = 1, .extra_count = 2, .call_count = 1, .call_type_count = 2 }, IR_I64, &g);
    ir_interpreter_test_expect(tally, created, "empty record fixture allocation");
    if (created)
    {
        empty.parameter_types[0] = IR_PTR;
        empty.parameter_types[1] = IR_I64;
        empty.parameter_passed = 1;
        empty.passed[0] = (IrPassed){ .location = { .registers = { BUSTER_IR_INTERPRETER_ABI_NONE, BUSTER_IR_INTERPRETER_ABI_NONE }, .kind = BUSTER_ABI_LOCATION_IGNORE }, .size = 1 };
        empty.passed[1] = (IrPassed){ .location = ir_interpreter_abi_registers(BUSTER_ABI_RDI, BUSTER_IR_INTERPRETER_ABI_NONE) };
        ir_interpreter_abi_emit(&e, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t ex = ir_interpreter_abi_emit(&e, IR_PARAM, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_emit(&e, IR_RET, IR_VOID, ir_interpreter_abi_emit(&e, IR_ADD, IR_I64, ex, ir_interpreter_abi_constant(&e, IR_I64, 1, 0), BUSTER_IR_NONE), BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&e);
        empty_caller.parameter_types[0] = IR_I64;
        const uint32_t gx = ir_interpreter_abi_emit(&g, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t empty_arguments[] = { ir_interpreter_abi_constant(&g, IR_I64, 12345, 0), gx };
        ir_interpreter_abi_emit(&g, IR_RET, IR_VOID, ir_interpreter_abi_call(&g, 0, 23, empty_arguments, 2, 2, 0, IR_I64, 0, BUSTER_IR_NONE), BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&g);
        const IrInterpreterFunction bindings[] = {
            { .symbol = 23, .function = &empty },
            { .symbol = 24, .function = &empty_caller },
        };
        const IrInterpreterProgram program = { .functions = bindings, .function_count = 2 };
        const InterpreterValue argument = { .low = 41 };
        ir_interpreter_test_run(tally, &program, 24, &argument, 1, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = 42 }, "an IGNOREd empty record's callee gets an object of its own, whatever its call passes");
    }
    arena_pop_to(memory, mark);
}

// The unnamed arguments of the variadic fixture's call, after out: six longs, nine doubles, a long double, an __int128
// and an int, which LLVM's assignment puts in rsi to r9, the stack's 0, xmm0 to xmm7, its 8, 16, 32 and 48.
#define BUSTER_IR_INTERPRETER_ABI_UNNAMED 18u
static const uint64_t ir_interpreter_abi_longs[6] = {
    UINT64_C(0x1111111111111111),
    UINT64_C(0x2222222222222222),
    UINT64_C(0x3333333333333333),
    UINT64_C(0x4444444444444444),
    UINT64_C(0x5555555555555555),
    UINT64_C(0x6666666666666666),
};

static uint64_t ir_interpreter_abi_double(uint32_t k)
{
    const double value = 1.5 + k * 0.25;
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// What v's out holds after the variadic fixture's call: each unnamed argument read back where its register save area
// slot or overflow area slot is.
static void ir_interpreter_abi_variadic_expected(uint8_t* out, long double extended, unsigned __int128 wide, uint32_t narrow)
{
    memset(out, 0, 160);
    for (uint32_t k = 0; k < 5; k += 1)
    {
        memcpy(out + 8 * k, &ir_interpreter_abi_longs[k], 8);
    }
    for (uint32_t k = 0; k < 8; k += 1)
    {
        const uint64_t bits = ir_interpreter_abi_double(k);
        memcpy(out + 40 + 8 * k, &bits, 8);
    }
    const uint64_t last = ir_interpreter_abi_double(8);
    memcpy(out + 104, &ir_interpreter_abi_longs[5], 8);
    memcpy(out + 112, &last, 8);
    memcpy(out + 120, &extended, 10);
    memcpy(out + 136, &wide, 16);
    memcpy(out + 152, &narrow, 4);
}

// A variadic definition, void v(void* out, ...), its anchors read where LLVM's assignment puts each unnamed scalar (no
// records: rsi to r9, xmm0 to xmm7, then the overflow area's 8-byte slots, a long double's and an __int128's 16 bytes
// aligned to 16), into out, a binding; then void w(void* out, ...) called with records, struct M { double d; int i; } in
// xmm0 and rsi and struct L { long v[3]; } on the stack, read from the save area's slots and the overflow area. And the
// refusals: named registers that aren't the incoming frame's, an unpromoted unnamed char, a variadic entry, and an anchor
// slot no argument wrote, undefined.
static void ir_interpreter_test_variadic(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
    const uint64_t mark = memory->length;
    IrFunction callee = { 0 };
    IrFunction caller = { 0 };
    IrFunction aggregates = { 0 };
    IrFunction aggregate_caller = { 0 };
    IrInterpreterAbiBuilder v;
    IrInterpreterAbiBuilder c;
    IrInterpreterAbiBuilder w;
    IrInterpreterAbiBuilder d;
    const IrSizes anchored = { .slot_count = 2, .parameter_count = 1 };
    bool created = ir_interpreter_abi_create(memory, &callee, anchored, IR_VOID, &v);
    created = created && ir_interpreter_abi_create(memory, &caller, (IrSizes){ .extra_count = 1 + BUSTER_IR_INTERPRETER_ABI_UNNAMED, .call_count = 1, .call_type_count = 1 + BUSTER_IR_INTERPRETER_ABI_UNNAMED }, IR_VOID, &c);
    created = created && ir_interpreter_abi_create(memory, &aggregates, anchored, IR_VOID, &w);
    created = created && ir_interpreter_abi_create(memory, &aggregate_caller, (IrSizes){ .slot_count = 2, .extra_count = 3, .call_count = 1, .call_type_count = 3, .abi_call_count = 1, .passed_count = 3 }, IR_VOID, &d);
    ir_interpreter_test_expect(tally, created, "variadic fixture allocation");
    if (created)
    {
        const AbiCall incoming = {
            .stack_alignment_log2 = 4,
            .integer_registers = 1,
        };
        IrFunction* definitions[] = { &callee, &aggregates };
        for (uint32_t k = 0; k < 2; k += 1)
        {
            definitions[k]->parameter_types[0] = IR_PTR;
            definitions[k]->variadic = true;
            definitions[k]->incoming = incoming;
            definitions[k]->slots[0] = ir_interpreter_abi_slot(BUSTER_ABI_SAVE_AREA_BYTES, BUSTER_IR_SLOT_REGISTER_SAVE_AREA);
            definitions[k]->slots[1] = ir_interpreter_abi_slot(0, BUSTER_IR_SLOT_OVERFLOW_AREA);
        }
        const uint32_t out = ir_interpreter_abi_emit(&v, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t save = ir_interpreter_abi_emit(&v, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t overflow = ir_interpreter_abi_emit(&v, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t k = 0; k < 5; k += 1)
        {
            ir_interpreter_abi_store(&v, out, 8 * k, ir_interpreter_abi_load(&v, IR_I64, save, 8 * (k + 1)));
        }
        for (uint32_t k = 0; k < 8; k += 1)
        {
            ir_interpreter_abi_store(&v, out, 40 + 8 * k, ir_interpreter_abi_load(&v, IR_F64, save, BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET + BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES * k));
        }
        const IrType overflow_types[] = { IR_I64, IR_F64, IR_F80, IR_I128, IR_I32 };
        const uint32_t overflow_offsets[] = { 0, 8, 16, 32, 48 };
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(overflow_types); k += 1)
        {
            ir_interpreter_abi_store(&v, out, 104 + (k < 3 ? 8 * k : 16 * k - 16), ir_interpreter_abi_load(&v, overflow_types[k], overflow, overflow_offsets[k]));
        }
        ir_interpreter_abi_emit(&v, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&v);

        const long double extended = -2.75L;
        const unsigned __int128 wide = (unsigned __int128)UINT64_C(0x0123456789abcdef) << 64 | UINT64_C(0xfedcba9876543210);
        const uint32_t narrow = 0x7abcdef1u;
        uint64_t extended_bits[2] = { 0 };
        memcpy(extended_bits, &extended, 10);
        uint32_t arguments[1 + BUSTER_IR_INTERPRETER_ABI_UNNAMED];
        arguments[0] = ir_interpreter_abi_emit(&c, IR_SYMBOL_ADDR, IR_PTR, 40, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t k = 0; k < 6; k += 1)
        {
            arguments[1 + k] = ir_interpreter_abi_constant(&c, IR_I64, ir_interpreter_abi_longs[k], 0);
        }
        for (uint32_t k = 0; k < 9; k += 1)
        {
            arguments[7 + k] = ir_interpreter_abi_constant(&c, IR_F64, ir_interpreter_abi_double(k), 0);
        }
        arguments[16] = ir_interpreter_abi_constant(&c, IR_F80, extended_bits[0], extended_bits[1]);
        arguments[17] = ir_interpreter_abi_constant(&c, IR_I128, (uint64_t)wide, (uint64_t)(wide >> 64));
        arguments[18] = ir_interpreter_abi_constant(&c, IR_I32, narrow, 0);
        ir_interpreter_abi_call(&c, 0, 30, arguments, BUSTER_ARRAY_LENGTH(arguments), 1, 0, IR_VOID, 0, BUSTER_IR_NONE);
        ir_interpreter_abi_emit(&c, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&c);

        uint8_t bytes[160] = { 0 };
        uint8_t expected[160];
        ir_interpreter_abi_variadic_expected(expected, extended, wide, narrow);
        const InterpreterBinding binding = { .symbol = 40, .bytes = bytes, .size = sizeof(bytes), .writable = true };
        const IrInterpreterFunction functions[] = {
            { .symbol = 30, .function = &callee },
            { .symbol = 31, .function = &caller },
            { .symbol = 32, .function = &aggregates },
            { .symbol = 33, .function = &aggregate_caller },
        };
        const IrInterpreterProgram program = { .functions = functions, .function_count = 4, .bindings = &binding, .binding_count = 1 };
        ir_interpreter_test_run(tally, &program, 31, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ 0 }, "a variadic definition's anchors hold its call's arguments");
        ir_interpreter_test_expect(tally, memcmp(bytes, expected, sizeof(bytes)) == 0, "each unnamed scalar where LLVM's assignment puts it");
        callee.incoming.integer_registers = 2;
        ir_interpreter_test_run(tally, &program, 31, 0, 0, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "named registers that aren't the incoming frame's");
        callee.incoming.integer_registers = 1;
        const IrConstant saved = caller.constants[caller.a[arguments[18]]];
        caller.type[arguments[18]] = IR_I8;
        caller.constants[caller.a[arguments[18]]] = (IrConstant){ .low = 1 };
        ir_interpreter_test_run(tally, &program, 31, 0, 0, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "an unnamed char, which the default argument promotions make an int");
        caller.type[arguments[18]] = IR_I32;
        caller.constants[caller.a[arguments[18]]] = saved;
        const InterpreterValue pointer = { .low = (uintptr_t)bytes };
        ir_interpreter_test_run(tally, &program, 30, &pointer, 1, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "a variadic entry, without a call to lay out its anchors");
        caller.calls[0].argument_count = 6;
        ir_interpreter_test_run(tally, &program, 31, 0, 0, 0, execution, INTERPRETER_UNDEFINED, (InterpreterValue){ 0 }, "a save area slot no argument wrote is undefined");
        caller.calls[0].argument_count = BUSTER_ARRAY_LENGTH(arguments);

        // w reads struct M from xmm0's and rsi's slots and struct L from the overflow area.
        const uint32_t w_out = ir_interpreter_abi_emit(&w, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t w_save = ir_interpreter_abi_emit(&w, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t w_overflow = ir_interpreter_abi_emit(&w, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_store(&w, w_out, 0, ir_interpreter_abi_load(&w, IR_F64, w_save, BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET));
        ir_interpreter_abi_store(&w, w_out, 8, ir_interpreter_abi_load(&w, IR_I32, w_save, 8));
        for (uint32_t k = 0; k < 3; k += 1)
        {
            ir_interpreter_abi_store(&w, w_out, 16 + 8 * k, ir_interpreter_abi_load(&w, IR_I64, w_overflow, 8 * k));
        }
        ir_interpreter_abi_emit(&w, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&w);
        aggregate_caller.slots[0] = ir_interpreter_abi_slot(16, 0);
        aggregate_caller.slots[1] = ir_interpreter_abi_slot(24, 0);
        aggregate_caller.abi_calls[0] = (IrAbiCall){ .call = { .stack_bytes = 24, .stack_alignment_log2 = 4, .integer_registers = 2, .vector_registers = 1 }, .passed = 0 };
        aggregate_caller.passed[0] = (IrPassed){ .location = ir_interpreter_abi_registers(BUSTER_ABI_RDI, BUSTER_IR_INTERPRETER_ABI_NONE) };
        aggregate_caller.passed[1] = (IrPassed){ .location = ir_interpreter_abi_registers(BUSTER_IR_INTERPRETER_ABI_XMM(0), BUSTER_ABI_RSI), .size = 16 };
        aggregate_caller.passed[2] = (IrPassed){ .location = ir_interpreter_abi_stack(0), .size = 24 };
        const uint32_t d_out = ir_interpreter_abi_emit(&d, IR_SYMBOL_ADDR, IR_PTR, 40, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t m = ir_interpreter_abi_emit(&d, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t l = ir_interpreter_abi_emit(&d, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_store(&d, m, 0, ir_interpreter_abi_constant(&d, IR_F64, ir_interpreter_abi_double(3), 0));
        ir_interpreter_abi_store(&d, m, 8, ir_interpreter_abi_constant(&d, IR_I32, 0x5a5a1234u, 0));
        for (uint32_t k = 0; k < 3; k += 1)
        {
            ir_interpreter_abi_store(&d, l, 8 * k, ir_interpreter_abi_constant(&d, IR_I64, ir_interpreter_abi_longs[k + 3], 0));
        }
        const uint32_t aggregate_arguments[] = { d_out, m, l };
        ir_interpreter_abi_call(&d, 0, 32, aggregate_arguments, 3, 1, 0, IR_VOID, 1, BUSTER_IR_NONE);
        ir_interpreter_abi_emit(&d, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&d);
        memset(bytes, 0, sizeof(bytes));
        memset(expected, 0, sizeof(expected));
        const uint64_t m_double = ir_interpreter_abi_double(3);
        const uint32_t m_integer = 0x5a5a1234u;
        memcpy(expected, &m_double, 8);
        memcpy(expected + 8, &m_integer, 4);
        memcpy(expected + 16, &ir_interpreter_abi_longs[3], 24);
        ir_interpreter_test_run(tally, &program, 33, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ 0 }, "a variadic call's records place its aggregates in the anchors");
        ir_interpreter_test_expect(tally, memcmp(bytes, expected, sizeof(bytes)) == 0, "an unnamed struct's eightbytes in xmm0's and rsi's slots, another's bytes in the overflow area");
        aggregate_caller.passed[2].location.stack_offset = 4096;
        ir_interpreter_test_run(tally, &program, 33, 0, 0, 0, execution, INTERPRETER_MEMORY, (InterpreterValue){ 0 }, "an unnamed aggregate placed past its call's argument area");
        aggregate_caller.passed[2].location.stack_offset = 0;
    }
    arena_pop_to(memory, mark);
}

// Arguments aligned past 16 (#833's review): void u(void* out, ...) reads an unnamed struct A { uint64_t w[4]; } aligned
// to 32 as va_arg does, at its overflow area's address rounded up to 32, and long n(struct O s), O's 128 bytes aligned to
// 128 on the stack, returns its parameter's address modulo 128; each called with records whose argument area is aligned
// so (2^5 and 2^7), from frames at each 16 bytes of 128 in the arena, where an area aligned only to 16 or a copy only to
// 64 would read A from past its start or give O's address a remainder. And n again with the call's records aligned to
// 16 but its incoming frame to 128, and a call aligned past BUSTER_IR_INTERPRETER_MAX_STACK_ALIGNMENT_LOG2, refused.
static void ir_interpreter_test_aligned(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
    const uint64_t mark = memory->length;
    IrFunction reader = { 0 };
    IrFunction caller = { 0 };
    IrFunction remainder = { 0 };
    IrFunction remainder_caller = { 0 };
    IrInterpreterAbiBuilder u;
    IrInterpreterAbiBuilder c;
    IrInterpreterAbiBuilder n;
    IrInterpreterAbiBuilder m;
    bool created = ir_interpreter_abi_create(memory, &reader, (IrSizes){ .slot_count = 2, .parameter_count = 1 }, IR_VOID, &u);
    created = created && ir_interpreter_abi_create(memory, &caller, (IrSizes){ .slot_count = 1, .extra_count = 2, .call_count = 1, .call_type_count = 1, .abi_call_count = 1, .passed_count = 2 }, IR_VOID, &c);
    created = created && ir_interpreter_abi_create(memory, &remainder, (IrSizes){ .parameter_count = 1, .passed_count = 1 }, IR_I64, &n);
    created = created && ir_interpreter_abi_create(memory, &remainder_caller, (IrSizes){ .slot_count = 1, .extra_count = 1, .call_count = 1, .call_type_count = 1, .abi_call_count = 1, .passed_count = 1 }, IR_I64, &m);
    ir_interpreter_test_expect(tally, created, "aligned arguments fixture allocation");
    if (created)
    {
        reader.parameter_types[0] = IR_PTR;
        reader.variadic = true;
        reader.incoming = (AbiCall){ .stack_alignment_log2 = 4, .integer_registers = 1 };
        reader.slots[0] = ir_interpreter_abi_slot(BUSTER_ABI_SAVE_AREA_BYTES, BUSTER_IR_SLOT_REGISTER_SAVE_AREA);
        reader.slots[1] = ir_interpreter_abi_slot(0, BUSTER_IR_SLOT_OVERFLOW_AREA);
        const uint32_t out = ir_interpreter_abi_emit(&u, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t overflow = ir_interpreter_abi_emit(&u, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t start = ir_interpreter_abi_emit(&u, IR_PTRTOINT, IR_I64, overflow, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t up = ir_interpreter_abi_emit(&u, IR_ADD, IR_I64, start, ir_interpreter_abi_constant(&u, IR_I64, 31, 0), BUSTER_IR_NONE);
        const uint32_t rounded = ir_interpreter_abi_emit(&u, IR_AND, IR_I64, up, ir_interpreter_abi_constant(&u, IR_I64, ~UINT64_C(31), 0), BUSTER_IR_NONE);
        const uint32_t argument = ir_interpreter_abi_emit(&u, IR_INTTOPTR, IR_PTR, rounded, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t k = 0; k < 4; k += 1)
        {
            ir_interpreter_abi_store(&u, out, 8 * k, ir_interpreter_abi_load(&u, IR_I64, argument, 8 * k));
        }
        ir_interpreter_abi_emit(&u, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&u);

        const uint64_t words[4] = { UINT64_C(0x1111111111111111), UINT64_C(0x2222222222222222), UINT64_C(0x3333333333333333), UINT64_C(0x4444444444444444) };
        // The callers' objects are aligned to 16 alone, as their bytes' copies don't depend on it: an object aligned more
        // would align the arena past the shifts below, which then wouldn't reach the callee's frame.
        caller.slots[0] = ir_interpreter_abi_slot(32, 0);
        caller.abi_calls[0] = (IrAbiCall){ .call = { .stack_bytes = 32, .stack_alignment_log2 = 5, .integer_registers = 1 }, .passed = 0 };
        caller.passed[0] = (IrPassed){ .location = ir_interpreter_abi_registers(BUSTER_ABI_RDI, BUSTER_IR_INTERPRETER_ABI_NONE) };
        caller.passed[1] = (IrPassed){ .location = ir_interpreter_abi_stack(0), .size = 32 };
        const uint32_t into = ir_interpreter_abi_emit(&c, IR_SYMBOL_ADDR, IR_PTR, 40, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t a = ir_interpreter_abi_emit(&c, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t k = 0; k < 4; k += 1)
        {
            ir_interpreter_abi_store(&c, a, 8 * k, ir_interpreter_abi_constant(&c, IR_I64, words[k], 0));
        }
        const uint32_t arguments[] = { into, a };
        ir_interpreter_abi_call(&c, 0, 50, arguments, 2, 1, 0, IR_VOID, 1, BUSTER_IR_NONE);
        ir_interpreter_abi_emit(&c, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&c);

        remainder.parameter_types[0] = IR_PTR;
        remainder.parameter_passed = 1;
        remainder.passed[0] = (IrPassed){ .location = ir_interpreter_abi_stack(0), .size = 128 };
        const uint32_t s = ir_interpreter_abi_emit(&n, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t bits = ir_interpreter_abi_emit(&n, IR_PTRTOINT, IR_I64, s, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const uint32_t low = ir_interpreter_abi_emit(&n, IR_AND, IR_I64, bits, ir_interpreter_abi_constant(&n, IR_I64, 127, 0), BUSTER_IR_NONE);
        ir_interpreter_abi_emit(&n, IR_RET, IR_VOID, low, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&n);

        remainder_caller.slots[0] = ir_interpreter_abi_slot(128, 0);
        remainder_caller.abi_calls[0] = (IrAbiCall){ .call = { .stack_bytes = 128, .stack_alignment_log2 = 7 }, .passed = 0 };
        remainder_caller.passed[0] = remainder.passed[0];
        const uint32_t o = ir_interpreter_abi_emit(&m, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_store(&m, o, 0, ir_interpreter_abi_constant(&m, IR_I64, words[0], 0));
        const uint32_t passed[] = { o };
        const uint32_t result = ir_interpreter_abi_call(&m, 0, 52, passed, 1, 1, 0, IR_I64, 1, BUSTER_IR_NONE);
        ir_interpreter_abi_emit(&m, IR_RET, IR_VOID, result, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&m);

        uint64_t bytes[4] = { 0 };
        const InterpreterBinding binding = { .symbol = 40, .bytes = (uint8_t*)bytes, .size = sizeof(bytes), .writable = true };
        const IrInterpreterFunction functions[] = {
            { .symbol = 50, .function = &reader },
            { .symbol = 51, .function = &caller },
            { .symbol = 52, .function = &remainder },
            { .symbol = 53, .function = &remainder_caller },
        };
        const IrInterpreterProgram program = { .functions = functions, .function_count = 4, .bindings = &binding, .binding_count = 1 };
        for (uint32_t step = 0; step < 8; step += 1)
        {
            const uint64_t before = execution->length;
            const bool shifted = arena_push(execution, 0, 128) != 0 && (step == 0 || arena_push(execution, 16 * step, 1) != 0);
            ir_interpreter_test_expect(tally, shifted, "aligned arguments fixture shift");
            memset(bytes, 0, sizeof(bytes));
            ir_interpreter_test_run(tally, &program, 51, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ 0 }, "an unnamed argument aligned to 32 in an overflow area aligned so");
            ir_interpreter_test_expect(tally, memcmp(bytes, words, sizeof(words)) == 0, "va_arg's address rounded up to 32 is the unnamed struct's");
            ir_interpreter_test_run(tally, &program, 53, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = 0 }, "a named aggregate aligned to 128 copied to an address aligned so");
            arena_pop_to(execution, before);
        }
        remainder_caller.abi_calls[0].call.stack_alignment_log2 = 4;
        remainder.incoming.stack_alignment_log2 = 7;
        for (uint32_t step = 0; step < 8; step += 1)
        {
            const uint64_t before = execution->length;
            const bool shifted = arena_push(execution, 0, 128) != 0 && (step == 0 || arena_push(execution, 16 * step, 1) != 0);
            ir_interpreter_test_expect(tally, shifted, "aligned arguments fixture shift");
            ir_interpreter_test_run(tally, &program, 53, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = 0 }, "a named aggregate aligned as its callee's incoming frame says");
            arena_pop_to(execution, before);
        }
        remainder.incoming.stack_alignment_log2 = 0;
        remainder_caller.abi_calls[0].call.stack_alignment_log2 = BUSTER_IR_INTERPRETER_MAX_STACK_ALIGNMENT_LOG2 + 1;
        ir_interpreter_test_run(tally, &program, 53, 0, 0, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "a call's argument area aligned past 2^30");
        caller.abi_calls[0].call.stack_alignment_log2 = BUSTER_IR_INTERPRETER_MAX_STACK_ALIGNMENT_LOG2 + 1;
        ir_interpreter_test_run(tally, &program, 51, 0, 0, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "a variadic call's argument area aligned past 2^30");
    }
    arena_pop_to(memory, mark);
}

typedef struct IrInterpreterAbiPair IrInterpreterAbiPair;
struct IrInterpreterAbiPair
{
    int64_t a;
    int64_t b;
};

typedef struct IrInterpreterAbiMixed IrInterpreterAbiMixed;
struct IrInterpreterAbiMixed
{
    double d;
    int32_t i;
};

typedef struct IrInterpreterAbiFloats IrInterpreterAbiFloats;
struct IrInterpreterAbiFloats
{
    float x;
    float y;
    float z;
};

typedef struct IrInterpreterAbiBig IrInterpreterAbiBig;
struct IrInterpreterAbiBig
{
    int64_t v[4];
};

typedef struct IrInterpreterAbiTiny IrInterpreterAbiTiny;
struct IrInterpreterAbiTiny
{
    uint8_t c[3];
};

typedef struct IrInterpreterAbiLong IrInterpreterAbiLong;
struct IrInterpreterAbiLong
{
    long double v;
};

static __attribute__((noinline)) IrInterpreterAbiPair ir_interpreter_abi_pair(IrInterpreterAbiPair p, int64_t x)
{
    const IrInterpreterAbiPair result = { .a = p.a * x + 1, .b = p.b - x };
    return result;
}

static __attribute__((noinline)) IrInterpreterAbiMixed ir_interpreter_abi_mixed(IrInterpreterAbiMixed m, float f)
{
    const IrInterpreterAbiMixed result = { .d = m.d * f, .i = m.i + 3 };
    return result;
}

static __attribute__((noinline)) IrInterpreterAbiFloats ir_interpreter_abi_floats(IrInterpreterAbiFloats f, double s)
{
    const IrInterpreterAbiFloats result = { .x = f.z * (float)s, .y = f.x + 1, .z = f.y - 1 };
    return result;
}

static __attribute__((noinline)) IrInterpreterAbiBig ir_interpreter_abi_big(int64_t x, IrInterpreterAbiBig b)
{
    IrInterpreterAbiBig result;
    for (uint32_t k = 0; k < 4; k += 1)
    {
        result.v[k] = b.v[3 - k] ^ x;
    }
    return result;
}

static __attribute__((noinline)) int64_t ir_interpreter_abi_tiny(IrInterpreterAbiTiny t, IrInterpreterAbiBig b, int64_t x)
{
    return t.c[0] + 3 * t.c[1] + 5 * t.c[2] + b.v[1] * x;
}

static __attribute__((noinline)) IrInterpreterAbiLong ir_interpreter_abi_long(double d, IrInterpreterAbiPair p)
{
    const IrInterpreterAbiLong result = { .v = (long double)d * p.a };
    return result;
}

// Calls with records to natives clang compiled, against calls of them from C: a struct of two longs in rdi and rsi
// returned in rax and rdx, a double and an int in xmm0 and rdi returned in xmm0 and rax, three floats in xmm0 and xmm1, 32
// bytes on the stack returned through a hidden pointer, 3 bytes in rdi beside 32 on the stack, and a long double struct
// returned in st0. Then what the bridge can't place: a register no argument goes in, a stack aligned past 16, a result in
// st1, a struct of 16 bytes in one vector register.
static void ir_interpreter_test_native_records(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
    const uint64_t mark = memory->length;
    IrFunction caller = { 0 };
    IrInterpreterAbiBuilder n;
    const bool created = ir_interpreter_abi_create(memory, &caller, (IrSizes){ .slot_count = 10, .extra_count = 32, .call_count = 6, .call_type_count = 32, .abi_call_count = 6, .passed_count = 32 }, IR_I64, &n);
    ir_interpreter_test_expect(tally, created, "native records fixture allocation");
    if (created)
    {
        const uint64_t sizes[] = { 16, 16, 16, 16, 16, 16, 32, 32, 16, 16 };
        for (uint32_t slot = 0; slot < 10; slot += 1)
        {
            caller.slots[slot] = ir_interpreter_abi_slot(sizes[slot], 0);
        }
        const IrAbiCall calls[] = {
            { .call = { .stack_alignment_log2 = 4, .integer_registers = 3, .result = ir_interpreter_abi_return(BUSTER_ABI_RESULT_REGISTERS, 8, BUSTER_ABI_RAX, BUSTER_ABI_RDX) }, .passed = 0, .result_size = 16 },
            { .call = { .stack_alignment_log2 = 4, .integer_registers = 1, .vector_registers = 2, .result = ir_interpreter_abi_return(BUSTER_ABI_RESULT_REGISTERS, 8, BUSTER_IR_INTERPRETER_ABI_XMM(0), BUSTER_ABI_RAX) }, .passed = 2, .result_size = 16 },
            { .call = { .stack_alignment_log2 = 4, .vector_registers = 3, .result = ir_interpreter_abi_return(BUSTER_ABI_RESULT_REGISTERS, 8, BUSTER_IR_INTERPRETER_ABI_XMM(0), BUSTER_IR_INTERPRETER_ABI_XMM(1)) }, .passed = 4, .result_size = 12 },
            { .call = { .stack_bytes = 32, .stack_alignment_log2 = 4, .integer_registers = 2, .result = ir_interpreter_abi_return(BUSTER_ABI_RESULT_MEMORY, 0, BUSTER_IR_INTERPRETER_ABI_NONE, BUSTER_IR_INTERPRETER_ABI_NONE) }, .passed = 6, .result_size = 32 },
            { .call = { .stack_bytes = 32, .stack_alignment_log2 = 4, .integer_registers = 2, .result = ir_interpreter_abi_return(BUSTER_ABI_RESULT_REGISTERS, 8, BUSTER_ABI_RAX, BUSTER_IR_INTERPRETER_ABI_NONE) }, .passed = 8 },
            { .call = { .stack_alignment_log2 = 4, .integer_registers = 2, .vector_registers = 1, .result = ir_interpreter_abi_return(BUSTER_ABI_RESULT_REGISTERS, 16, BUSTER_ABI_REGISTER_X87, BUSTER_IR_INTERPRETER_ABI_NONE) }, .passed = 11, .result_size = 16 },
        };
        memcpy(caller.abi_calls, calls, sizeof(calls));
        const IrPassed places[] = {
            { .location = ir_interpreter_abi_registers(BUSTER_ABI_RDI, BUSTER_ABI_RSI), .size = 16 },
            { .location = ir_interpreter_abi_registers(BUSTER_ABI_RDX, BUSTER_IR_INTERPRETER_ABI_NONE) },
            { .location = ir_interpreter_abi_registers(BUSTER_IR_INTERPRETER_ABI_XMM(0), BUSTER_ABI_RDI), .size = 16 },
            { .location = ir_interpreter_abi_registers(BUSTER_IR_INTERPRETER_ABI_XMM(1), BUSTER_IR_INTERPRETER_ABI_NONE) },
            { .location = ir_interpreter_abi_registers(BUSTER_IR_INTERPRETER_ABI_XMM(0), BUSTER_IR_INTERPRETER_ABI_XMM(1)), .size = 12 },
            { .location = ir_interpreter_abi_registers(BUSTER_IR_INTERPRETER_ABI_XMM(2), BUSTER_IR_INTERPRETER_ABI_NONE) },
            { .location = ir_interpreter_abi_registers(BUSTER_ABI_RSI, BUSTER_IR_INTERPRETER_ABI_NONE) },
            { .location = ir_interpreter_abi_stack(0), .size = 32 },
            { .location = ir_interpreter_abi_registers(BUSTER_ABI_RDI, BUSTER_IR_INTERPRETER_ABI_NONE), .size = 3 },
            { .location = ir_interpreter_abi_stack(0), .size = 32 },
            { .location = ir_interpreter_abi_registers(BUSTER_ABI_RSI, BUSTER_IR_INTERPRETER_ABI_NONE) },
            { .location = ir_interpreter_abi_registers(BUSTER_IR_INTERPRETER_ABI_XMM(0), BUSTER_IR_INTERPRETER_ABI_NONE) },
            { .location = ir_interpreter_abi_registers(BUSTER_ABI_RDI, BUSTER_ABI_RSI), .size = 16 },
        };
        memcpy(caller.passed, places, sizeof(places));
        const uint32_t out = ir_interpreter_abi_emit(&n, IR_SYMBOL_ADDR, IR_PTR, 60, BUSTER_IR_NONE, BUSTER_IR_NONE);
        uint32_t slots[10];
        for (uint32_t slot = 0; slot < 10; slot += 1)
        {
            slots[slot] = ir_interpreter_abi_emit(&n, IR_STACK_ADDR, IR_PTR, slot, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        const float floats[] = { 1.25f, 2.5f, 4.0f, 1.5f };
        uint32_t float_bits[4];
        memcpy(float_bits, floats, sizeof(floats));
        const double doubles[] = { 2.5, 3.0, 1.5 };
        uint64_t double_bits[3];
        memcpy(double_bits, doubles, sizeof(doubles));
        ir_interpreter_abi_store(&n, slots[0], 0, ir_interpreter_abi_constant(&n, IR_I64, 6, 0));
        ir_interpreter_abi_store(&n, slots[0], 8, ir_interpreter_abi_constant(&n, IR_I64, 100, 0));
        const uint32_t pair_arguments[] = { slots[0], ir_interpreter_abi_constant(&n, IR_I64, 7, 0) };
        ir_interpreter_abi_call(&n, 0, 70, pair_arguments, 2, 2, 0, IR_VOID, 1, slots[1]);
        ir_interpreter_abi_store(&n, slots[2], 0, ir_interpreter_abi_constant(&n, IR_F64, double_bits[0], 0));
        ir_interpreter_abi_store(&n, slots[2], 8, ir_interpreter_abi_constant(&n, IR_I32, 40, 0));
        const uint32_t mixed_arguments[] = { slots[2], ir_interpreter_abi_constant(&n, IR_F32, float_bits[3], 0) };
        ir_interpreter_abi_call(&n, 1, 71, mixed_arguments, 2, 2, 2, IR_VOID, 2, slots[3]);
        for (uint32_t k = 0; k < 3; k += 1)
        {
            ir_interpreter_abi_store(&n, slots[4], 4 * k, ir_interpreter_abi_constant(&n, IR_F32, float_bits[k], 0));
        }
        const uint32_t floats_arguments[] = { slots[4], ir_interpreter_abi_constant(&n, IR_F64, double_bits[1], 0) };
        ir_interpreter_abi_call(&n, 2, 72, floats_arguments, 2, 2, 4, IR_VOID, 3, slots[5]);
        for (uint32_t k = 0; k < 4; k += 1)
        {
            ir_interpreter_abi_store(&n, slots[6], 8 * k, ir_interpreter_abi_constant(&n, IR_I64, 10 * (k + 1), 0));
        }
        const uint32_t big_arguments[] = { ir_interpreter_abi_constant(&n, IR_I64, 5, 0), slots[6] };
        ir_interpreter_abi_call(&n, 3, 73, big_arguments, 2, 2, 6, IR_VOID, 4, slots[7]);
        for (uint32_t k = 0; k < 3; k += 1)
        {
            ir_interpreter_abi_store(&n, slots[8], k, ir_interpreter_abi_constant(&n, IR_I8, k + 1, 0));
        }
        const uint32_t tiny_arguments[] = { slots[8], slots[6], ir_interpreter_abi_constant(&n, IR_I64, 9, 0) };
        const uint32_t tiny = ir_interpreter_abi_call(&n, 4, 74, tiny_arguments, 3, 3, 8, IR_I64, 5, BUSTER_IR_NONE);
        const uint32_t long_arguments[] = { ir_interpreter_abi_constant(&n, IR_F64, double_bits[2], 0), slots[0] };
        ir_interpreter_abi_call(&n, 5, 75, long_arguments, 2, 2, 11, IR_VOID, 6, slots[9]);
        const IrType result_types[] = { IR_I64, IR_I64, IR_F64, IR_I32, IR_F32, IR_F32, IR_F32, IR_I64, IR_I64, IR_I64, IR_I64, IR_F80 };
        const uint32_t result_slots[] = { 1, 1, 3, 3, 5, 5, 5, 7, 7, 7, 7, 9 };
        const uint32_t result_offsets[] = { 0, 8, 0, 8, 0, 4, 8, 0, 8, 16, 24, 0 };
        const uint32_t out_offsets[] = { 0, 8, 16, 24, 32, 36, 40, 48, 56, 64, 72, 96 };
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(result_types); k += 1)
        {
            ir_interpreter_abi_store(&n, out, out_offsets[k], ir_interpreter_abi_load(&n, result_types[k], slots[result_slots[k]], result_offsets[k]));
        }
        ir_interpreter_abi_store(&n, out, 80, tiny);
        ir_interpreter_abi_emit(&n, IR_RET, IR_VOID, tiny, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_abi_finish(&n);

        // The same calls from C.
        const IrInterpreterAbiPair pair = { .a = 6, .b = 100 };
        const IrInterpreterAbiMixed mixed = { .d = doubles[0], .i = 40 };
        const IrInterpreterAbiFloats three = { .x = floats[0], .y = floats[1], .z = floats[2] };
        const IrInterpreterAbiBig big = { .v = { 10, 20, 30, 40 } };
        const IrInterpreterAbiTiny small = { .c = { 1, 2, 3 } };
        const IrInterpreterAbiPair pair_result = ir_interpreter_abi_pair(pair, 7);
        const IrInterpreterAbiMixed mixed_result = ir_interpreter_abi_mixed(mixed, floats[3]);
        const IrInterpreterAbiFloats floats_result = ir_interpreter_abi_floats(three, doubles[1]);
        const IrInterpreterAbiBig big_result = ir_interpreter_abi_big(5, big);
        const int64_t tiny_result = ir_interpreter_abi_tiny(small, big, 9);
        const IrInterpreterAbiLong long_result = ir_interpreter_abi_long(doubles[2], pair);
        uint8_t expected[128] = { 0 };
        memcpy(expected, &pair_result, 16);
        memcpy(expected + 16, &mixed_result.d, 8);
        memcpy(expected + 24, &mixed_result.i, 4);
        memcpy(expected + 32, &floats_result, 12);
        memcpy(expected + 48, &big_result, 32);
        memcpy(expected + 80, &tiny_result, 8);
        memcpy(expected + 96, &long_result.v, 10);
        uint8_t bytes[128] = { 0 };
        const InterpreterBinding binding = { .symbol = 60, .bytes = bytes, .size = sizeof(bytes), .writable = true };
        const InterpreterNativeBinding natives[] = {
            { .symbol = 70, .address = (void*)&ir_interpreter_abi_pair },
            { .symbol = 71, .address = (void*)&ir_interpreter_abi_mixed },
            { .symbol = 72, .address = (void*)&ir_interpreter_abi_floats },
            { .symbol = 73, .address = (void*)&ir_interpreter_abi_big },
            { .symbol = 74, .address = (void*)&ir_interpreter_abi_tiny },
            { .symbol = 75, .address = (void*)&ir_interpreter_abi_long },
        };
        const IrInterpreterFunction function = { .symbol = 50, .function = &caller };
        const IrInterpreterProgram program = { .functions = &function, .function_count = 1, .bindings = &binding, .binding_count = 1, .natives = natives, .native_count = BUSTER_ARRAY_LENGTH(natives) };
        ir_interpreter_test_run(tally, &program, 50, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = (uint64_t)tiny_result }, "natives with records, as clang's code takes and returns aggregates");
        ir_interpreter_test_expect(tally, memcmp(bytes, expected, sizeof(bytes)) == 0, "every aggregate result's bytes where C's calls put them");
        // What the bridge can't place, each refused before the native runs.
        caller.passed[0].location.registers[0] = BUSTER_ABI_R10;
        ir_interpreter_test_run(tally, &program, 50, 0, 0, 0, execution, INTERPRETER_NATIVE, (InterpreterValue){ 0 }, "an argument in a register no argument goes in");
        caller.passed[0].location.registers[0] = BUSTER_ABI_RDI;
        caller.abi_calls[0].call.stack_alignment_log2 = 5;
        ir_interpreter_test_run(tally, &program, 50, 0, 0, 0, execution, INTERPRETER_NATIVE, (InterpreterValue){ 0 }, "an argument area aligned past 16");
        caller.abi_calls[0].call.stack_alignment_log2 = 4;
        caller.abi_calls[5].call.result.registers[1] = BUSTER_ABI_REGISTER_X87 + 1;
        ir_interpreter_test_run(tally, &program, 50, 0, 0, 0, execution, INTERPRETER_NATIVE, (InterpreterValue){ 0 }, "a result in st1");
        caller.abi_calls[5].call.result.registers[1] = BUSTER_IR_INTERPRETER_ABI_NONE;
        caller.passed[2].location.registers[1] = BUSTER_IR_INTERPRETER_ABI_NONE;
        ir_interpreter_test_run(tally, &program, 50, 0, 0, 0, execution, INTERPRETER_NATIVE, (InterpreterValue){ 0 }, "16 bytes in one vector register, its upper half unplaced");
        caller.passed[2].location.registers[1] = BUSTER_ABI_RDI;
        memset(bytes, 0, sizeof(bytes));
        ir_interpreter_test_run(tally, &program, 50, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = (uint64_t)tiny_result }, "the natives run again after their refusals");
        ir_interpreter_test_expect(tally, memcmp(bytes, expected, sizeof(bytes)) == 0, "the same bytes again");
    }
    arena_pop_to(memory, mark);
}
