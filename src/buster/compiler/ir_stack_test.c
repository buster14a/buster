// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent dynamic-stack contracts, included after the interpreter, optimizers and selector. test-ir-lowering
// runs these alongside generated typed/reference/Tier-0-native comparisons. Handwritten IR supplies an independent
// oracle for lowering, dynamic memory lifetime, CFG execution and transformations; twin equality alone is not proof.

static InterpreterOptions ir_stack_test_limits(void)
{
    InterpreterOptions options = interpreter_default_options();
    options.max_frames = 8;
    options.max_values = 8192;
    options.max_locals = 512;
    options.max_memories = 256;
    return options;
}

static void ir_stack_test_execute(IrLowerTestTally* tally, const IrFunction* function, uint64_t argument, uint64_t expected, InterpreterStatus status, Arena* execution)
{
    const IrInterpreterFunction code = { .symbol = 1, .function = function };
    const IrInterpreterProgram program = { .functions = &code, .function_count = 1 };
    const InterpreterValue input = { .low = argument };
    const InterpreterOptions options = ir_stack_test_limits();
    const uint64_t mark = execution->length;
    const InterpreterResult result = ir_interpret(&program, 1, &input, function->parameter_count, &options, execution);
    const bool ok = result.status == status && (status != INTERPRETER_OK || result.value.low == expected);
    ir_lower_test_expect(tally, ok && execution->length == mark, "dynamic IR execution and arena cleanup", argument, status);
    if (!ok && tally->failures < 20)
    {
        fprintf(stderr, "  dynamic status %u expected %u, value %" PRIu64 " expected %" PRIu64 ", instruction %u\n", result.status, status, result.value.low, expected, result.entry);
    }
}

static bool ir_stack_test_parse(IrLowerTestTally* tally, const char* text, Arena* output, Arena* scratch, IrFunction* function)
{
    const String8 source = { .pointer = (char*)text, .length = strlen(text) };
    const IrParseResult parsed = ir_parse(source, output, scratch, function);
    const bool ok = parsed.error == IR_PARSE_OK && parsed.consumed == source.length;
    ir_lower_test_expect(tally, ok, "independent dynamic-stack IR fixture parses", parsed.line, parsed.column);
    return ok;
}

static void ir_stack_test_effects(IrLowerTestTally* tally, const IrFunction* before, const IrFunction* after)
{
    uint32_t next = 0;
    bool equal = true;
    for (uint32_t i = 0; i < before->instruction_count; i += 1)
    {
        const uint8_t op = before->op[i];
        if (op == IR_ALLOCA || op == IR_STACK_SAVE || op == IR_STACK_RESTORE)
        {
            while (next < after->instruction_count && after->op[next] != IR_ALLOCA && after->op[next] != IR_STACK_SAVE && after->op[next] != IR_STACK_RESTORE)
            {
                next += 1;
            }
            equal &= next < after->instruction_count && after->op[next] == op;
            next += next < after->instruction_count;
        }
    }
    while (next < after->instruction_count)
    {
        equal &= after->op[next] != IR_ALLOCA && after->op[next] != IR_STACK_SAVE && after->op[next] != IR_STACK_RESTORE;
        next += 1;
    }
    ir_lower_test_expect(tally, equal, "stack effects retained once and in order", 0, 0);
}

static void ir_stack_test_consumers(IrLowerTestTally* tally, const IrFunction* function, uint64_t argument, uint64_t expected, Arena* output, Arena* scratch, Arena* execution)
{
    const uint64_t mark = output->length;
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        for (uint32_t pass = 0; pass < 5; pass += 1)
        {
            IrFunction transformed = { 0 };
            IrCheck check = { 0 };
            switch (pass)
            {
            case 0:
                check = scalar ? ssa_ir_promote_scalar(function, output, scratch, &transformed) : ssa_ir_promote(function, output, scratch, &transformed);
                break;
            case 1:
            {
                IrOptimizeOptions options = ir_optimize_options();
                options.scalar = scalar != 0;
                IrOptimizeResult result = { 0 };
                check = ir_optimize(function, output, scratch, options, &result);
                transformed = result.function;
                break;
            }
            case 2:
            {
                IrGvnOptions options = ir_gvn_options();
                options.scalar = scalar != 0;
                IrGvnResult result = { 0 };
                check = ir_optimize_gvn(function, output, scratch, options, &result);
                transformed = result.function;
                break;
            }
            case 3:
            {
                IrLicmOptions options = ir_licm_options();
                options.scalar = scalar != 0;
                IrLicmResult result = { 0 };
                check = ir_optimize_licm(function, output, scratch, options, &result);
                transformed = result.function;
                break;
            }
            case 4:
            {
                const IrSroaOptions options = { .scalar = scalar != 0 };
                IrSroaResult result = { 0 };
                check = ir_optimize_sroa(function, output, scratch, options, &result);
                transformed = result.function;
                break;
            }
            }
            if (ir_lower_test_expect(tally, check.error == IR_OK, "stack IR consumer accepts", scalar, pass))
            {
                ir_lower_test_expect(tally, ir_verify(&transformed, scratch, 0).error == IR_OK && ir_verify_scalar(&transformed, scratch, 0).error == IR_OK, "stack IR verifies after transformation", scalar, pass);
                ir_stack_test_effects(tally, function, &transformed);
                ir_stack_test_execute(tally, &transformed, argument, expected, INTERPRETER_OK, execution);
            }
            arena_pop_to(output, mark);
        }
    }
}

