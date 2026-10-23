// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent scalar execution expectations, explicit-frame recursion, simultaneous loop phis, checked memory and
// generated typed-stream comparisons. Include after ir_interpreter.c, ir_lower.c, tier0_generate.c and tier0_test.c.
// Generated integer/pointer workloads avoid the generator's documented out-of-range floating-conversion cases;
// floating precision, payloads and conversion boundaries have independently expected fixtures here instead.

#include <errno.h>
#include <stdlib.h>

#define BUSTER_IR_INTERPRETER_TEST_BYTES (64ull << 20)

typedef struct IrInterpreterTestTally IrInterpreterTestTally;
struct IrInterpreterTestTally
{
    uint64_t checks;
    uint64_t failures;
};

static void ir_interpreter_test_expect(IrInterpreterTestTally* tally, bool condition, const char* message)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 20)
        {
            fprintf(stderr, "IR interpreter: %s\n", message);
        }
        tally->failures += 1;
    }
}

static void ir_interpreter_test_instruction(IrFunction* function, uint32_t id, IrOp op, IrType type, uint32_t a, uint32_t b, uint32_t c)
{
    function->op[id] = (uint8_t)op;
    function->type[id] = (uint8_t)type;
    function->a[id] = a;
    function->b[id] = b;
    function->c[id] = c;
}

static InterpreterResult ir_interpreter_test_run(IrInterpreterTestTally* tally, const IrInterpreterProgram* program, SymbolId entry, const InterpreterValue* arguments, uint32_t argument_count, const InterpreterOptions* options, Arena* arena, InterpreterStatus expected_status, InterpreterValue expected_value, const char* message)
{
    const uint64_t mark = arena->length;
    const InterpreterResult result = ir_interpret(program, entry, arguments, argument_count, options, arena);
    const bool correct = result.status == expected_status && (expected_status != INTERPRETER_OK || (result.value.low == expected_value.low && result.value.high == expected_value.high));
    ir_interpreter_test_expect(tally, correct, message);
    ir_interpreter_test_expect(tally, arena->length == mark, "execution retained scratch after success or failure");
    if (!correct && tally->failures < 20)
    {
        fprintf(stderr, "  status %u, expected %u; value %016" PRIx64 "%016" PRIx64 ", expected %016" PRIx64 "%016" PRIx64 "; function %u entry %u\n", result.status, expected_status, result.value.high, result.value.low, expected_value.high, expected_value.low, result.function, result.entry);
    }
    return result;
}

typedef struct IrInterpreterTestNumber IrInterpreterTestNumber;
struct IrInterpreterTestNumber
{
    IrOp op;
    IrType type;
    IrType source;
    InterpreterValue a;
    InterpreterValue b;
    uint32_t predicate;
    InterpreterValue expected;
    InterpreterStatus status;
    const char* name;
};

