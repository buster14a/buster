// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2541's general splice fixtures: callees of any control flow (a diamond, several returns, a loop, a switch, calls of
// their own, stack objects with and without lifetime markers, an unreachable path, parameters that don't begin the body,
// stores to memory, and a callee that never returns) expanded into callers that call them from their entry, from a
// loop's latch, twice from a phi's block, before a branch whose successor's phi names the call's block, under a guard,
// and from a switch's targets. Each unit is generated from text templates with random constants, at two integer types,
// and runs on the shared IR interpreter (an oracle independent of the inliner) before inlining, after it, and after SROA
// and SSA promotion of the caller; the inliner's two twins must write the same columns (ir_inline_test_run). A by-value
// callee with two returns, each of its own object's address, checks the aggregate result's phi and the lifetime ends'
// place after its copy. Include after ir_optimize_inline_simd_test.c, whose parse and run helpers it uses.

#include <stdarg.h>

#define BUSTER_IR_INLINE_CFG_TEXT 4096u
#define BUSTER_IR_INLINE_CFG_CALLEES 9u
#define BUSTER_IR_INLINE_CFG_CALLERS 6u
#define BUSTER_IR_INLINE_CFG_TRIALS 24u
#define BUSTER_IR_INLINE_CFG_MEMORY 64u
// The memory binding's symbol, which the store callee writes through.
#define BUSTER_IR_INLINE_CFG_BUFFER 90u

typedef struct IrInlineCfgText IrInlineCfgText;
struct IrInlineCfgText
{
    char text[BUSTER_IR_INLINE_CFG_TEXT];
    uint32_t length;
    const char* type;
};

static void ir_inline_cfg_line(IrInlineCfgText* text, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(text->text + text->length, BUSTER_IR_INLINE_CFG_TEXT - text->length, format, arguments);
    va_end(arguments);
    text->length += written > 0 && (uint32_t)written < BUSTER_IR_INLINE_CFG_TEXT - text->length ? (uint32_t)written : 0;
}

// A constant of the template's type, as the IR's text writes one: 32 hex digits.
static void ir_inline_cfg_constant(char* buffer, uint64_t value)
{
    snprintf(buffer, 40, "0x0000000000000000%016" PRIx64, value);
}

static void ir_inline_cfg_begin(IrInlineCfgText* text, const char* type)
{
    text->length = 0;
    text->type = type;
    ir_inline_cfg_line(text, "function (%s, %s) -> %s {\n", type, type, type);
}