static void ir_stack_test_memory(IrLowerTestTally* tally)
{
    Arena arena = arena_reserve(1ull << 20);
    InterpreterMemory memories[16];
    InterpreterRuntime runtime = { .memories = memories, .memory_capacity = 16, .check_memory = true, .check_undefined = true };
    InterpreterStack stack = { 0 };
    InterpreterStack other = { 0 };
    uint8_t ordinary[8] = { 37 };
    uint8_t defined[8];
    memset(defined, 1, sizeof(defined));
    bool ok = interpreter_memory_add(&runtime, ordinary, 8, defined, true, 1);
    uint64_t outer = 0;
    uint64_t same = 0;
    uint64_t inner = 0;
    uint64_t pointer = 0;
    uint64_t second = 0;
    ok &= interpreter_stack_allocate(&stack, 0, 1, &runtime, &arena, &pointer) == INTERPRETER_OK;
    ok &= interpreter_stack_save(&stack, &runtime, &arena, &outer) == INTERPRETER_OK;
    ok &= interpreter_stack_allocate(&stack, 0, 1, &runtime, &arena, &second) == INTERPRETER_OK && pointer == second;
    const uint64_t saved_mark = arena.length;
    for (uint32_t repeat = 0; repeat < 1000; repeat += 1)
    {
        ok &= interpreter_stack_save(&stack, &runtime, &arena, &same) == INTERPRETER_OK && same == outer && arena.length == saved_mark;
    }
    ir_lower_test_expect(tally, ok, "same-position saves are bounded and interchangeable", 0, 0);
    for (uint32_t size = 1; size < 80; size += 1)
    {
        const uint64_t rounded = (size + 15u) & ~15u;
        ok = interpreter_stack_allocate(&stack, size, 1, &runtime, &arena, &pointer) == INTERPRETER_OK;
        ok &= (pointer & 15) == 0 && runtime.memories[runtime.memory_count - 1].size == rounded;
        ok &= interpreter_memory_access(&runtime, pointer, rounded, false) == INTERPRETER_UNDEFINED;
        ok &= interpreter_memory_access(&runtime, pointer, rounded, true) == INTERPRETER_OK;
        memset((void*)(uintptr_t)pointer, 0x6b, rounded);
        ok &= interpreter_stack_save(&stack, &runtime, &arena, &inner) == INTERPRETER_OK;
        ok &= interpreter_stack_allocate(&stack, 17, 1, &runtime, &arena, &second) == INTERPRETER_OK;
        ok &= interpreter_stack_restore(&stack, inner, &runtime, &arena) == INTERPRETER_OK;
        ok &= interpreter_memory_access(&runtime, second, 1, false) == INTERPRETER_MEMORY;
        ok &= interpreter_memory_access(&runtime, pointer, rounded, false) == INTERPRETER_OK;
        ok &= interpreter_stack_restore(&stack, outer, &runtime, &arena) == INTERPRETER_OK && arena.length == saved_mark;
        ok &= interpreter_memory_access(&runtime, pointer, 1, false) == INTERPRETER_MEMORY;
        ok &= interpreter_stack_restore(&stack, inner, &runtime, &arena) == INTERPRETER_INVALID;
        ok &= ordinary[0] == 37 && interpreter_memory_access(&runtime, (uintptr_t)ordinary, 8, false) == INTERPRETER_OK;
        ir_lower_test_expect(tally, ok, "padding, undefined bytes, nested restores and ordinary locals", size, 0);
    }
    ok = interpreter_stack_restore(&other, outer, &runtime, &arena) == INTERPRETER_INVALID;
    ok &= interpreter_stack_restore(&stack, UINT64_MAX, &runtime, &arena) == INTERPRETER_INVALID;
    const uint64_t mark = arena.length;
    const uint32_t count = runtime.memory_count;
    ok &= interpreter_stack_allocate(&stack, 0, 1, &runtime, &arena, &pointer) == INTERPRETER_OK && (pointer & 15) == 0 && arena.length == mark;
    ok &= interpreter_stack_allocate(&stack, UINT64_MAX, 1, &runtime, &arena, &pointer) == INTERPRETER_ARITHMETIC;
    ok &= interpreter_stack_allocate(&stack, UINT64_MAX - 15, 1, &runtime, &arena, &pointer) == INTERPRETER_RESOURCE;
    ok &= interpreter_stack_allocate(&stack, arena.capacity, 1, &runtime, &arena, &pointer) == INTERPRETER_RESOURCE;
    ok &= arena.length == mark && runtime.memory_count == count && stack.allocation == 0;
    ir_lower_test_expect(tally, ok, "forged/cross-frame handles and allocation overflow roll back", 0, 0);
    runtime.memory_capacity = runtime.memory_count;
    ok = interpreter_stack_allocate(&stack, 16, 1, &runtime, &arena, &pointer) == INTERPRETER_RESOURCE && arena.length == mark && stack.allocation == 0;
    ir_lower_test_expect(tally, ok, "memory registry exhaustion rolls back dynamic storage", 0, 0);
    runtime.memory_capacity = 16;
    ok = interpreter_stack_allocate(&stack, 16, 1, &runtime, &arena, &pointer) == INTERPRETER_OK;
    ok &= interpreter_stack_save(&stack, &runtime, &arena, &inner) == INTERPRETER_OK;
    ok &= interpreter_stack_restore(&stack, outer, &runtime, &arena) == INTERPRETER_OK;
    ok &= interpreter_stack_allocate(&stack, 16, 1, &runtime, &arena, &pointer) == INTERPRETER_OK;
    ok &= interpreter_stack_save(&stack, &runtime, &arena, &same) == INTERPRETER_OK && same != inner;
    ok &= interpreter_stack_restore(&stack, inner, &runtime, &arena) == INTERPRETER_INVALID;
    ok &= interpreter_stack_restore(&other, same, &runtime, &arena) == INTERPRETER_INVALID;
    ok &= interpreter_stack_restore(&stack, outer, &runtime, &arena) == INTERPRETER_OK;
    ir_lower_test_expect(tally, ok, "discarded handle cannot revive when arena addresses are reused", 0, 0);
    ok = interpreter_stack_allocate(&stack, 16, 1, &runtime, &arena, &pointer) == INTERPRETER_OK;
    const uint64_t full_mark = arena.length;
    runtime.stack_serial = UINT64_MAX;
    ok &= interpreter_stack_save(&stack, &runtime, &arena, &same) == INTERPRETER_RESOURCE && arena.length == full_mark;
    ok &= interpreter_stack_restore(&stack, outer, &runtime, &arena) == INTERPRETER_OK;
    ir_lower_test_expect(tally, ok, "save handle exhaustion leaves stack state usable", 0, 0);
    arena_release(&arena);
}