static void ir_interpreter_test_numbers(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
    const IrInterpreterTestNumber cases[] = {
        { IR_ADD, IR_I128, IR_I128, { UINT64_MAX, UINT64_MAX }, { 1, 0 }, 0, { 0, 0 }, INTERPRETER_OK, "i128 addition wraps" },
        { IR_SUB, IR_I128, IR_I128, { 0, 0 }, { 1, 0 }, 0, { UINT64_MAX, UINT64_MAX }, INTERPRETER_OK, "i128 subtraction wraps" },
        { IR_MUL, IR_I128, IR_I128, { 0, 1ull << 63 }, { 2, 0 }, 0, { 0, 0 }, INTERPRETER_OK, "i128 multiplication wraps" },
        { IR_UDIV, IR_I128, IR_I128, { 0, 1ull << 63 }, { 2, 0 }, 0, { 0, 1ull << 62 }, INTERPRETER_OK, "i128 unsigned division" },
        { IR_UREM, IR_I128, IR_I128, { 5, 1 }, { 3, 0 }, 0, { 0, 0 }, INTERPRETER_OK, "i128 unsigned remainder includes high word" },
        { IR_SDIV, IR_I128, IR_I128, { UINT64_MAX - 6, UINT64_MAX }, { 3, 0 }, 0, { UINT64_MAX - 1, UINT64_MAX }, INTERPRETER_OK, "i128 signed division truncates toward zero" },
        { IR_SREM, IR_I128, IR_I128, { UINT64_MAX - 6, UINT64_MAX }, { 3, 0 }, 0, { UINT64_MAX, UINT64_MAX }, INTERPRETER_OK, "i128 signed remainder follows dividend" },
        { IR_UDIV, IR_I128, IR_I128, { 1, 0 }, { 0, 0 }, 0, { 0, 0 }, INTERPRETER_ARITHMETIC, "division by zero rejected" },
        { IR_SDIV, IR_I128, IR_I128, { 0, 1ull << 63 }, { UINT64_MAX, UINT64_MAX }, 0, { 0, 0 }, INTERPRETER_ARITHMETIC, "i128 signed division overflow rejected" },
        { IR_SREM, IR_I8, IR_I8, { 128, 0 }, { 255, 0 }, 0, { 0, 0 }, INTERPRETER_ARITHMETIC, "narrow signed remainder overflow rejected" },
        { IR_AND, IR_I128, IR_I128, { UINT64_MAX, 0x55 }, { 0x42, 0xf0 }, 0, { 0x42, 0x50 }, INTERPRETER_OK, "i128 bitwise and" },
        { IR_OR, IR_I128, IR_I128, { 1, 0x55 }, { 2, 0xf0 }, 0, { 3, 0xf5 }, INTERPRETER_OK, "i128 bitwise or" },
        { IR_XOR, IR_I128, IR_I128, { 1, 0x55 }, { 3, 0xf0 }, 0, { 2, 0xa5 }, INTERPRETER_OK, "i128 bitwise xor" },
        { IR_SHL, IR_I128, IR_I128, { 1, 0 }, { 127, 0 }, 0, { 0, 1ull << 63 }, INTERPRETER_OK, "i128 shift across both words" },
        { IR_LSHR, IR_I128, IR_I128, { 0, 1ull << 63 }, { 127, 0 }, 0, { 1, 0 }, INTERPRETER_OK, "i128 logical shift" },
        { IR_ASHR, IR_I128, IR_I128, { 0, 1ull << 63 }, { 127, 0 }, 0, { UINT64_MAX, UINT64_MAX }, INTERPRETER_OK, "i128 arithmetic shift" },
        { IR_SHL, IR_I128, IR_I128, { 1, 0 }, { 0, 1 }, 0, { 0, 0 }, INTERPRETER_ARITHMETIC, "full i128 shift count checked" },
        { IR_SHL, IR_I8, IR_I8, { 1, 0 }, { 8, 0 }, 0, { 0, 0 }, INTERPRETER_ARITHMETIC, "narrow shift count checked" },
        { IR_ASHR, IR_I8, IR_I8, { 128, 0 }, { 7, 0 }, 0, { 255, 0 }, INTERPRETER_OK, "narrow arithmetic shift sign extends" },
        { IR_ADD, IR_I8, IR_I8, { 255, 0 }, { 1, 0 }, 0, { 0, 0 }, INTERPRETER_OK, "narrow integer arithmetic wraps" },
        { IR_NEG, IR_I1, IR_I1, { 1, 0 }, { 0, 0 }, 0, { 1, 0 }, INTERPRETER_OK, "i1 negation wraps" },
        { IR_NOT, IR_I128, IR_I128, { 0, 1 }, { 0, 0 }, 0, { UINT64_MAX, UINT64_MAX - 1 }, INTERPRETER_OK, "i128 complement" },
        { IR_TRUNC, IR_I8, IR_I128, { 0x1234, 4 }, { 0, 0 }, 0, { 0x34, 0 }, INTERPRETER_OK, "wide truncation" },
        { IR_ZEXT, IR_I128, IR_I8, { 255, 0 }, { 0, 0 }, 0, { 255, 0 }, INTERPRETER_OK, "wide zero extension" },
        { IR_SEXT, IR_I128, IR_I1, { 1, 0 }, { 0, 0 }, 0, { UINT64_MAX, UINT64_MAX }, INTERPRETER_OK, "i1 sign extension" },
        { IR_ICMP, IR_I1, IR_I128, { 0, 1ull << 63 }, { 0, 0 }, IR_LT, { 1, 0 }, INTERPRETER_OK, "signed i128 comparison" },
        { IR_ICMP, IR_I1, IR_I128, { 0, 1ull << 63 }, { 0, 0 }, IR_ULT, { 0, 0 }, INTERPRETER_OK, "unsigned i128 comparison" },
        { IR_FADD, IR_F32, IR_F32, { 0x4b800000, 0 }, { 0x3f800000, 0 }, 0, { 0x4b800000, 0 }, INTERPRETER_OK, "f32 arithmetic rounds at f32 precision" },
        { IR_FADD, IR_F64, IR_F64, { 0x4340000000000000ull, 0 }, { 0x3ff0000000000000ull, 0 }, 0, { 0x4340000000000000ull, 0 }, INTERPRETER_OK, "f64 arithmetic rounds at f64 precision" },
        { IR_FADD, IR_F80, IR_F80, { 1ull << 63, 0x3fff }, { 1ull << 63, 0x3fc0 }, 0, { (1ull << 63) + 1, 0x3fff }, INTERPRETER_OK, "f80 retains sixty fourth significand bit" },
        { IR_FSUB, IR_F64, IR_F64, { 0x4008000000000000ull, 0 }, { 0x3ff0000000000000ull, 0 }, 0, { 0x4000000000000000ull, 0 }, INTERPRETER_OK, "floating subtraction" },
        { IR_FMUL, IR_F32, IR_F32, { 0x40000000, 0 }, { 0x40400000, 0 }, 0, { 0x40c00000, 0 }, INTERPRETER_OK, "floating multiplication" },
        { IR_FDIV, IR_F80, IR_F80, { 0xc000000000000000ull, 0x4000 }, { 1ull << 63, 0x4000 }, 0, { 0xc000000000000000ull, 0x3fff }, INTERPRETER_OK, "f80 division" },
        { IR_FNEG, IR_F80, IR_F80, { 0xc000000000000123ull, 0x7fff }, { 0, 0 }, 0, { 0xc000000000000123ull, 0xffff }, INTERPRETER_OK, "f80 negation preserves NaN payload" },
        { IR_FCMP, IR_I1, IR_F64, { 0x7ff8000000000001ull, 0 }, { 0, 0 }, IR_NE, { 1, 0 }, INTERPRETER_OK, "NaN not equal is true" },
        { IR_FCMP, IR_I1, IR_F64, { 0x7ff8000000000001ull, 0 }, { 0, 0 }, IR_LT, { 0, 0 }, INTERPRETER_OK, "NaN ordered comparison is false" },
        { IR_FTRUNC, IR_F64, IR_F80, { (1ull << 63) + 1, 0x3fff }, { 0, 0 }, 0, { 0x3ff0000000000000ull, 0 }, INTERPRETER_OK, "f80 narrowing rounds" },
        { IR_FEXT, IR_F80, IR_F32, { 0x3fc00000, 0 }, { 0, 0 }, 0, { 0xc000000000000000ull, 0x3fff }, INTERPRETER_OK, "floating extension" },
        { IR_UITOFP, IR_F80, IR_I128, { 0, 1 }, { 0, 0 }, 0, { 1ull << 63, 0x403f }, INTERPRETER_OK, "unsigned i128 to f80 conversion" },
        { IR_SITOFP, IR_F64, IR_I128, { UINT64_MAX, UINT64_MAX }, { 0, 0 }, 0, { 0xbff0000000000000ull, 0 }, INTERPRETER_OK, "signed i128 to floating conversion" },
        { IR_FPTOUI, IR_I8, IR_F64, { 0xbfe0000000000000ull, 0 }, { 0, 0 }, 0, { 0, 0 }, INTERPRETER_OK, "negative fraction truncates to unsigned zero" },
        { IR_FPTOSI, IR_I8, IR_F64, { 0xc060100000000000ull, 0 }, { 0, 0 }, 0, { 128, 0 }, INTERPRETER_OK, "negative fraction near signed minimum truncates first" },
        { IR_FPTOSI, IR_I8, IR_F64, { 0x4060000000000000ull, 0 }, { 0, 0 }, 0, { 0, 0 }, INTERPRETER_ARITHMETIC, "floating signed conversion upper boundary rejected" },
        { IR_FPTOSI, IR_I128, IR_F80, { 1ull << 63, 0xc07e }, { 0, 0 }, 0, { 0, 1ull << 63 }, INTERPRETER_OK, "floating i128 minimum accepted" },
        { IR_FPTOSI, IR_I128, IR_F80, { 1ull << 63, 0x407e }, { 0, 0 }, 0, { 0, 0 }, INTERPRETER_ARITHMETIC, "floating i128 upper boundary rejected" },
        { IR_FPTOUI, IR_I128, IR_F80, { UINT64_MAX, 0x407e }, { 0, 0 }, 0, { 0, UINT64_MAX }, INTERPRETER_OK, "floating to unsigned i128 keeps high word" },
        { IR_FPTOSI, IR_I64, IR_F64, { 0x7ff8000000000001ull, 0 }, { 0, 0 }, 0, { 0, 0 }, INTERPRETER_ARITHMETIC, "NaN integer conversion rejected" },
        { IR_FPTOUI, IR_I128, IR_F80, { 1ull << 63, 0x7fff }, { 0, 0 }, 0, { 0, 0 }, INTERPRETER_ARITHMETIC, "infinite integer conversion rejected" },
        { IR_BITCAST, IR_I64, IR_F64, { 0x8000000000000000ull, 0 }, { 0, 0 }, 0, { 0x8000000000000000ull, 0 }, INTERPRETER_OK, "bitcast preserves negative zero" },
        { IR_INTTOPTR, IR_PTR, IR_I128, { 0x1234, 8 }, { 0, 0 }, 0, { 0x1234, 0 }, INTERPRETER_OK, "integer to pointer truncates to target width" },
        { IR_PTRTOINT, IR_I128, IR_PTR, { 0x1234, 0 }, { 0, 0 }, 0, { 0x1234, 0 }, INTERPRETER_OK, "pointer to integer zero extends" },
    };
    for (uint32_t t = 0; t < sizeof(cases) / sizeof(cases[0]); t += 1)
    {
        const uint64_t mark = memory->length;
        const IrInterpreterTestNumber test = cases[t];
        IrFunction function = { 0 };
        const IrSizes sizes = {
            .instruction_count = 4,
            .block_count = 1,
            .constant_count = 2,
        };
        const bool created = ir_create(memory, &sizes, &function) == IR_OK;
        ir_interpreter_test_expect(tally, created, "numeric fixture allocation");
        if (created)
        {
            function.result_type = (uint8_t)test.type;
            function.constants[0] = (IrConstant){ .low = test.a.low, .high = test.a.high };
            function.constants[1] = (IrConstant){ .low = test.b.low, .high = test.b.high };
            function.block_offsets[1] = 4;
            ir_interpreter_test_instruction(&function, 0, IR_CONST, test.source, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&function, 1, IR_CONST, test.source, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint8_t operands = ir_operand_table[test.op];
            const uint32_t b = (operands & 2) != 0 ? 1 : BUSTER_IR_NONE;
            const uint32_t c = test.op == IR_ICMP || test.op == IR_FCMP ? test.predicate : BUSTER_IR_NONE;
            ir_interpreter_test_instruction(&function, 2, test.op, test.type, 0, b, c);
            ir_interpreter_test_instruction(&function, 3, IR_RET, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const IrInterpreterFunction binding = { .symbol = 91, .function = &function };
            const IrInterpreterProgram program = { .functions = &binding, .function_count = 1 };
            ir_interpreter_test_run(tally, &program, 91, 0, 0, 0, execution, test.status, test.expected, test.name);
        }
        arena_pop_to(memory, mark);
    }
}

static void ir_interpreter_test_control(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
    const uint64_t mark = memory->length;
    IrFunction loop = { 0 };
    const IrSizes sizes = {
        .instruction_count = 15,
        .block_count = 3,
        .constant_count = 5,
        .extra_count = 12,
    };
    const bool created = ir_create(memory, &sizes, &loop) == IR_OK;
    ir_interpreter_test_expect(tally, created, "loop-phi fixture allocation");
    if (created)
    {
        loop.result_type = IR_I64;
        const uint64_t constants[] = { 1, 2, 0, 10, 2 };
        const uint32_t offsets[] = { 0, 6, 12, 15 };
        const uint32_t incoming[] = { 0, 0, 1, 7, 0, 1, 1, 6, 0, 2, 1, 9 };
        memcpy(loop.block_offsets, offsets, sizeof(offsets));
        memcpy(loop.extra, incoming, sizeof(incoming));
        for (uint32_t c = 0; c < 5; c += 1)
        {
            loop.constants[c].low = constants[c];
            ir_interpreter_test_instruction(&loop, c, IR_CONST, IR_I64, c, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        ir_interpreter_test_instruction(&loop, 5, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&loop, 6, IR_PHI, IR_I64, 0, 2, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&loop, 7, IR_PHI, IR_I64, 4, 2, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&loop, 8, IR_PHI, IR_I64, 8, 2, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&loop, 9, IR_ADD, IR_I64, 8, 0, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&loop, 10, IR_ICMP, IR_I1, 9, 4, IR_LT);
        ir_interpreter_test_instruction(&loop, 11, IR_CBR, IR_VOID, 10, 1, 2);
        ir_interpreter_test_instruction(&loop, 12, IR_MUL, IR_I64, 6, 3, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&loop, 13, IR_ADD, IR_I64, 12, 7, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&loop, 14, IR_RET, IR_VOID, 13, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const IrInterpreterFunction binding = { .symbol = 71, .function = &loop };
        const IrInterpreterProgram program = { .functions = &binding, .function_count = 1 };
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = 21 }, "loop phis swap values simultaneously");
        InterpreterOptions options = interpreter_default_options();
        options.max_steps = 8;
        const InterpreterResult limited = ir_interpreter_test_run(tally, &program, 71, 0, 0, &options, execution, INTERPRETER_STEP_LIMIT, (InterpreterValue){ 0 }, "phi execution consumes global step budget");
        ir_interpreter_test_expect(tally, limited.steps == 8, "phi budget stops at its exact limit");
    }
    arena_pop_to(memory, mark);
    IrFunction selection = { 0 };
    const IrSizes selection_sizes = {
        .instruction_count = 5,
        .block_count = 1,
        .constant_count = 2,
    };
    if (ir_create(memory, &selection_sizes, &selection) == IR_OK)
    {
        selection.result_type = IR_I64;
        selection.block_offsets[1] = 5;
        selection.constants[0].low = 1;
        selection.constants[1].low = 37;
        ir_interpreter_test_instruction(&selection, 0, IR_CONST, IR_I1, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&selection, 1, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&selection, 2, IR_UNDEF, IR_I64, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&selection, 3, IR_SELECT, IR_I64, 0, 1, 2);
        ir_interpreter_test_instruction(&selection, 4, IR_RET, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const IrInterpreterFunction binding = { .symbol = 71, .function = &selection };
        const IrInterpreterProgram program = { .functions = &binding, .function_count = 1 };
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = 37 }, "select does not consume its unused undef alternative");
        selection.constants[0].low = 0;
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_UNDEFINED, (InterpreterValue){ 0 }, "return of selected undef diagnosed");
        InterpreterOptions options = interpreter_default_options();
        options.check_undefined = false;
        ir_interpreter_test_run(tally, &program, 71, 0, 0, &options, execution, INTERPRETER_OK, (InterpreterValue){ 0 }, "unchecked undef has deterministic zero payload");
        selection.a[4] = 99;
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "malformed IR is rejected before execution");
    }
    else
    {
        ir_interpreter_test_expect(tally, false, "selection fixture allocation");
    }
    arena_pop_to(memory, mark);
    IrFunction switched = { 0 };
    const IrSizes switch_sizes = {
        .instruction_count = 6,
        .block_count = 3,
        .constant_count = 3,
        .extra_count = 3,
    };
    if (ir_create(memory, &switch_sizes, &switched) == IR_OK)
    {
        switched.result_type = IR_I64;
        switched.constants[0].high = 1;
        switched.constants[1].low = 17;
        switched.constants[2].low = 29;
        const uint32_t offsets[] = { 0, 2, 4, 6 };
        const uint32_t cases[] = { 1, 0, 2 };
        memcpy(switched.block_offsets, offsets, sizeof(offsets));
        memcpy(switched.extra, cases, sizeof(cases));
        ir_interpreter_test_instruction(&switched, 0, IR_CONST, IR_I128, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&switched, 1, IR_SWITCH, IR_VOID, 0, 0, 1);
        ir_interpreter_test_instruction(&switched, 2, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&switched, 3, IR_RET, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&switched, 4, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&switched, 5, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const IrInterpreterFunction binding = { .symbol = 71, .function = &switched };
        const IrInterpreterProgram program = { .functions = &binding, .function_count = 1 };
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = 29 }, "switch compares both i128 words");
    }
    else
    {
        ir_interpreter_test_expect(tally, false, "switch fixture allocation");
    }
    arena_pop_to(memory, mark);
}