// Callee `shape` of symbol 2 (the file's comment), of two parameters and a result of the text's type; k1 its constant.
static void ir_inline_cfg_callee(IrInlineCfgText* text, uint32_t shape, uint64_t k1)
{
    const char* t = text->type;
    char k[40];
    char zero[40];
    char one[40];
    char two[40];
    char three[40];
    char seven[40];
    ir_inline_cfg_constant(k, k1);
    ir_inline_cfg_constant(zero, 0);
    ir_inline_cfg_constant(one, 1);
    ir_inline_cfg_constant(two, 2);
    ir_inline_cfg_constant(three, 3);
    ir_inline_cfg_constant(seven, 7);
    ir_inline_cfg_begin(text, t);
    if (shape == 5)
    {
        ir_inline_cfg_line(text, "  slot $0 size=8 align=8 type=void flags=0x01\n  slot $1 size=8 align=8 type=void flags=0x01\n");
    }
    if (shape == 7)
    {
        // Its parameters don't begin it: the vector splice leaves it to the scalar twin.
        ir_inline_cfg_line(text, "b0:\n  %%0 = const.%s %s\n  %%1 = param.%s 0\n  %%2 = param.%s 1\n", t, k, t, t);
    }
    else
    {
        ir_inline_cfg_line(text, "b0:\n  %%0 = param.%s 0\n  %%1 = param.%s 1\n", t, t);
    }
    switch (shape)
    {
        case 0:
            // A diamond joined by a phi.
            ir_inline_cfg_line(text, "  %%2 = const.%s %s\n  %%3 = icmp.i1 %%0, %%1 ult\n  %%4 = cbr.void %%3, b1, b2\n", t, k);
            ir_inline_cfg_line(text, "b1:\n  %%5 = add.%s %%0, %%2\n  %%6 = br.void b3\n", t);
            ir_inline_cfg_line(text, "b2:\n  %%7 = xor.%s %%1, %%2\n  %%8 = br.void b3\n", t);
            ir_inline_cfg_line(text, "b3:\n  %%9 = phi.%s [%%5, b1], [%%7, b2]\n  %%10 = ret.void %%9\n", t);
            break;
        case 1:
            // Two returns.
            ir_inline_cfg_line(text, "  %%2 = const.%s %s\n  %%3 = icmp.i1 %%0, %%1 ult\n  %%4 = cbr.void %%3, b1, b2\n", t, k);
            ir_inline_cfg_line(text, "b1:\n  %%5 = sub.%s %%1, %%0\n  %%6 = ret.void %%5\n", t);
            ir_inline_cfg_line(text, "b2:\n  %%7 = mul.%s %%0, %%2\n  %%8 = ret.void %%7\n", t);
            break;
        case 2:
            // A loop of p0 & 7 trips whose header's phis name its latch's later values.
            ir_inline_cfg_line(text, "  %%2 = const.%s %s\n  %%3 = const.%s %s\n  %%4 = const.%s %s\n  %%5 = and.%s %%0, %%4\n  %%6 = br.void b1\n", t, zero, t, one, t, seven, t);
            ir_inline_cfg_line(text, "b1:\n  %%7 = phi.%s [%%2, b0], [%%12, b2]\n  %%8 = phi.%s [%%1, b0], [%%11, b2]\n  %%9 = icmp.i1 %%7, %%5 ult\n  %%10 = cbr.void %%9, b2, b3\n", t, t);
            ir_inline_cfg_line(text, "b2:\n  %%11 = xor.%s %%8, %%7\n  %%12 = add.%s %%7, %%3\n  %%13 = br.void b1\n", t, t);
            ir_inline_cfg_line(text, "b3:\n  %%14 = ret.void %%8\n");
            break;
        case 3:
            // A switch whose targets and default each return.
            ir_inline_cfg_line(text, "  %%2 = const.%s %s\n  %%3 = and.%s %%0, %%2\n  %%4 = switch.void %%3, default b4, [%s, b1], [%s, b2], [%s, b3]\n", t, three, t, zero, one, two);
            ir_inline_cfg_line(text, "b1:\n  %%5 = ret.void %%1\n");
            ir_inline_cfg_line(text, "b2:\n  %%6 = const.%s %s\n  %%7 = add.%s %%1, %%6\n  %%8 = ret.void %%7\n", t, k, t);
            ir_inline_cfg_line(text, "b3:\n  %%9 = xor.%s %%0, %%1\n  %%10 = ret.void %%9\n", t);
            ir_inline_cfg_line(text, "b4:\n  %%11 = sub.%s %%0, %%1\n  %%12 = ret.void %%11\n", t);
            break;
        case 4:
            // Calls of its own: an external function's, which stays a call, and a leaf's, expanded first.
            ir_inline_cfg_line(text, "  %%2 = icmp.i1 %%0, %%1 ult\n  %%3 = cbr.void %%2, b1, b2\n");
            ir_inline_cfg_line(text, "b1:\n  %%4 = call.%s @5(%%0, %%1) fixed=(%s, %s) variadic=0\n  %%5 = br.void b3\n", t, t, t);
            ir_inline_cfg_line(text, "b2:\n  %%6 = call.%s @3(%%1, %%0) fixed=(%s, %s) variadic=0\n  %%7 = br.void b3\n", t, t, t);
            ir_inline_cfg_line(text, "b3:\n  %%8 = phi.%s [%%4, b1], [%%6, b2]\n  %%9 = ret.void %%8\n", t);
            break;
        case 5:
            // Stack objects: $0 without lifetime markers, which the expansion brackets, and $1 with its own.
            ir_inline_cfg_line(text, "  %%2 = stack_addr.ptr $0\n  %%3 = store.void %%2, %%0 align=8\n  %%4 = icmp.i1 %%0, %%1 ult\n  %%5 = cbr.void %%4, b1, b2\n");
            ir_inline_cfg_line(text, "b1:\n  %%6 = load.%s %%2 align=8\n  %%7 = add.%s %%6, %%1\n  %%8 = store.void %%2, %%7 align=8\n  %%9 = br.void b2\n", t, t);
            ir_inline_cfg_line(text, "b2:\n  %%10 = lifetime.void $1 start\n  %%11 = stack_addr.ptr $1\n  %%12 = store.void %%11, %%1 align=8\n  %%13 = load.%s %%2 align=8\n", t);
            ir_inline_cfg_line(text, "  %%14 = load.%s %%11 align=8\n  %%15 = xor.%s %%13, %%14\n  %%16 = lifetime.void $1 end\n  %%17 = ret.void %%15\n", t, t);
            break;
        case 6:
            // A path that never runs ends in unreachable.
            ir_inline_cfg_line(text, "  %%2 = const.%s %s\n  %%3 = icmp.i1 %%0, %%2 ult\n  %%4 = cbr.void %%3, b1, b2\n", t, zero);
            ir_inline_cfg_line(text, "b1:\n  %%5 = unreachable.void\n");
            ir_inline_cfg_line(text, "b2:\n  %%6 = add.%s %%0, %%1\n  %%7 = ret.void %%6\n", t);
            break;
        case 7:
            ir_inline_cfg_line(text, "  %%3 = icmp.i1 %%1, %%2 ult\n  %%4 = cbr.void %%3, b1, b2\n");
            ir_inline_cfg_line(text, "b1:\n  %%5 = add.%s %%1, %%0\n  %%6 = ret.void %%5\n", t);
            ir_inline_cfg_line(text, "b2:\n  %%7 = sub.%s %%2, %%0\n  %%8 = ret.void %%7\n", t);
            break;
        default:
            // Stores to memory, at an address row each way, which the comparison reads.
            ir_inline_cfg_line(text, "  %%2 = symbol_addr.ptr @%u\n  %%3 = const.%s %s\n  %%4 = and.%s %%0, %%3\n  %%5 = icmp.i1 %%4, %%3 eq\n  %%6 = cbr.void %%5, b1, b2\n",
                               BUSTER_IR_INLINE_CFG_BUFFER, t, one, t);
            ir_inline_cfg_line(text, "b1:\n  %%7 = address.ptr %%2 offset=8\n  %%8 = store.void %%7, %%1 align=1\n  %%9 = ret.void %%0\n");
            ir_inline_cfg_line(text, "b2:\n  %%10 = address.ptr %%2 offset=24\n  %%11 = store.void %%10, %%0 align=1\n  %%12 = ret.void %%1\n");
            break;
    }
    ir_inline_cfg_line(text, "}\n");
}