static void ir_stack_test_chain(IrLowerTestTally* tally, const IrFunction* function, uint64_t argument, uint64_t expected, Arena* scratch, Arena* execution)
{
    Arena storage[2] = { arena_reserve(8ull << 20), arena_reserve(8ull << 20) };
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        IrFunction current = *function;
        bool ok = storage[0].base != 0 && storage[1].base != 0;
        for (uint32_t pass = 0; ok && pass < 5; pass += 1)
        {
            Arena* output = &storage[pass % 2];
            arena_pop_to(output, 0);
            IrFunction next = { 0 };
            IrCheck check = { 0 };
            switch (pass)
            {
            case 0:
                check = scalar ? ssa_ir_promote_scalar(&current, output, scratch, &next) : ssa_ir_promote(&current, output, scratch, &next);
                break;
            case 1:
            {
                IrOptimizeOptions options = ir_optimize_options();
                options.scalar = scalar != 0;
                IrOptimizeResult result = { 0 };
                check = ir_optimize(&current, output, scratch, options, &result);
                next = result.function;
                break;
            }
            case 2:
            {
                IrGvnOptions options = ir_gvn_options();
                options.scalar = scalar != 0;
                IrGvnResult result = { 0 };
                check = ir_optimize_gvn(&current, output, scratch, options, &result);
                next = result.function;
                break;
            }
            case 3:
            {
                IrLicmOptions options = ir_licm_options();
                options.scalar = scalar != 0;
                IrLicmResult result = { 0 };
                check = ir_optimize_licm(&current, output, scratch, options, &result);
                next = result.function;
                break;
            }
            case 4:
            {
                const IrSroaOptions options = { .scalar = scalar != 0 };
                IrSroaResult result = { 0 };
                check = ir_optimize_sroa(&current, output, scratch, options, &result);
                next = result.function;
                break;
            }
            }
            ok = ir_lower_test_expect(tally, check.error == IR_OK && ir_verify_complete(&next, scratch, true).error == IR_OK && ir_verify_complete(&next, scratch, false).error == IR_OK, "chained stack SSA/optimizers preserve complete verification", scalar, pass);
            if (ok)
            {
                ir_stack_test_effects(tally, function, &next);
                ir_stack_test_execute(tally, &next, argument, expected, INTERPRETER_OK, execution);
                current = next;
            }
        }
    }
    arena_release(&storage[0]);
    arena_release(&storage[1]);
}