static void ir_interpreter_test_dominance(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
    const uint64_t mark = memory->length;
    IrFunction function = { 0 };
    const IrSizes sizes = {
        .instruction_count = 7,
        .block_count = 4,
        .constant_count = 2,
    };
    const bool created = ir_create(memory, &sizes, &function) == IR_OK;
    ir_interpreter_test_expect(tally, created, "cross-block dominance fixture allocation");
    if (created)
    {
        function.result_type = IR_I64;
        function.constants[0].low = 1;
        function.constants[1].low = 7;
        const uint32_t offsets[] = { 0, 2, 4, 6, 7 };
        memcpy(function.block_offsets, offsets, sizeof(offsets));
        ir_interpreter_test_instruction(&function, 0, IR_CONST, IR_I1, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&function, 1, IR_CBR, IR_VOID, 0, 1, 2);
        ir_interpreter_test_instruction(&function, 2, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&function, 3, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&function, 4, IR_COPY, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&function, 5, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&function, 6, IR_RET, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const IrInterpreterFunction code = { .symbol = 71, .function = &function };
        const IrInterpreterProgram program = { .functions = &code, .function_count = 1 };
        // The constant predicate selects the defining arm dynamically, but the CFG still admits the other arm.
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "non-dominating definition rejected before a favorable dynamic path executes");
        ir_interpreter_test_instruction(&function, 1, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = 7 }, "unreachable cross-block use does not invalidate reachable SSA");
        ir_interpreter_test_instruction(&function, 1, IR_BR, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "unreachable definition cannot satisfy a reachable use");
    }
    arena_pop_to(memory, mark);
}