// Caller `shape` of symbol 1 (the file's comment), which calls symbol 2 and, under a guard no argument meets, symbol 4.
static void ir_inline_cfg_caller(IrInlineCfgText* text, uint32_t shape, uint64_t guard)
{
    const char* t = text->type;
    char zero[40];
    char one[40];
    char three[40];
    char never[40];
    ir_inline_cfg_constant(zero, 0);
    ir_inline_cfg_constant(one, 1);
    ir_inline_cfg_constant(three, 3);
    ir_inline_cfg_constant(never, guard);
    ir_inline_cfg_begin(text, t);
    ir_inline_cfg_line(text, "b0:\n  %%0 = param.%s 0\n  %%1 = param.%s 1\n", t, t);
    switch (shape)
    {
        case 0:
            ir_inline_cfg_line(text, "  %%2 = call.%s @2(%%0, %%1) fixed=(%s, %s) variadic=0\n  %%3 = add.%s %%2, %%0\n  %%4 = ret.void %%3\n", t, t, t, t);
            break;
        case 1:
            // From a loop's latch, a phi's predecessor, with the header's phis as its arguments.
            ir_inline_cfg_line(text, "  %%2 = const.%s %s\n  %%3 = const.%s %s\n  %%4 = const.%s %s\n  %%5 = br.void b1\n", t, zero, t, one, t, three);
            ir_inline_cfg_line(text, "b1:\n  %%6 = phi.%s [%%2, b0], [%%12, b2]\n  %%7 = phi.%s [%%1, b0], [%%11, b2]\n  %%8 = icmp.i1 %%6, %%4 ult\n  %%9 = cbr.void %%8, b2, b3\n", t, t);
            ir_inline_cfg_line(text, "b2:\n  %%10 = call.%s @2(%%7, %%0) fixed=(%s, %s) variadic=0\n  %%11 = xor.%s %%7, %%10\n  %%12 = add.%s %%6, %%3\n  %%13 = br.void b1\n", t, t, t, t, t);
            ir_inline_cfg_line(text, "b3:\n  %%14 = ret.void %%7\n");
            break;
        case 2:
            // Twice from a phi's block, the second call taking the first's result.
            ir_inline_cfg_line(text, "  %%2 = const.%s %s\n  %%3 = icmp.i1 %%0, %%1 ult\n  %%4 = cbr.void %%3, b1, b2\n", t, one);
            ir_inline_cfg_line(text, "b1:\n  %%5 = add.%s %%0, %%2\n  %%6 = br.void b3\n", t);
            ir_inline_cfg_line(text, "b2:\n  %%7 = sub.%s %%1, %%2\n  %%8 = br.void b3\n", t);
            ir_inline_cfg_line(text, "b3:\n  %%9 = phi.%s [%%5, b1], [%%7, b2]\n  %%10 = call.%s @2(%%9, %%1) fixed=(%s, %s) variadic=0\n", t, t, t, t);
            ir_inline_cfg_line(text, "  %%11 = call.%s @2(%%10, %%9) fixed=(%s, %s) variadic=0\n  %%12 = ret.void %%11\n", t, t, t);
            break;
        case 3:
            // Before a branch: the successor's phi names the call's block, which the expansion splits.
            ir_inline_cfg_line(text, "  %%2 = call.%s @2(%%0, %%1) fixed=(%s, %s) variadic=0\n  %%3 = icmp.i1 %%2, %%0 eq\n  %%4 = cbr.void %%3, b1, b2\n", t, t, t);
            ir_inline_cfg_line(text, "b1:\n  %%5 = br.void b2\n");
            ir_inline_cfg_line(text, "b2:\n  %%6 = phi.%s [%%2, b0], [%%1, b1]\n  %%7 = ret.void %%6\n", t);
            break;
        case 4:
            // A callee that never returns, under a guard: its continuation is unreachable.
            ir_inline_cfg_line(text, "  %%2 = const.%s %s\n  %%3 = icmp.i1 %%0, %%2 eq\n  %%4 = cbr.void %%3, b1, b2\n", t, never);
            ir_inline_cfg_line(text, "b1:\n  %%5 = call.%s @4(%%0, %%1) fixed=(%s, %s) variadic=0\n  %%6 = br.void b2\n", t, t, t);
            ir_inline_cfg_line(text, "b2:\n  %%7 = phi.%s [%%1, b0], [%%5, b1]\n  %%8 = call.%s @2(%%7, %%0) fixed=(%s, %s) variadic=0\n  %%9 = ret.void %%8\n", t, t, t, t);
            break;
        default:
            // From a switch's targets.
            ir_inline_cfg_line(text, "  %%2 = const.%s %s\n  %%3 = and.%s %%1, %%2\n  %%4 = switch.void %%3, default b3, [%s, b1], [%s, b2]\n", t, three, t, zero, one);
            ir_inline_cfg_line(text, "b1:\n  %%5 = call.%s @2(%%0, %%1) fixed=(%s, %s) variadic=0\n  %%6 = br.void b3\n", t, t, t);
            ir_inline_cfg_line(text, "b2:\n  %%7 = call.%s @2(%%1, %%0) fixed=(%s, %s) variadic=0\n  %%8 = br.void b3\n", t, t, t);
            ir_inline_cfg_line(text, "b3:\n  %%9 = phi.%s [%%0, b0], [%%5, b1], [%%7, b2]\n  %%10 = ret.void %%9\n", t);
            break;
    }
    ir_inline_cfg_line(text, "}\n");
}