static const char ir_stack_test_loop[] =
    "function (i64) -> i64 {\n"
    "b0:\n"
    "  %0 = param.i64 0\n"
    "  %1 = const.i64 0x0000000000000000\n"
    "  %2 = const.i64 0x0000000000000001\n"
    "  %3 = const.i64 0x0000000000010000\n"
    "  %4 = br.void b1\n"
    "b1:\n"
    "  %5 = phi.i64 [%1, b0], [%19, b3]\n"
    "  %6 = icmp.i1 %5, %0 ult\n"
    "  %7 = cbr.void %6, b2, b4\n"
    "b2:\n"
    "  %8 = stack_save.i64\n"
    "  %9 = alloca.ptr %3\n"
    "  %10 = store.void %9, %5 align=16\n"
    "  %11 = stack_save.i64\n"
    "  %12 = alloca.ptr %3\n"
    "  %13 = store.void %12, %2 align=16\n"
    "  %14 = stack_restore.void %11\n"
    "  %15 = load.i64 %9 align=16\n"
    "  %16 = icmp.i1 %15, %5 eq\n"
    "  %17 = cbr.void %16, b3, b5\n"
    "b3:\n"
    "  %18 = stack_restore.void %8\n"
    "  %19 = add.i64 %5, %2\n"
    "  %20 = br.void b1\n"
    "b4:\n"
    "  %21 = ret.void %5\n"
    "b5:\n"
    "  %22 = unreachable.void\n"
    "}\n";

static void ir_stack_test_invalid(IrLowerTestTally* tally, IrFunction* function, Arena* scratch)
{
    uint32_t nodes[3] = { 0 };
    for (uint32_t i = 0; i < function->instruction_count; i += 1)
    {
        if (function->op[i] >= IR_ALLOCA && function->op[i] <= IR_STACK_RESTORE)
        {
            nodes[function->op[i] - IR_ALLOCA] = i;
        }
    }
    for (uint32_t op = 0; op < 3; op += 1)
    {
        const uint32_t i = nodes[op];
        const uint8_t type = function->type[i];
        const uint32_t a = function->a[i];
        for (uint32_t mutation = 0; mutation < 5; mutation += 1)
        {
            if (mutation == 0) { function->type[i] = IR_F64; }
            if (mutation == 1) { function->b[i] = 0; }
            if (mutation == 2) { function->flags[i] = BUSTER_IR_VOLATILE; }
            if (mutation == 3) { function->alignment_log2[i] = 4; }
            if (mutation == 4) { function->a[i] = op == 1 ? 0 : BUSTER_IR_NONE; }
            const IrCheck vector = ir_verify(function, scratch, 0);
            const IrCheck scalar = ir_verify_scalar(function, scratch, 0);
            ir_lower_test_expect(tally, vector.error != IR_OK && vector.error == scalar.error && vector.instruction == scalar.instruction && vector.instruction == i, "malformed dynamic IR refused by both verifiers", op, mutation);
            function->type[i] = type;
            function->a[i] = a;
            function->b[i] = BUSTER_IR_NONE;
            function->flags[i] = 0;
            function->alignment_log2[i] = 0;
        }
    }
}