static void ir_interpreter_test_calls(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
    const uint64_t mark = memory->length;
    IrFunction function = { 0 };
    IrFunction caller = { 0 };
    const IrSizes sizes = {
        .instruction_count = 10,
        .block_count = 3,
        .constant_count = 2,
        .extra_count = 1,
        .call_count = 1,
        .call_type_count = 1,
        .parameter_count = 1,
    };
    const IrSizes caller_sizes = {
        .instruction_count = 4,
        .block_count = 1,
        .extra_count = 1,
        .call_count = 1,
        .call_type_count = 1,
        .parameter_count = 1,
    };
    const bool created = ir_create(memory, &sizes, &function) == IR_OK && ir_create(memory, &caller_sizes, &caller) == IR_OK;
    ir_interpreter_test_expect(tally, created, "recursive/indirect-call fixture allocation");
    if (created)
    {
        function.result_type = IR_I64;
        function.parameter_types[0] = IR_I64;
        function.constants[1].low = 1;
        const uint32_t offsets[] = { 0, 4, 9, 10 };
        memcpy(function.block_offsets, offsets, sizeof(offsets));
        function.extra[0] = 5;
        function.call_types[0] = IR_I64;
        function.calls[0] = (IrCall){
            .argument_count = 1,
            .parameter_count = 1,
            .result_type = IR_I64,
        };
        ir_interpreter_test_instruction(&function, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&function, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&function, 2, IR_ICMP, IR_I1, 0, 1, IR_EQ);
        ir_interpreter_test_instruction(&function, 3, IR_CBR, IR_VOID, 2, 2, 1);
        ir_interpreter_test_instruction(&function, 4, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&function, 5, IR_SUB, IR_I64, 0, 4, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&function, 6, IR_CALL, IR_I64, 10, 0, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&function, 7, IR_ADD, IR_I64, 6, 4, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&function, 8, IR_RET, IR_VOID, 7, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&function, 9, IR_RET, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        caller.result_type = IR_I64;
        caller.parameter_types[0] = IR_I64;
        caller.block_offsets[1] = 4;
        caller.extra[0] = 0;
        caller.call_types[0] = IR_I64;
        caller.calls[0] = function.calls[0];
        ir_interpreter_test_instruction(&caller, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&caller, 1, IR_SYMBOL_ADDR, IR_PTR, 10, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&caller, 2, IR_CALL_INDIRECT, IR_I64, 1, 0, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&caller, 3, IR_RET, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const IrInterpreterFunction bindings[] = {
            { .symbol = 10, .function = &function },
            { .symbol = 77, .function = &caller },
        };
        const IrInterpreterProgram program = { .functions = bindings, .function_count = 2 };
        const InterpreterValue argument = { .low = 128 };
        ir_interpreter_test_run(tally, &program, 10, &argument, 1, 0, execution, INTERPRETER_OK, argument, "recursive calls use explicit arena frames");
        ir_interpreter_test_run(tally, &program, 77, &argument, 1, 0, execution, INTERPRETER_OK, argument, "indirect calls resolve internal function pointer tokens");
        InterpreterOptions options = interpreter_default_options();
        options.max_frames = 8;
        ir_interpreter_test_run(tally, &program, 77, &argument, 1, &options, execution, INTERPRETER_FRAME_LIMIT, (InterpreterValue){ 0 }, "recursive call depth limit");
        options = interpreter_default_options();
        options.max_values = 16;
        ir_interpreter_test_run(tally, &program, 77, &argument, 1, &options, execution, INTERPRETER_VALUE_LIMIT, (InterpreterValue){ 0 }, "active frames share one value limit");
        options = interpreter_default_options();
        options.max_steps = 10;
        const InterpreterResult limited = ir_interpreter_test_run(tally, &program, 77, &argument, 1, &options, execution, INTERPRETER_STEP_LIMIT, (InterpreterValue){ 0 }, "nested calls share one instruction budget");
        ir_interpreter_test_expect(tally, limited.steps == 10, "nested instruction budget exact");
        ir_interpreter_test_run(tally, &program, 11, 0, 0, 0, execution, INTERPRETER_SYMBOL, (InterpreterValue){ 0 }, "unknown entry symbol");
        ir_interpreter_test_run(tally, &program, 10, 0, 0, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "entry argument count checked");
        const uint8_t* saved_types = function.parameter_types;
        function.parameter_types = 0;
        ir_interpreter_test_run(tally, &program, 77, &argument, 1, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "malformed indirect callee rejected before signature access");
        function.parameter_types = (uint8_t*)saved_types;
        caller.a[1] = 88;
        ir_interpreter_test_run(tally, &program, 77, &argument, 1, 0, execution, INTERPRETER_SYMBOL, (InterpreterValue){ 0 }, "unresolved symbol address rejected");
        caller.a[1] = 10;
        ir_interpreter_test_run(tally, &program, 77, &argument, 1, 0, execution, INTERPRETER_OK, argument, "successful execution after fault unwinds all frames");
    }
    arena_pop_to(memory, mark);
}

static __attribute__((noinline)) int64_t ir_interpreter_test_signed_native(signed char value)
{
    return (int64_t)value * 3;
}

static void ir_interpreter_test_memory(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
    const uint64_t mark = memory->length;
    const IrType types[] = { IR_I128, IR_F80 };
    const InterpreterValue payloads[] = {
        { .low = 0x0123456789abcdefull, .high = 0xfedcba9876543210ull },
        { .low = 0xc000000000000123ull, .high = 0x7fff },
    };
    for (uint32_t t = 0; t < 2; t += 1)
    {
        IrFunction function = { 0 };
        const IrSizes sizes = {
            .instruction_count = 6,
            .block_count = 1,
            .constant_count = 1,
            .address_count = 1,
        };
        const bool created = ir_create(memory, &sizes, &function) == IR_OK;
        ir_interpreter_test_expect(tally, created, "wide memory fixture allocation");
        if (created)
        {
            const IrType type = types[t];
            const uint32_t bytes = (ir_type_bits[type] + 7) / 8;
            uint8_t data[18];
            memset(data, 0xcc, sizeof(data));
            function.result_type = (uint8_t)type;
            function.block_offsets[1] = 6;
            function.constants[0] = (IrConstant){ .low = payloads[t].low, .high = payloads[t].high };
            function.addresses[0] = (IrAddress){ .offset = 1, .scale = 1 };
            ir_interpreter_test_instruction(&function, 0, IR_SYMBOL_ADDR, IR_PTR, 99, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&function, 1, IR_ADDRESS, IR_PTR, 0, BUSTER_IR_NONE, 0);
            ir_interpreter_test_instruction(&function, 2, IR_CONST, type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&function, 3, IR_STORE, IR_VOID, 1, 2, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&function, 4, IR_LOAD, type, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_interpreter_test_instruction(&function, 5, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const IrInterpreterFunction code = { .symbol = 71, .function = &function };
            InterpreterBinding binding = { .symbol = 99, .bytes = data, .size = bytes + 2, .writable = true };
            const IrInterpreterProgram program = { .functions = &code, .function_count = 1, .bindings = &binding, .binding_count = 1 };
            ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_OK, payloads[t], "unaligned wide load/store preserves exact payload");
            ir_interpreter_test_expect(tally, data[0] == 0xcc && data[bytes + 1] == 0xcc && memcmp(data + 1, &payloads[t], bytes) == 0, "wide memory transfers preserve surrounding bytes");
            function.addresses[0].offset = 3;
            ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_MEMORY, (InterpreterValue){ 0 }, "out-of-bounds wide store rejected");
            function.addresses[0].offset = 1;
            binding.writable = false;
            ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_MEMORY, (InterpreterValue){ 0 }, "read-only global store rejected");
        }
        arena_pop_to(memory, mark);
    }
    IrFunction local = { 0 };
    const IrSizes local_sizes = {
        .instruction_count = 3,
        .block_count = 1,
        .slot_count = 1,
    };
    if (ir_create(memory, &local_sizes, &local) == IR_OK)
    {
        local.result_type = IR_I64;
        local.block_offsets[1] = 3;
        local.slots[0] = (IrSlot){ .size = 8, .alignment = 8, .type = IR_I64 };
        ir_interpreter_test_instruction(&local, 0, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&local, 1, IR_LOAD, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&local, 2, IR_RET, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const IrInterpreterFunction code = { .symbol = 71, .function = &local };
        const IrInterpreterProgram program = { .functions = &code, .function_count = 1 };
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_UNDEFINED, (InterpreterValue){ 0 }, "uninitialized stack bytes diagnosed");
        InterpreterOptions options = interpreter_default_options();
        options.check_undefined = false;
        ir_interpreter_test_run(tally, &program, 71, 0, 0, &options, execution, INTERPRETER_OK, (InterpreterValue){ 0 }, "optional uninitialized-memory check can be disabled");
        options = interpreter_default_options();
        options.max_locals = 0;
        ir_interpreter_test_run(tally, &program, 71, 0, 0, &options, execution, INTERPRETER_RESOURCE, (InterpreterValue){ 0 }, "local slot limit enforced");
        Arena tiny = arena_reserve(32);
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, &tiny, INTERPRETER_RESOURCE, (InterpreterValue){ 0 }, "tiny execution arena reports resource failure");
        arena_release(&tiny);
    }
    else
    {
        ir_interpreter_test_expect(tally, false, "uninitialized-memory fixture allocation");
    }
    arena_pop_to(memory, mark);
    IrFunction callee = { 0 };
    IrFunction caller = { 0 };
    const IrSizes callee_sizes = { .instruction_count = 2, .block_count = 1, .slot_count = 1 };
    const IrSizes caller_sizes = { .instruction_count = 3, .block_count = 1, .call_count = 1 };
    if (ir_create(memory, &callee_sizes, &callee) == IR_OK && ir_create(memory, &caller_sizes, &caller) == IR_OK)
    {
        callee.result_type = IR_PTR;
        callee.block_offsets[1] = 2;
        callee.slots[0] = (IrSlot){ .size = 8, .alignment = 8, .type = IR_I64 };
        ir_interpreter_test_instruction(&callee, 0, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&callee, 1, IR_RET, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        caller.result_type = IR_I64;
        caller.block_offsets[1] = 3;
        caller.calls[0].result_type = IR_PTR;
        ir_interpreter_test_instruction(&caller, 0, IR_CALL, IR_PTR, 10, 0, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&caller, 1, IR_LOAD, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&caller, 2, IR_RET, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const IrInterpreterFunction code[] = {
            { .symbol = 10, .function = &callee },
            { .symbol = 71, .function = &caller },
        };
        const IrInterpreterProgram program = { .functions = code, .function_count = 2 };
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_MEMORY, (InterpreterValue){ 0 }, "returned local pointer expires with its frame");
    }
    else
    {
        ir_interpreter_test_expect(tally, false, "dangling-pointer fixture allocation");
    }
    arena_pop_to(memory, mark);
    IrFunction native = { 0 };
    const IrSizes native_sizes = {
        .instruction_count = 3,
        .block_count = 1,
        .constant_count = 1,
        .call_count = 1,
        .call_type_count = 1,
        .extra_count = 1,
    };
    if (ir_create(memory, &native_sizes, &native) == IR_OK)
    {
        native.result_type = IR_I64;
        native.block_offsets[1] = 3;
        native.constants[0].low = 255;
        native.call_types[0] = IR_I8;
        native.calls[0] = (IrCall){ .argument_count = 1, .parameter_count = 1, .result_type = IR_I64 };
        ir_interpreter_test_instruction(&native, 0, IR_CONST, IR_I8, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&native, 1, IR_CALL, IR_I64, 88, 0, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&native, 2, IR_RET, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        const IrInterpreterFunction code = { .symbol = 71, .function = &native };
        const InterpreterNativeBinding binding = { .symbol = 88, .address = (void*)ir_interpreter_test_signed_native };
        IrInterpreterProgram program = { .functions = &code, .function_count = 1, .natives = &binding, .native_count = 1 };
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_NATIVE, (InterpreterValue){ 0 }, "signless narrow IR native argument requires explicit ABI extension");
        const uint8_t extensions[] = { 1 };
        const uint8_t* extension_slices[] = { extensions };
        program.native_extensions = extension_slices;
        ir_interpreter_test_run(tally, &program, 71, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = UINT64_MAX - 2 }, "explicit native signed-byte extension follows prototype");
    }
    else
    {
        ir_interpreter_test_expect(tally, false, "native narrow-argument fixture allocation");
    }
    arena_pop_to(memory, mark);
}