// The unit's other functions: a straight-line leaf (symbol 3), a callee that never returns (4) and an external function
// (5), which stays a call.
static void ir_inline_cfg_helper(IrInlineCfgText* text, uint32_t symbol, uint64_t k2)
{
    const char* t = text->type;
    char k[40];
    ir_inline_cfg_constant(k, k2);
    ir_inline_cfg_begin(text, t);
    ir_inline_cfg_line(text, "b0:\n  %%0 = param.%s 0\n  %%1 = param.%s 1\n", t, t);
    if (symbol == 3)
    {
        ir_inline_cfg_line(text, "  %%2 = const.%s %s\n  %%3 = add.%s %%0, %%2\n  %%4 = xor.%s %%3, %%1\n  %%5 = ret.void %%4\n", t, k, t, t);
    }
    else if (symbol == 4)
    {
        ir_inline_cfg_line(text, "  %%2 = br.void b1\nb1:\n  %%3 = br.void b1\n");
    }
    else
    {
        ir_inline_cfg_line(text, "  %%2 = mul.%s %%0, %%1\n  %%3 = ret.void %%2\n", t);
    }
    ir_inline_cfg_line(text, "}\n");
}

static bool ir_inline_cfg_parse(IrInlineTest* test, const IrInlineCfgText* text, Arena* arena, Arena* scratch, IrFunction* function)
{
    const bool parsed = ir_inline_simd_parse(test, text->text, arena, scratch, function);
    if (!parsed)
    {
        fprintf(stderr, "%s", text->text);
    }
    return parsed;
}

typedef struct IrInlineCfgRun IrInlineCfgRun;
struct IrInlineCfgRun
{
    InterpreterResult result;
    uint8_t memory[BUSTER_IR_INLINE_CFG_MEMORY];
};

// Runs symbol 1 of the unit's five functions, `functions`, on two arguments and a memory buffer whose bytes start as `seed`'s.
static IrInlineCfgRun ir_inline_cfg_run(const IrFunction* functions, const InterpreterValue* arguments, const uint8_t* seed, Arena* scratch)
{
    IrInlineCfgRun run = { 0 };
    memcpy(run.memory, seed, sizeof(run.memory));
    IrInterpreterFunction bound[5];
    for (uint32_t f = 0; f < 5; f += 1)
    {
        bound[f] = (IrInterpreterFunction){
            .symbol = f + 1,
            .function = &functions[f],
        };
    }
    const InterpreterBinding buffer = {
        .symbol = BUSTER_IR_INLINE_CFG_BUFFER,
        .bytes = run.memory,
        .size = sizeof(run.memory),
        .writable = true,
    };
    const IrInterpreterProgram program = {
        .functions = bound,
        .function_count = 5,
        .bindings = &buffer,
        .binding_count = 1,
    };
    // The units run a few hundred steps; a bound of 2^20 makes a wrong branch that loops fail at once rather than run the
    // interpreter's default 2 x 10^8 steps.
    InterpreterOptions options = interpreter_default_options();
    options.max_steps = UINT64_C(1) << 20;
    const uint64_t mark = scratch->length;
    run.result = ir_interpret(&program, 1, arguments, 2, &options, scratch);
    arena_pop_to(scratch, mark);
    return run;
}

static bool ir_inline_cfg_same(const IrInlineCfgRun* a, const IrInlineCfgRun* b, uint64_t mask)
{
    return a->result.status == b->result.status && (a->result.value.low & mask) == (b->result.value.low & mask) && memcmp(a->memory, b->memory, sizeof(a->memory)) == 0;
}