static void ir_stack_test_native_boundary(IrLowerTestTally* tally, Arena* input, Arena* output, Arena* scratch)
{
    static const char* const sources[] = {
        "function () -> void {\nb0:\n  %0 = stack_save.i64\n  %1 = ret.void\n}\n",
        "function (i64) -> void {\nb0:\n  %0 = param.i64 0\n  %1 = alloca.ptr %0\n  %2 = ret.void\n}\n",
        "function (i64) -> void {\nb0:\n  %0 = param.i64 0\n  %1 = stack_restore.void %0\n  %2 = ret.void\n}\n",
    };
    const uint64_t input_mark = input->length;
    for (uint32_t op = 0; op < 3; op += 1)
    {
        IrFunction function = { 0 };
        if (ir_stack_test_parse(tally, sources[op], input, scratch, &function))
        {
            for (uint32_t vector = 0; vector < 2; vector += 1)
            {
                Tier1SelectPlan plan = { .estimated_cost = 123 };
                const Tier1SelectPlan preserved = plan;
                const uint64_t mark = output->length;
                const uint64_t scratch_mark = scratch->length;
                uint32_t node = BUSTER_IR_NONE;
                const Tier1SelectError error = tier1_select_plan(&function, 0, output, scratch, vector != 0, &plan, &node);
                ir_lower_test_expect(tally, error == TIER1_SELECT_ERROR_UNSUPPORTED && node == (op == 0 ? 0u : 1u) && memcmp(&plan, &preserved, sizeof(plan)) == 0 && output->length == mark && scratch->length == scratch_mark, "native dynamic stack refused transactionally", vector, op);
            }
        }
        arena_pop_to(input, input_mark);
    }
}

static void ir_stack_test_calls(IrLowerTestTally* tally, Arena* input, Arena* output, Arena* scratch, Arena* execution)
{
    static const char caller[] =
        "function () -> i64 {\nb0:\n"
        "  %0 = stack_save.i64\n"
        "  %1 = const.i64 0x10\n"
        "  %2 = alloca.ptr %1\n"
        "  %3 = const.i64 0x49\n"
        "  %4 = store.void %2, %3 align=16\n"
        "  %5 = call.i64 @2() fixed=() variadic=0\n"
        "  %6 = load.i64 %2 align=16\n"
        "  %7 = stack_restore.void %0\n"
        "  %8 = add.i64 %6, %5\n"
        "  %9 = ret.void %8\n}\n";
    static const char callee[] =
        "function () -> i64 {\nb0:\n"
        "  %0 = const.i64 0x10\n"
        "  %1 = alloca.ptr %0\n"
        "  %2 = const.i64 0x7\n"
        "  %3 = store.void %1, %2 align=16\n"
        "  %4 = ret.void %2\n}\n";
    const uint64_t mark = input->length;
    const uint64_t output_mark = output->length;
    IrFunction functions[2];
    const bool ready = ir_stack_test_parse(tally, caller, input, scratch, &functions[0]) && ir_stack_test_parse(tally, callee, input, scratch, &functions[1]);
    if (ready)
    {
        functions[0].symbol_count = 3;
        functions[1].symbol_count = 3;
        const uint32_t symbols[2] = { 1, 2 };
        const uint8_t flags[2] = { BUSTER_IR_INLINE_INTERNAL, BUSTER_IR_INLINE_INTERNAL };
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            const IrInlineUnit unit = { .functions = functions, .symbols = symbols, .flags = flags, .count = 2, .symbol_count = 3 };
            IrInlineOptions options = ir_inline_options();
            options.scalar = scalar != 0;
            IrInlineResult inlined = { 0 };
            uint32_t failed = BUSTER_IR_NONE;
            const IrCheck check = ir_optimize_inline(&unit, output, scratch, options, &inlined, &failed);
            if (ir_lower_test_expect(tally, check.error == IR_OK && inlined.inlined_calls == 0 && inlined.call_count == 1 && inlined.decisions[0] == IR_INLINE_BODY, "inlining preserves callee dynamic lifetime boundary", scalar, failed))
            {
                const IrInterpreterFunction code[2] = {
                    { .symbol = 1, .function = &inlined.functions[0] },
                    { .symbol = 2, .function = &inlined.functions[1] },
                };
                const IrInterpreterProgram program = { .functions = code, .function_count = 2 };
                const InterpreterOptions limits = ir_stack_test_limits();
                const InterpreterResult result = ir_interpret(&program, 1, 0, 0, &limits, execution);
                ir_lower_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == 80 && execution->length == 0, "callee early return preserves caller dynamic objects", scalar, result.status);
            }
            arena_pop_to(output, output_mark);
        }
    }
    arena_pop_to(input, mark);
}