static void ir_interpreter_test_mappings(IrInterpreterTestTally* tally, Arena* memory, Arena* execution)
{
    const uint64_t mark = memory->length;
    IrFunction function = { 0 };
    const IrSizes sizes = {
        .instruction_count = 2,
        .block_count = 1,
        .constant_count = 1,
    };
    const bool created = ir_create(memory, &sizes, &function) == IR_OK;
    ir_interpreter_test_expect(tally, created, "symbol mapping fixture allocation");
    if (created)
    {
        function.result_type = IR_I64;
        function.constants[0].low = 7;
        function.block_offsets[1] = 2;
        ir_interpreter_test_instruction(&function, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_interpreter_test_instruction(&function, 1, IR_RET, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        IrInterpreterFunction code[] = {
            { .symbol = 10, .function = &function },
            { .symbol = 20, .function = &function },
        };
        uint8_t bytes = 0;
        InterpreterBinding data[] = {
            { .symbol = 30, .bytes = &bytes, .size = 1, .writable = true },
            { .symbol = 40, .bytes = &bytes, .size = 1, .writable = true },
        };
        InterpreterNativeBinding native[] = {
            { .symbol = 50, .address = (void*)ir_interpreter_test_signed_native },
            { .symbol = 60, .address = (void*)ir_interpreter_test_signed_native },
        };
        const IrInterpreterProgram program = {
            .functions = code,
            .function_count = 2,
            .bindings = data,
            .binding_count = 2,
            .natives = native,
            .native_count = 2,
        };
        ir_interpreter_test_run(tally, &program, 10, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = 7 }, "distinct symbols may alias function/data/native addresses");
        const SymbolId duplicate_functions[] = { 0, UINT32_MAX, 20 };
        for (uint32_t i = 0; i < sizeof(duplicate_functions) / sizeof(duplicate_functions[0]); i += 1)
        {
            code[0].symbol = duplicate_functions[i];
            ir_interpreter_test_run(tally, &program, 10, 0, 0, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "reserved or duplicate function identity rejected");
        }
        code[0].symbol = 10;
        const SymbolId duplicate_data[] = { 0, UINT32_MAX, 10, 40 };
        for (uint32_t i = 0; i < sizeof(duplicate_data) / sizeof(duplicate_data[0]); i += 1)
        {
            data[0].symbol = duplicate_data[i];
            ir_interpreter_test_run(tally, &program, 10, 0, 0, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "reserved or colliding data identity rejected");
        }
        data[0].symbol = 30;
        const SymbolId duplicate_native[] = { 0, UINT32_MAX, 10, 30, 60 };
        for (uint32_t i = 0; i < sizeof(duplicate_native) / sizeof(duplicate_native[0]); i += 1)
        {
            native[0].symbol = duplicate_native[i];
            ir_interpreter_test_run(tally, &program, 10, 0, 0, 0, execution, INTERPRETER_INVALID, (InterpreterValue){ 0 }, "reserved or colliding native identity rejected");
        }
        native[0].symbol = 50;
        ir_interpreter_test_run(tally, &program, 20, 0, 0, 0, execution, INTERPRETER_OK, (InterpreterValue){ .low = 7 }, "valid alias mapping remains usable after rejected mappings");
        ir_interpreter_test_run(tally, &program, 0, 0, 0, 0, execution, INTERPRETER_SYMBOL, (InterpreterValue){ 0 }, "reserved zero entry never resolves");
        ir_interpreter_test_run(tally, &program, UINT32_MAX, 0, 0, 0, execution, INTERPRETER_SYMBOL, (InterpreterValue){ 0 }, "sentinel entry never resolves");
    }
    arena_pop_to(memory, mark);
}

static bool ir_interpreter_test_roundtrip(const IrFunction* original, Arena* source_memory, Arena* output, Arena* scratch, IrFunction* parsed)
{
    FILE* first = tmpfile();
    FILE* second = tmpfile();
    bool ok = first != 0 && second != 0;
    long bytes = -1;
    if (ok)
    {
        ok = ir_print(original, scratch, first) == IR_OK && fflush(first) == 0;
        bytes = ok ? ftell(first) : -1;
        ok = bytes > 0 && fseek(first, 0, SEEK_SET) == 0;
    }
    if (ok)
    {
        uint8_t* text = arena_push(source_memory, (uint64_t)bytes, 1);
        ok = text != 0 && fread(text, 1, (size_t)bytes, first) == (size_t)bytes;
        if (ok)
        {
            const String8 source = { .pointer = (char*)text, .length = (uint64_t)bytes };
            const IrParseResult read = ir_parse(source, output, scratch, parsed);
            ok = read.error == IR_PARSE_OK && read.consumed == (uint64_t)bytes;
        }
        if (ok)
        {
            ok = ir_print(parsed, scratch, second) == IR_OK && fflush(second) == 0 && fseek(second, 0, SEEK_SET) == 0;
            for (long b = 0; ok && b < bytes; b += 1)
            {
                ok = fgetc(second) == text[b];
            }
            ok = ok && fgetc(second) == EOF && ferror(second) == 0;
        }
    }
    if (first != 0)
    {
        fclose(first);
    }
    if (second != 0)
    {
        fclose(second);
    }
    return ok;
}

static void ir_interpreter_test_generated(IrInterpreterTestTally* tally, uint64_t seed, uint32_t count)
{
    Arena input = arena_reserve(1ull << 30);
    Arena output = arena_reserve(1ull << 30);
    Arena parsed_output = arena_reserve(1ull << 30);
    Arena scratch = arena_reserve(1ull << 30);
    Arena execution = arena_reserve(1ull << 30);
    Tier0Tables tables = { 0 };
    const bool ready = input.base != 0 && output.base != 0 && parsed_output.base != 0 && scratch.base != 0 && execution.base != 0 && tier0_tables_open(&tables);
    ir_interpreter_test_expect(tally, ready, "generated-program arenas and ABI tables");
    for (uint32_t p = 0; ready && p < count; p += 1)
    {
        const uint64_t program_seed = seed + p;
        Tier0Program* generated = BUSTER_ARENA_PUSH_ARRAY(&input, Tier0Program, 1);
        Tier0Functions* sources = BUSTER_ARENA_PUSH_ARRAY(&input, Tier0Functions, 1);
        IrFunction* functions = BUSTER_ARENA_PUSH_ARRAY(&input, IrFunction, BUSTER_TIER0_GENERATE_MAX_FUNCTIONS);
        IrFunction* parsed_functions = BUSTER_ARENA_PUSH_ARRAY(&input, IrFunction, BUSTER_TIER0_GENERATE_MAX_FUNCTIONS);
        IrInterpreterFunction* code = BUSTER_ARENA_PUSH_ARRAY(&input, IrInterpreterFunction, BUSTER_TIER0_GENERATE_MAX_FUNCTIONS);
        IrInterpreterFunction* parsed_code = BUSTER_ARENA_PUSH_ARRAY(&input, IrInterpreterFunction, BUSTER_TIER0_GENERATE_MAX_FUNCTIONS);
        InterpreterBinding* data_bindings = BUSTER_ARENA_PUSH_ARRAY(&input, InterpreterBinding, BUSTER_TIER0_GENERATE_MAX_GLOBALS);
        const uint64_t global_capacity = BUSTER_TIER0_GENERATE_MAX_GLOBALS * BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS * 8 + 64;
        uint8_t* globals = BUSTER_ARENA_PUSH_ARRAY(&input, uint8_t, global_capacity);
        uint8_t* expected_globals = BUSTER_ARENA_PUSH_ARRAY(&input, uint8_t, global_capacity);
        const Tier0ProgramOptions options = {
            .functions = 8,
            .statements = 40,
            .expression_nodes = 30,
            .calls = true,
            .loops = true,
            .floating = false,
            .pointers = true,
            .defined = true,
        };
        bool ok = generated != 0 && sources != 0 && functions != 0 && parsed_functions != 0 && code != 0 && parsed_code != 0 && data_bindings != 0 && globals != 0 && expected_globals != 0;
        ok = ok && tier0_generate_program(generated, &input, program_seed, options, "ir_interpreter_") && tier0_program_functions(generated, &tables, &input, sources);
        ir_interpreter_test_expect(tally, ok, "generated typed workload construction");
        for (uint32_t f = 0; ok && f < sources->count; f += 1)
        {
            const IrType result_type = (IrType)ir_typed_types[generated->functions[f].result_class];
            const IrCheck lowered = ir_lower_typed(&sources->functions[f], result_type, generated->global_count + generated->function_count + 1, &output, &scratch, &functions[f]);
            ok = lowered.error == IR_OK;
            code[f] = (IrInterpreterFunction){ .symbol = generated->global_count + f + 1, .function = &functions[f] };
            ok = ok && ir_interpreter_test_roundtrip(&functions[f], &input, &parsed_output, &scratch, &parsed_functions[f]);
            parsed_code[f] = (IrInterpreterFunction){ .symbol = code[f].symbol, .function = &parsed_functions[f] };
        }
        ir_interpreter_test_expect(tally, ok, "generated IR print/parse/print preserves canonical text");
        if (ok)
        {
            const Tier0TestData layout = tier0_test_layout(generated);
            for (uint32_t g = 0; g < generated->global_count; g += 1)
            {
                const uint64_t size = (uint64_t)generated->global_elements[g] * tier0_class_bytes(generated->global_classes[g]);
                data_bindings[g] = (InterpreterBinding){ .symbol = g + 1, .bytes = globals + layout.offsets[g], .size = size, .writable = true };
            }
            const IrInterpreterProgram program = { .functions = code, .function_count = sources->count, .bindings = data_bindings, .binding_count = generated->global_count };
            const IrInterpreterProgram parsed_program = { .functions = parsed_code, .function_count = sources->count, .bindings = data_bindings, .binding_count = generated->global_count };
            uint64_t inputs[BUSTER_TIER0_TEST_INPUTS][3];
            tier0_test_inputs(program_seed, inputs);
            for (uint32_t r = 0; r < BUSTER_TIER0_TEST_INPUTS; r += 1)
            {
                bool source_fault = false;
                tier0_test_initial(generated, &layout, globals);
                const uint64_t expected = tier0_test_interpret(generated, sources, &layout, globals, inputs[r], &source_fault);
                memcpy(expected_globals, globals, layout.size);
                tier0_test_initial(generated, &layout, globals);
                const InterpreterValue arguments[] = {
                    { .low = inputs[r][0] },
                    { .low = inputs[r][1] },
                    { .low = inputs[r][2] },
                };
                const InterpreterResult result = ir_interpreter_test_run(tally, &program, code[sources->count - 1].symbol, arguments, 3, 0, &execution, INTERPRETER_OK, (InterpreterValue){ .low = expected }, "generated IR agrees with independent typed-stream result");
                ir_interpreter_test_expect(tally, !source_fault && result.status == INTERPRETER_OK && memcmp(expected_globals, globals, layout.size) == 0, "generated IR agrees with independent global-memory effects");
                tier0_test_initial(generated, &layout, globals);
                ir_interpreter_test_run(tally, &parsed_program, code[sources->count - 1].symbol, arguments, 3, 0, &execution, INTERPRETER_OK, (InterpreterValue){ .low = expected }, "parsed generated IR agrees with typed-stream result");
                ir_interpreter_test_expect(tally, memcmp(expected_globals, globals, layout.size) == 0, "parsed generated IR preserves global-memory effects");
                if ((source_fault || result.status != INTERPRETER_OK || result.value.low != expected) && tally->failures < 20)
                {
                    fprintf(stderr, "  generated seed %" PRIu64 ", input %u\n", program_seed, r);
                }
            }
        }
        arena_pop_to(&input, 0);
        arena_pop_to(&output, 0);
        arena_pop_to(&parsed_output, 0);
    }
    tier0_tables_close(&tables);
    arena_release(&input);
    arena_release(&output);
    arena_release(&parsed_output);
    arena_release(&scratch);
    arena_release(&execution);
}

static __attribute__((unused)) int run_ir_interpreter_tests(int argc, char** argv)
{
    uint64_t seed = 0x123456789abcdefull;
    uint32_t count = 8;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool is_seed = strcmp(argv[i], "--seed") == 0;
        if ((is_seed || strcmp(argv[i], "--count") == 0) && i + 1 < argc)
        {
            char* end = 0;
            errno = 0;
            i += 1;
            const uint64_t number = strtoull(argv[i], &end, 0);
            valid = errno == 0 && end != argv[i] && *end == 0 && argv[i][0] != '-' && (is_seed || (number != 0 && number <= 100000));
            if (valid && is_seed)
            {
                seed = number;
            }
            else if (valid)
            {
                count = (uint32_t)number;
            }
        }
        else
        {
            valid = false;
        }
    }
    int status = 1;
    if (valid)
    {
        IrInterpreterTestTally tally = { 0 };
        Arena memory = arena_reserve(BUSTER_IR_INTERPRETER_TEST_BYTES);
        Arena execution = arena_reserve(BUSTER_IR_INTERPRETER_TEST_BYTES);
        ir_interpreter_test_expect(&tally, memory.base != 0 && execution.base != 0, "fixture arenas");
        if (memory.base != 0 && execution.base != 0)
        {
            ir_interpreter_test_numbers(&tally, &memory, &execution);
            ir_interpreter_test_control(&tally, &memory, &execution);
            ir_interpreter_test_dominance(&tally, &memory, &execution);
            ir_interpreter_test_calls(&tally, &memory, &execution);
            ir_interpreter_test_memory(&tally, &memory, &execution);
            ir_interpreter_test_mappings(&tally, &memory, &execution);
            ir_interpreter_test_generated(&tally, seed, count);
        }
        arena_release(&memory);
        arena_release(&execution);
        printf("test-ir-interpreter: %" PRIu64 " checks, %" PRIu64 " failed\n", tally.checks, tally.failures);
        status = tally.failures != 0;
    }
    else
    {
        fprintf(stderr, "usage: test-ir-interpreter [--seed N] [--count N]\n");
    }
    return status;
}