// Every callee shape into every caller shape at i32 and i64: decisions, the four runs' agreement, and promotion.
static void ir_inline_test_general_units(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    static const char* const types[2] = {
        "i32",
        "i64",
    };
    IrInlineCfgText text;
    for (uint32_t width = 0; width < 2; width += 1)
    {
        const uint64_t mask = width == 0 ? UINT32_MAX : UINT64_MAX;
        for (uint32_t callee = 0; callee < BUSTER_IR_INLINE_CFG_CALLEES; callee += 1)
        {
            for (uint32_t caller = 0; caller < BUSTER_IR_INLINE_CFG_CALLERS; caller += 1)
            {
                IrFunction functions[5];
                bool parsed = true;
                const uint64_t k1 = ir_inline_test_random(test) & 0xffff;
                const uint64_t k2 = ir_inline_test_random(test) & 0xffff;
                const uint64_t guard = (mask & UINT64_C(0x7ffffff1)) | 0x10000;
                text.type = types[width];
                ir_inline_cfg_caller(&text, caller, guard);
                parsed = parsed && ir_inline_cfg_parse(test, &text, input, scratch, &functions[0]);
                ir_inline_cfg_callee(&text, callee, k1);
                parsed = parsed && ir_inline_cfg_parse(test, &text, input, scratch, &functions[1]);
                for (uint32_t symbol = 3; parsed && symbol <= 5; symbol += 1)
                {
                    text.type = types[width];
                    ir_inline_cfg_helper(&text, symbol, k2);
                    parsed = ir_inline_cfg_parse(test, &text, input, scratch, &functions[symbol - 1]);
                }
                if (parsed)
                {
                    const uint32_t symbols[5] = {
                        1,
                        2,
                        3,
                        4,
                        5,
                    };
                    const uint8_t flags[5] = {
                        0,
                        BUSTER_IR_INLINE_INTERNAL,
                        BUSTER_IR_INLINE_INTERNAL,
                        BUSTER_IR_INLINE_INTERNAL,
                        0,
                    };
                    const IrInlineUnit unit = {
                        .functions = functions,
                        .symbols = symbols,
                        .flags = flags,
                        .count = 5,
                        .symbol_count = 128,
                    };
                    // Without the option a body with control flow keeps its calls (IR_INLINE_BODY); the leaf, one
                    // straight line, is taken either way.
                    IrInlineResult kept = { 0 };
                    if (ir_inline_test_run(test, &unit, ir_inline_options(), output, twin, scratch, &kept))
                    {
                        bool body = true;
                        for (uint32_t call = kept.call_offsets[0]; call < kept.call_offsets[1]; call += 1)
                        {
                            body = body && kept.decisions[call] == IR_INLINE_BODY;
                        }
                        ir_inline_test_expect(test, body, "without control_flow a callee with blocks keeps its calls");
                    }
                    arena_pop_to(output, 0);
                    arena_pop_to(twin, 0);
                    // As c_check_tier1_inline.c offers an always_inline callee: whatever its cost.
                    IrInlineOptions options = ir_inline_options();
                    options.maximum_cost = UINT32_MAX - 1;
                    options.control_flow = true;
                    IrInlineResult result = { 0 };
                    if (ir_inline_test_run(test, &unit, options, output, twin, scratch, &result))
                    {
                        bool selected = result.call_offsets[1] > result.call_offsets[0];
                        for (uint32_t call = result.call_offsets[0]; call < result.call_offsets[1]; call += 1)
                        {
                            selected = selected && result.decisions[call] == IR_INLINE_SELECTED;
                        }
                        uint32_t kept_calls = 0;
                        for (uint32_t node = 0; node < result.functions[0].instruction_count; node += 1)
                        {
                            kept_calls += result.functions[0].op[node] == IR_CALL;
                        }
                        // The callee's call of the external function stays a call wherever it is expanded.
                        ir_inline_test_expect(test, selected && (callee == 4 ? kept_calls != 0 : kept_calls == 0), "every call of a callee with control flow is expanded");
                        ir_inline_test_expect(test, result.functions[0].block_count > functions[0].block_count, "a general expansion splits its call's block");
                        // The caller's SROA and promotion, as the Tier 1 pipeline's next steps.
                        IrSroaOptions sroa = ir_sroa_options();
                        sroa.flagged = true;
                        IrSroaResult split = { 0 };
                        const IrCheck checked = ir_optimize_sroa(&result.functions[0], output, scratch, sroa, &split);
                        IrFunction promoted = { 0 };
                        const IrCheck promotion = checked.error == IR_OK ? ssa_ir_promote(&split.function, twin, scratch, &promoted) : checked;
                        ir_inline_test_expect(test, checked.error == IR_OK && promotion.error == IR_OK, "SROA and promotion take the expanded caller");
                        IrFunction after[5];
                        IrFunction last[5];
                        memcpy(after, result.functions, sizeof(after));
                        memcpy(last, result.functions, sizeof(last));
                        last[0] = promotion.error == IR_OK ? promoted : result.functions[0];
                        for (uint32_t trial = 0; trial < BUSTER_IR_INLINE_CFG_TRIALS; trial += 1)
                        {
                            InterpreterValue arguments[2] = { 0 };
                            arguments[0].low = (trial < 4 ? trial : ir_inline_test_random(test)) & mask;
                            arguments[1].low = (trial < 4 ? 3 - trial : ir_inline_test_random(test)) & mask;
                            uint8_t seed[BUSTER_IR_INLINE_CFG_MEMORY];
                            for (uint32_t b = 0; b < sizeof(seed); b += 1)
                            {
                                seed[b] = (uint8_t)ir_inline_test_random(test);
                            }
                            const IrInlineCfgRun before = ir_inline_cfg_run(functions, arguments, seed, scratch);
                            const IrInlineCfgRun inlined = ir_inline_cfg_run(after, arguments, seed, scratch);
                            const IrInlineCfgRun final = ir_inline_cfg_run(last, arguments, seed, scratch);
                            ir_inline_test_expect(test, before.result.status == INTERPRETER_OK, "the generated unit runs");
                            ir_inline_test_expect(test, ir_inline_cfg_same(&before, &inlined, mask), "before and after the general splice");
                            ir_inline_test_expect(test, ir_inline_cfg_same(&before, &final, mask), "before inlining and after SROA and promotion");
                            if (!ir_inline_cfg_same(&before, &inlined, mask) || !ir_inline_cfg_same(&before, &final, mask))
                            {
                                fprintf(stderr, "ir-inline: general unit callee=%u caller=%u type=%s trial=%u: status %u/%u/%u value %" PRIx64 "/%" PRIx64 "/%" PRIx64 "\n", callee, caller,
                                        types[width], trial, before.result.status, inlined.result.status, final.result.status, before.result.value.low, inlined.result.value.low,
                                        final.result.value.low);
                            }
                            test->executions += 3;
                        }
                    }
                    else
                    {
                        fprintf(stderr, "ir-inline: general unit callee=%u caller=%u type=%s failed\n", callee, caller, types[width]);
                    }
                }
                arena_pop_to(input, 0);
                arena_pop_to(output, 0);
                arena_pop_to(twin, 0);
            }
        }
    }
}