static void ir_stack_test_joins(IrLowerTestTally* tally, Arena* input, Arena* output, Arena* scratch, Arena* execution)
{
    static const char source[] =
        "function (i1) -> i64 {\nb0:\n"
        "  %0 = param.i1 0\n  %1 = const.i64 0x10\n  %2 = stack_save.i64\n"
        "  %3 = alloca.ptr %1\n  %4 = store.void %3, %1 align=16\n  %5 = cbr.void %0, b1, b2\n"
        "b1:\n  %6 = stack_save.i64\n  %7 = alloca.ptr %1\n  %8 = br.void b3\n"
        "b2:\n  %9 = copy.i64 %2\n  %10 = br.void b3\n"
        "b3:\n  %11 = phi.i64 [%6, b1], [%9, b2]\n"
        "  %12 = stack_restore.void %11\n  %13 = cbr.void %0, b4, b5\n"
        "b4:\n  %14 = load.i64 %3 align=16\n  %15 = ret.void %14\n"
        "b5:\n  %16 = ret.void %1\n}\n";
    const uint64_t mark = input->length;
    IrFunction function = { 0 };
    if (ir_stack_test_parse(tally, source, input, scratch, &function))
    {
        for (uint64_t condition = 0; condition < 2; condition += 1)
        {
            ir_stack_test_execute(tally, &function, condition, 16, INTERPRETER_OK, execution);
            ir_stack_test_consumers(tally, &function, condition, 16, output, scratch, execution);
            ir_stack_test_chain(tally, &function, condition, 16, scratch, execution);
        }
    }
    arena_pop_to(input, mark);
}

static void ir_stack_test_faults(IrLowerTestTally* tally, Arena* input, Arena* output, Arena* scratch, Arena* execution)
{
    static const char* const sources[] = {
        "function () -> i64 {\nb0:\n  %0 = const.i64 0x10\n  %1 = alloca.ptr %0\n  %2 = load.i64 %1 align=16\n  %3 = ret.void %2\n}\n",
        "function () -> i64 {\nb0:\n  %0 = stack_save.i64\n  %1 = const.i64 0x10\n  %2 = alloca.ptr %1\n  %3 = store.void %2, %1 align=16\n  %4 = stack_restore.void %0\n  %5 = load.i64 %2 align=16\n  %6 = ret.void %5\n}\n",
        "function () -> void {\nb0:\n  %0 = const.i64 0x1\n  %1 = stack_restore.void %0\n  %2 = ret.void\n}\n",
        "function () -> i64 {\nb0:\n  %0 = const.i64 0x0\n  %1 = alloca.ptr %0\n  %2 = ptrtoint.i64 %1\n  %3 = const.i64 0xf\n  %4 = and.i64 %2, %3\n  %5 = ret.void %4\n}\n",
        "function () -> i1 {\nb0:\n  %0 = stack_save.i64\n  %1 = stack_save.i64\n  %2 = stack_restore.void %0\n  %3 = stack_restore.void %1\n  %4 = icmp.i1 %0, %1 eq\n  %5 = ret.void %4\n}\n",
        "function () -> i1 {\nb0:\n  %0 = const.i64 0x0\n  %1 = alloca.ptr %0\n  %2 = stack_save.i64\n  %3 = alloca.ptr %0\n  %4 = icmp.i1 %1, %3 eq\n  %5 = stack_restore.void %2\n  %6 = ret.void %4\n}\n",
        "function () -> i64 {\n  slot $0 size=32 align=16 type=void flags=0x01\nb0:\n  %0 = stack_addr.ptr $0\n  %1 = const.i64 0x49\n  %2 = store.void %0, %1 align=16\n  %3 = stack_save.i64\n  %4 = const.i64 0x10\n  %5 = alloca.ptr %4\n  %6 = store.void %5, %1 align=16\n  %7 = stack_restore.void %3\n  %8 = load.i64 %0 align=16\n  %9 = ret.void %8\n}\n",
        "function () -> i64 {\nb0:\n  %0 = const.i64 0x10\n  %1 = alloca.ptr %0\n  %2 = const.f64 0x3ff8000000000000\n  %3 = store.void %1, %2 align=16\n  %4 = load.f64 %1 align=16\n  %5 = bitcast.i64 %4\n  %6 = ret.void %5\n}\n",
    };
    static const InterpreterStatus statuses[] = { INTERPRETER_UNDEFINED, INTERPRETER_MEMORY, INTERPRETER_INVALID, INTERPRETER_OK, INTERPRETER_OK, INTERPRETER_OK, INTERPRETER_OK, INTERPRETER_OK };
    static const uint64_t values[] = { 0, 0, 0, 0, 1, 1, 73, UINT64_C(0x3ff8000000000000) };
    const uint64_t mark = input->length;
    for (uint32_t c = 0; c < sizeof(sources) / sizeof(sources[0]); c += 1)
    {
        IrFunction function = { 0 };
        if (ir_stack_test_parse(tally, sources[c], input, scratch, &function))
        {
            ir_stack_test_execute(tally, &function, 0, values[c], statuses[c], execution);
            if (c >= 6)
            {
                ir_stack_test_consumers(tally, &function, 0, values[c], output, scratch, execution);
                ir_stack_test_chain(tally, &function, 0, values[c], scratch, execution);
            }
        }
        arena_pop_to(input, mark);
    }
    static const char* const invalid[] = {
        "function (i32) -> ptr {\nb0:\n  %0 = param.i32 0\n  %1 = alloca.ptr %0\n  %2 = ret.void %1\n}\n",
        "function (i128) -> void {\nb0:\n  %0 = param.i128 0\n  %1 = stack_restore.void %0\n  %2 = ret.void\n}\n",
    };
    for (uint32_t c = 0; c < 2; c += 1)
    {
        const String8 source = { .pointer = (char*)invalid[c], .length = strlen(invalid[c]) };
        IrFunction result = { .instruction_count = 123 };
        const IrFunction preserved = result;
        const IrParseResult parsed = ir_parse(source, input, scratch, &result);
        ir_lower_test_expect(tally, parsed.error == IR_PARSE_VERIFY && parsed.check.error == IR_ERROR_TYPE && input->length == mark && memcmp(&result, &preserved, sizeof(result)) == 0, "unsupported dynamic operand width rejected transactionally", c, 0);
    }
}

static void ir_lower_test_stack(IrLowerTestTally* tally)
{
    ir_stack_test_memory(tally);
    Arena output = arena_reserve(8ull << 20);
    Arena twin = arena_reserve(8ull << 20);
    Arena scratch = arena_reserve(8ull << 20);
    Arena execution = arena_reserve(2ull << 20);
    const uint64_t before = tally->checks;
    const uint8_t kinds[] = {
        BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_ALLOCA, BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONST, BUSTER_TYPED_SET,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_LOAD, BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_STACK_RESTORE,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_RETURN,
    };
    uint8_t classes[sizeof(kinds)];
    memset(classes, BUSTER_TYPED_CLASS_U64, sizeof(classes));
    uint64_t values[] = { 0, 1, 0, 0, 2, 2, 73, 0, 2, 0, 3, 1, 0, 3, 0 };
    const uint8_t local_classes[] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
    const uint8_t local_flags[4] = { 0 };
    const AbiLocation parameter = {
        .kind = BUSTER_ABI_LOCATION_REGISTERS,
        .registers = {
            X86_REGISTER_RDI,
        },
    };
    const Tier0Function source = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = sizeof(kinds),
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 4,
        .parameter_count = 1,
        .parameters = &parameter,
    };
    IrFunction function = { 0 };
    if (ir_lower_test_lower(tally, &source, IR_I64, 2, &output, &twin, &scratch, &function, 131, 0))
    {
        const uint64_t sizes[] = { 0, 8, 15, 16, 17, 4096, 65537, UINT64_MAX - 15, UINT64_MAX - 14, UINT64_MAX };
        const SymbolId symbol = 1;
        const InterpreterProgram program = { .functions = &source, .function_symbols = &symbol, .function_count = 1 };
        const InterpreterOptions options = ir_stack_test_limits();
        for (uint32_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s += 1)
        {
            const uint64_t size = sizes[s];
            const InterpreterStatus expected = size == 0 ? INTERPRETER_MEMORY : size > UINT64_MAX - 15 ? INTERPRETER_ARITHMETIC : size > execution.capacity ? INTERPRETER_RESOURCE : INTERPRETER_OK;
            ir_stack_test_execute(tally, &function, size, 73, expected, &execution);
            const InterpreterValue argument = { .low = size };
            InterpreterResult result = { 0 };
            const bool ran = interpreter_typed_run(&program, 1, &argument, 1, &execution, &options, &result);
            ir_lower_test_expect(tally, ran == (expected == INTERPRETER_OK) && result.status == expected && (expected != INTERPRETER_OK || result.value.low == 73) && execution.length == 0, "typed stack execution agrees with independent expected result", size, result.status);
        }
        ir_stack_test_consumers(tally, &function, 17, 73, &twin, &scratch, &execution);
        ir_stack_test_chain(tally, &function, 17, 73, &scratch, &execution);
        IrFunction parsed = { 0 };
        ir_lower_test_expect(tally, ir_interpreter_test_roundtrip(&function, &execution, &twin, &scratch, &parsed), "lowered stack IR text round trip", 0, 0);
        ir_stack_test_invalid(tally, &function, &scratch);
        // Vary every available output capacity; each failed attempt preserves nonzero marks and the result.
        const uint64_t total = function.arena_bytes;
        const uint64_t capacity = twin.capacity;
        arena_pop_to(&twin, 0);
        arena_push(&twin, 24, 1);
        arena_push(&scratch, 40, 1);
        for (uint32_t vector = 0; vector < 2; vector += 1)
        {
            for (uint64_t bytes = 0; bytes < total; bytes += 8)
            {
                twin.capacity = 24 + bytes;
                IrFunction result = { .instruction_count = 123 };
                const IrFunction preserved = result;
                const IrCheck check = ir_lower_typed_path(&source, IR_I64, 2, &twin, &scratch, &result, vector != 0);
                const bool good = check.error == IR_OK || (check.error == IR_ERROR_MEMORY && memcmp(&result, &preserved, sizeof(result)) == 0 && twin.length == 24);
                ir_lower_test_expect(tally, good && scratch.length == 40, "dynamic lowering allocation rollback", bytes, vector);
                arena_pop_to(&twin, 24);
            }
        }
        twin.capacity = capacity;
        arena_pop_to(&scratch, 0);
        for (uint32_t vector = 0; vector < 2; vector += 1)
        {
            for (uint32_t mutation = 0; mutation < 3; mutation += 1)
            {
                values[mutation == 0 ? 0 : 3] = mutation < 2 ? 1 : 0;
                classes[3] = mutation == 2 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64;
                IrFunction result = { .instruction_count = 123 };
                const IrFunction preserved = result;
                const uint64_t mark = twin.length;
                const IrCheck check = ir_lower_typed_path(&source, IR_I64, 2, &twin, &scratch, &result, vector != 0);
                ir_lower_test_expect(tally, check.error == IR_ERROR_STRUCTURE && twin.length == mark && scratch.length == 0 && memcmp(&result, &preserved, sizeof(result)) == 0, "malformed typed stack input refused before allocation", vector, mutation);
                values[0] = 0;
                values[3] = 0;
                classes[3] = BUSTER_TYPED_CLASS_U64;
            }
        }
    }
    arena_pop_to(&output, 0);
    arena_pop_to(&twin, 0);
    arena_pop_to(&execution, 0);
    const uint8_t edge_kinds[] = {
        BUSTER_TYPED_STACK_SAVE,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_ALLOCA,
        BUSTER_TYPED_DISCARD,
        BUSTER_TYPED_JUMP_VALUE,
        BUSTER_TYPED_STACK_RESTORE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_RETURN,
    };
    uint8_t edge_classes[sizeof(edge_kinds)];
    memset(edge_classes, BUSTER_TYPED_CLASS_U64, sizeof(edge_classes));
    const uint64_t edge_values[] = { 0, 0, 7, 16, 0, 0, 7, 0, 89, 0 };
    const Tier0Function edge_source = {
        .kinds = edge_kinds,
        .classes = edge_classes,
        .values = edge_values,
        .count = sizeof(edge_kinds),
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 1,
        .parameter_count = 1,
        .parameters = &parameter,
    };
    if (ir_lower_test_lower(tally, &edge_source, IR_I64, 1, &output, &twin, &scratch, &function, 131, 1))
    {
        for (uint64_t condition = 0; condition < 2; condition += 1)
        {
            ir_stack_test_execute(tally, &function, condition, 89, INTERPRETER_OK, &execution);
            ir_stack_test_chain(tally, &function, condition, 89, &scratch, &execution);
        }
    }
    arena_pop_to(&output, 0);
    arena_pop_to(&twin, 0);
    ir_stack_test_native_boundary(tally, &output, &twin, &scratch);
    ir_stack_test_calls(tally, &output, &twin, &scratch, &execution);
    ir_stack_test_joins(tally, &output, &twin, &scratch, &execution);
    ir_stack_test_faults(tally, &output, &twin, &scratch, &execution);
    if (ir_stack_test_parse(tally, ir_stack_test_loop, &output, &scratch, &function))
    {
        ir_stack_test_execute(tally, &function, 0, 0, INTERPRETER_OK, &execution);
        ir_stack_test_execute(tally, &function, 1000, 1000, INTERPRETER_OK, &execution);
        ir_stack_test_consumers(tally, &function, 1000, 1000, &twin, &scratch, &execution);
        ir_stack_test_chain(tally, &function, 1000, 1000, &scratch, &execution);
    }
    printf("IR dynamic stack: %" PRIu64 " consumer/contract checks (plus lifetime checks)\n", tally->checks - before);
    arena_release(&output);
    arena_release(&twin);
    arena_release(&scratch);
    arena_release(&execution);
}