// A by-value callee with control flow (#2541 with #1282's records): two vectors' bytes in, and by their first lane's low
// bit the sum's or the xor's bytes out, each from an object of its own whose address its return gives. The expansion
// joins the two addresses in a phi, copies the result's bytes from it, and only then ends the objects' lifetimes.
static void ir_inline_test_general_by_value(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    static const char caller_text[] =
        "function (ptr) -> void {\n"
        "  passed #0 size=64 loc(1,32,255,0,0)\n"
        "  passed #1 size=64 loc(1,33,255,0,0)\n"
        "  abi #0 call(0,4,0,2,ret(1,0,64,32,255,255,255)) passed=#0 result=64\n"
        "  slot $0 size=64 align=16 type=void flags=0x01\n"
        "  slot $1 size=64 align=16 type=void flags=0x01\n"
        "  slot $2 size=64 align=16 type=void flags=0x01\n"
        "b0:\n"
        "  %0 = stack_addr.ptr $0\n"
        "  %1 = stack_addr.ptr $1\n"
        "  %2 = stack_addr.ptr $2\n"
        "  %3 = param.ptr 0\n"
        "  %4 = load.v16i32 %3 align=1\n"
        "  %5 = store.void %0, %4 align=16\n"
        "  %6 = address.ptr %3 offset=64\n"
        "  %7 = load.v16i32 %6 align=1\n"
        "  %8 = store.void %1, %7 align=16\n"
        "  %9 = call.void @2(%0, %1) fixed=(ptr, ptr) variadic=0 abi=#0 into=%2\n"
        "  %10 = load.v16i32 %2 align=16\n"
        "  %11 = address.ptr %3 offset=128\n"
        "  %12 = store.void %11, %10 align=1\n"
        "  %13 = ret.void\n"
        "}\n";
    static const char callee_text[] =
        "function (ptr, ptr) -> ptr {\n"
        "  result size=64 ret(1,0,64,32,255,255,255) passed=#0\n"
        "  passed #0 size=64 loc(1,32,255,0,0)\n"
        "  passed #1 size=64 loc(1,33,255,0,0)\n"
        "  slot $0 size=64 align=64 type=void flags=0x01\n"
        "  slot $1 size=64 align=64 type=void flags=0x01\n"
        "b0:\n"
        "  %0 = param.ptr 0\n"
        "  %1 = param.ptr 1\n"
        "  %2 = load.v16i32 %0 align=1\n"
        "  %3 = load.v16i32 %1 align=1\n"
        "  %4 = load.i32 %0 align=1\n"
        "  %5 = const.i32 0x00000000000000000000000000000001\n"
        "  %6 = and.i32 %4, %5\n"
        "  %7 = icmp.i1 %6, %5 eq\n"
        "  %8 = cbr.void %7, b1, b2\n"
        "b1:\n"
        "  %9 = add.v16i32 %2, %3\n"
        "  %10 = stack_addr.ptr $0\n"
        "  %11 = store.void %10, %9 align=64\n"
        "  %12 = ret.void %10\n"
        "b2:\n"
        "  %13 = xor.v16i32 %2, %3\n"
        "  %14 = stack_addr.ptr $1\n"
        "  %15 = store.void %14, %13 align=64\n"
        "  %16 = ret.void %14\n"
        "}\n";
    IrFunction functions[2];
    if (ir_inline_simd_parse(test, caller_text, input, scratch, &functions[0]) && ir_inline_simd_parse(test, callee_text, input, scratch, &functions[1]))
    {
        const uint32_t symbols[] = {
            1,
            2,
        };
        const uint8_t flags[] = {
            0,
            BUSTER_IR_INLINE_INTERNAL,
        };
        const IrInlineUnit unit = {
            .functions = functions,
            .symbols = symbols,
            .flags = flags,
            .count = 2,
            .symbol_count = 128,
        };
        for (uint32_t trial = 0; trial < 8; trial += 1)
        {
            uint8_t seed[128];
            for (uint32_t i = 0; i < sizeof(seed); i += 1)
            {
                seed[i] = (uint8_t)ir_inline_test_random(test);
            }
            seed[0] = (uint8_t)((seed[0] & ~1u) | (trial & 1));
            uint8_t expected[192];
            memcpy(expected, seed, 128);
            for (uint32_t lane = 0; lane < 16; lane += 1)
            {
                uint32_t a = 0;
                uint32_t b = 0;
                memcpy(&a, seed + lane * 4, 4);
                memcpy(&b, seed + 64 + lane * 4, 4);
                const uint32_t out = (trial & 1) != 0 ? a + b : a ^ b;
                memcpy(expected + 128 + lane * 4, &out, 4);
            }
            uint8_t bytes[192];
            ir_inline_test_expect(test, ir_inline_simd_run(functions, 2, seed, bytes, scratch) && memcmp(bytes, expected, sizeof(bytes)) == 0, "the general by-value unit runs as its lanes say");
            for (uint32_t copies = 0; copies < 2; copies += 1)
            {
                IrInlineOptions options = ir_inline_options();
                options.maximum_cost = UINT32_MAX - 1;
                options.vector_copies = copies == 0 ? 7 : 0;
                options.control_flow = true;
                IrInlineResult result = { 0 };
                if (ir_inline_test_run(test, &unit, options, output, twin, scratch, &result))
                {
                    const IrFunction* out = &result.functions[0];
                    // Two blocks of the callee's three past the caller's one, its two slots and the arguments' two,
                    // each bracketed: four starts and four ends.
                    ir_inline_test_expect(test, result.inlined_calls == 1 && ir_inline_simd_count(out, IR_CALL) == 0 && out->block_count == 1 + 3 + 1 && out->slot_count == 3 + 2 + 2 &&
                                              ir_inline_simd_count(out, IR_LIFETIME) == 8 && ir_inline_simd_count(out, IR_PHI) == 1,
                                          "a by-value callee with two returns is spliced, its result's address a phi");
                    // The interpreter gives each slot bytes of its own, so it can't see a lifetime that ends too soon: the
                    // continuation's lifetime ends must follow the loads that copy the result out of the callee's objects,
                    // through the result's address (the phi that begins the continuation) or an address row off it.
                    uint32_t continuation = BUSTER_IR_NONE;
                    for (uint32_t block = 0; block < out->block_count; block += 1)
                    {
                        continuation = out->op[out->block_offsets[block]] == IR_PHI ? block : continuation;
                    }
                    const uint32_t phi = continuation != BUSTER_IR_NONE ? out->block_offsets[continuation] : BUSTER_IR_NONE;
                    uint32_t last_load = 0;
                    uint32_t first_end = UINT32_MAX;
                    for (uint32_t node = phi; phi != BUSTER_IR_NONE && node < out->block_offsets[continuation + 1]; node += 1)
                    {
                        const uint32_t address = out->op[node] == IR_LOAD ? out->a[node] : BUSTER_IR_NONE;
                        const bool copied = address == phi || (address != BUSTER_IR_NONE && out->op[address] == IR_ADDRESS && out->a[address] == phi);
                        last_load = copied ? node : last_load;
                        first_end = out->op[node] == IR_LIFETIME && out->b[node] == 1 && first_end == UINT32_MAX ? node : first_end;
                    }
                    ir_inline_test_expect(test, continuation != BUSTER_IR_NONE && last_load != 0 && first_end != UINT32_MAX && last_load < first_end,
                                          "the callee's objects' lifetimes end after the result's copy");
                    IrFunction inlined = *out;
                    ir_inline_test_expect(test, ir_inline_simd_run(&inlined, 1, seed, bytes, scratch) && memcmp(bytes, expected, sizeof(bytes)) == 0, "the spliced by-value caller runs as the unit");
                    IrSroaOptions sroa = ir_sroa_options();
                    sroa.simd = true;
                    sroa.flagged = true;
                    IrSroaResult split = { 0 };
                    const uint64_t mark = output->length;
                    const uint64_t twin_mark = twin->length;
                    const IrCheck checked = ir_optimize_sroa(&inlined, output, scratch, sroa, &split);
                    IrFunction promoted = { 0 };
                    const IrCheck promotion = checked.error == IR_OK ? ssa_ir_promote(&split.function, twin, scratch, &promoted) : checked;
                    ir_inline_test_expect(test, promotion.error == IR_OK && ir_inline_simd_run(&promoted, 1, seed, bytes, scratch) && memcmp(bytes, expected, sizeof(bytes)) == 0,
                                          "the spliced by-value caller runs as the unit after SROA and promotion");
                    test->executions += 3;
                    arena_pop_to(twin, twin_mark);
                    arena_pop_to(output, mark);
                }
                arena_pop_to(output, 0);
                arena_pop_to(twin, 0);
            }
        }
    }
    arena_pop_to(input, 0);
}

// #2541 step 1's unit fields: a cost limit per callee (IrInlineUnit.limits), so that one callee of the same cost is
// expanded and another kept, and the caller alone rewritten (IrInlineUnit.only), its callees taken as they are, finished
// summaries: neither verified nor rewritten, their own calls kept even where their callee is in the unit.
static void ir_inline_test_general_limits(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    IrInlineCfgText text;
    IrFunction functions[5];
    bool parsed = true;
    text.type = "i64";
    ir_inline_cfg_begin(&text, "i64");
    ir_inline_cfg_line(&text, "b0:\n  %%0 = param.i64 0\n  %%1 = param.i64 1\n  %%2 = call.i64 @2(%%0, %%1) fixed=(i64, i64) variadic=0\n");
    ir_inline_cfg_line(&text, "  %%3 = call.i64 @3(%%1, %%0) fixed=(i64, i64) variadic=0\n  %%4 = call.i64 @4(%%0, %%0) fixed=(i64, i64) variadic=0\n");
    ir_inline_cfg_line(&text, "  %%5 = add.i64 %%2, %%3\n  %%6 = xor.i64 %%5, %%4\n  %%7 = ret.void %%6\n}\n");
    parsed = parsed && ir_inline_cfg_parse(test, &text, input, scratch, &functions[0]);
    // Two callees of one shape (a diamond), and a third that calls the second.
    ir_inline_cfg_callee(&text, 0, 7);
    parsed = parsed && ir_inline_cfg_parse(test, &text, input, scratch, &functions[1]);
    ir_inline_cfg_callee(&text, 0, 9);
    parsed = parsed && ir_inline_cfg_parse(test, &text, input, scratch, &functions[2]);
    ir_inline_cfg_begin(&text, "i64");
    ir_inline_cfg_line(&text, "b0:\n  %%0 = param.i64 0\n  %%1 = param.i64 1\n  %%2 = call.i64 @3(%%0, %%1) fixed=(i64, i64) variadic=0\n  %%3 = ret.void %%2\n}\n");
    parsed = parsed && ir_inline_cfg_parse(test, &text, input, scratch, &functions[3]);
    text.type = "i64";
    ir_inline_cfg_helper(&text, 5, 0);
    parsed = parsed && ir_inline_cfg_parse(test, &text, input, scratch, &functions[4]);
    if (parsed)
    {
        const uint32_t symbols[5] = {
            1,
            2,
            3,
            4,
            5,
        };
        const uint8_t flags[5] = {
            0,
            BUSTER_IR_INLINE_INTERNAL,
            BUSTER_IR_INLINE_INTERNAL,
            BUSTER_IR_INLINE_INTERNAL,
            0,
        };
        const uint32_t limits[5] = {
            0,
            UINT32_MAX - 1,
            1,
            UINT32_MAX - 1,
            0,
        };
        const IrInlineUnit unit = {
            .functions = functions,
            .symbols = symbols,
            .flags = flags,
            .count = 5,
            .symbol_count = 128,
            .limits = limits,
            .only = 1,
        };
        IrInlineOptions options = ir_inline_options();
        options.control_flow = true;
        IrInlineResult result = { 0 };
        if (ir_inline_test_run(test, &unit, options, output, twin, scratch, &result))
        {
            const uint32_t first = result.call_offsets[0];
            ir_inline_test_expect(test, result.call_offsets[1] - first == 3 && result.decisions[first] == IR_INLINE_SELECTED && result.decisions[first + 1] == IR_INLINE_COST &&
                                      result.decisions[first + 2] == IR_INLINE_SELECTED && result.inlined_calls == 2,
                                  "each callee's own cost limit decides its calls");
            // The third callee's call of the second stays a call in it, and the caller's copy of it keeps that call.
            uint32_t kept = 0;
            for (uint32_t node = 0; node < result.functions[0].instruction_count; node += 1)
            {
                kept += result.functions[0].op[node] == IR_CALL;
            }
            ir_inline_test_expect(test, result.functions[3].op == functions[3].op && result.functions[1].op == functions[1].op && result.decisions[result.call_offsets[3]] == IR_INLINE_DISABLED &&
                                      kept == 2,
                                  "the callees are taken as they are, their calls kept");
            IrInlineCfgRun runs[2];
            for (uint32_t trial = 0; trial < 16; trial += 1)
            {
                InterpreterValue arguments[2] = { 0 };
                arguments[0].low = ir_inline_test_random(test);
                arguments[1].low = ir_inline_test_random(test);
                uint8_t seed[BUSTER_IR_INLINE_CFG_MEMORY] = { 0 };
                runs[0] = ir_inline_cfg_run(functions, arguments, seed, scratch);
                runs[1] = ir_inline_cfg_run(result.functions, arguments, seed, scratch);
                ir_inline_test_expect(test, runs[0].result.status == INTERPRETER_OK && ir_inline_cfg_same(&runs[0], &runs[1], UINT64_MAX), "the caller alone rewritten runs as the unit");
                test->executions += 2;
            }
        }
        // A limit column inside the output arena's free tail is refused, as any borrowed input there is.
        IrInlineUnit tail = unit;
        tail.limits = (const uint32_t*)(output->base + output->length);
        IrInlineResult refused = { 0 };
        uint32_t failed = 0;
        ir_inline_test_expect(test, ir_optimize_inline(&tail, output, scratch, options, &refused, &failed).error == IR_ERROR_STRUCTURE, "a limits column in the output's tail is refused");
        IrInlineUnit beyond = unit;
        beyond.only = 6;
        ir_inline_test_expect(test, ir_optimize_inline(&beyond, output, scratch, options, &refused, &failed).error == IR_ERROR_STRUCTURE, "an only function past the unit is refused");
    }
    arena_pop_to(input, 0);
    arena_pop_to(output, 0);
    arena_pop_to(twin, 0);
}

static void ir_inline_test_general(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    ir_inline_test_general_units(test, input, output, twin, scratch);
    ir_inline_test_general_by_value(test, input, output, twin, scratch);
    ir_inline_test_general_limits(test, input, output, twin, scratch);
}
