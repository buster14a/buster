// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1282's inlining fixtures: SIMD callees (vectors, vector constants, masks, machine rows and stack slots, a straight
// chain of blocks) and a call that passes and returns vectors by value through ABI records, the x86 intrinsics'
// wrappers' shape. The by-value unit runs on the shared IR interpreter (an independent oracle with slots, vectors and
// ABI calls) before inlining, after it, and after SROA's #1282 options and SSA promotion, against the lanes computed
// here; promotion must leave no slot and only the parameter's three accesses. The interpreter has no model of x86
// records, so the rows' fixture checks the rewrite's columns: rows, vector constants and slots appended, their indices
// rebased, the row calls kept. Include after ir_optimize_sroa.c, ir_interpreter.c, ir_parse.c and
// ir_optimize_inline_test.c, which declares ir_inline_test_simd.

static bool ir_inline_simd_parse(IrInlineTest* test, const char* text, Arena* arena, Arena* scratch, IrFunction* function)
{
    const IrParseResult parsed = ir_parse((String8){ .pointer = (char*)text, .length = strlen(text) }, arena, scratch, function);
    ir_inline_test_expect(test, parsed.error == IR_PARSE_OK && parsed.check.error == IR_OK, "a SIMD fixture parses");
    if (parsed.error != IR_PARSE_OK || parsed.check.error != IR_OK)
    {
        fprintf(stderr, "ir-inline: fixture parse error %u at line %" PRIu64 " column %" PRIu64 ", check %u\n", parsed.error, parsed.line, parsed.column, parsed.check.error);
    }
    // One symbol namespace for the unit's functions.
    function->symbol_count = 128;
    return parsed.error == IR_PARSE_OK && parsed.check.error == IR_OK;
}

// Runs `entry` of the functions (symbols 1 and 2) on a 192-byte buffer whose first 128 bytes are `seed`'s, and returns
// whether it finished; the buffer's bytes into `bytes`.
static bool ir_inline_simd_run(const IrFunction* functions, uint32_t count, const uint8_t* seed, uint8_t* bytes, Arena* scratch)
{
    memcpy(bytes, seed, 128);
    memset(bytes + 128, 0x5a, 64);
    const IrInterpreterFunction bound[2] = {
        { .symbol = 1, .function = &functions[0] },
        { .symbol = 2, .function = count > 1 ? &functions[1] : &functions[0] },
    };
    const InterpreterBinding buffer = { .symbol = 90, .bytes = bytes, .size = 192, .writable = true };
    const IrInterpreterProgram program = { .functions = bound, .function_count = count, .bindings = &buffer, .binding_count = 1 };
    const InterpreterValue argument = { .low = (uintptr_t)bytes };
    const InterpreterResult result = ir_interpret(&program, 1, &argument, 1, 0, scratch);
    return result.status == INTERPRETER_OK;
}

static uint32_t ir_inline_simd_count(const IrFunction* function, uint32_t op)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < function->instruction_count; i += 1)
    {
        count += function->op[i] == op;
    }
    return count;
}

// A vector's 128 hex digits of 0123456789abcdef words: its 32-bit lanes alternate 0x89abcdef and 0x01234567.
#define BUSTER_IR_INLINE_SIMD_CONSTANT "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

static void ir_inline_test_by_value(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    // The caller: two vectors at p and p + 64 passed by value, the result's bytes into its slot, stored at p + 128.
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
        "  %9 = ptrtoint.i64 %0\n"
        "  %10 = inttoptr.ptr %9\n"
        "  %11 = call.void @2(%10, %1) fixed=(ptr, ptr) variadic=0 abi=#0 into=%2\n"
        "  %12 = load.v16i32 %2 align=16\n"
        "  %13 = address.ptr %3 offset=128\n"
        "  %14 = store.void %13, %12 align=1\n"
        "  %15 = ret.void\n"
        "}\n";
    // The wrapper, as the typed stream's lowering and the optimizer leave one: its parameters' bytes copied into its
    // objects in a preheader of its own, then (a + b) ^ K stored in its result's object, whose address it returns.
    static const char callee_text[] =
        "function (ptr, ptr) -> ptr {\n"
        "  result size=64 ret(1,0,64,32,255,255,255) passed=#0\n"
        "  passed #0 size=64 loc(1,32,255,0,0)\n"
        "  passed #1 size=64 loc(1,33,255,0,0)\n"
        "  slot $0 size=64 align=64 type=void flags=0x01\n"
        "  slot $1 size=64 align=64 type=void flags=0x01\n"
        "  slot $2 size=64 align=16 type=void flags=0x01\n"
        "b0:\n"
        "  %0 = stack_addr.ptr $0\n"
        "  %1 = stack_addr.ptr $1\n"
        "  %2 = stack_addr.ptr $2\n"
        "  %3 = param.ptr 0\n"
        "  %4 = load.v16i32 %3 align=1\n"
        "  %5 = store.void %0, %4 align=64\n"
        "  %6 = param.ptr 1\n"
        "  %7 = load.v16i32 %6 align=1\n"
        "  %8 = store.void %1, %7 align=64\n"
        "  %9 = br.void b1\n"
        "b1:\n"
        "  %10 = load.v16i32 %0 align=64\n"
        "  %11 = load.v16i32 %1 align=64\n"
        "  %12 = vconst.v16i32 0x" BUSTER_IR_INLINE_SIMD_CONSTANT "\n"
        "  %13 = add.v16i32 %10, %11\n"
        "  %14 = xor.v16i32 %13, %12\n"
        "  %15 = store.void %2, %14 align=16\n"
        "  %16 = ptrtoint.i64 %2\n"
        "  %17 = inttoptr.ptr %16\n"
        "  %18 = ret.void %17\n"
        "}\n";
    IrFunction functions[2];
    if (ir_inline_simd_parse(test, caller_text, input, scratch, &functions[0]) && ir_inline_simd_parse(test, callee_text, input, scratch, &functions[1]))
    {
        const uint32_t symbols[] = { 1, 2 };
        const uint8_t flags[] = { 0, BUSTER_IR_INLINE_INTERNAL };
        const IrInlineUnit unit = { functions, symbols, flags, 2, 128 };
        uint8_t seed[128];
        for (uint32_t i = 0; i < sizeof(seed); i += 1)
        {
            seed[i] = (uint8_t)ir_inline_test_random(test);
        }
        uint8_t expected[192];
        memcpy(expected, seed, 128);
        for (uint32_t lane = 0; lane < 16; lane += 1)
        {
            uint32_t a = 0;
            uint32_t b = 0;
            memcpy(&a, seed + lane * 4, 4);
            memcpy(&b, seed + 64 + lane * 4, 4);
            const uint32_t sum = (a + b) ^ (lane % 2 == 0 ? UINT32_C(0x89abcdef) : UINT32_C(0x01234567));
            memcpy(expected + 128 + lane * 4, &sum, 4);
        }
        uint8_t bytes[192];
        ir_inline_test_expect(test, ir_inline_simd_run(functions, 2, seed, bytes, scratch) && memcmp(bytes, expected, sizeof(bytes)) == 0, "the by-value unit runs as its lanes say");
        // Its vectors copied whole (bit 2: 64 bytes) and by integers (none), both twins.
        for (uint32_t copies = 0; copies < 2; copies += 1)
        {
            // An always_inline callee's cost is no reason to keep its call (c_check_tier1_inline.c's options).
            IrInlineOptions options = ir_inline_options();
            options.maximum_cost = UINT32_MAX - 1;
            options.vector_copies = copies == 0 ? 7 : 0;
            IrInlineResult result = { 0 };
            if (ir_inline_test_run(test, &unit, options, output, twin, scratch, &result))
            {
                const IrFunction* out = &result.functions[0];
                ir_inline_test_expect(test, result.call_count == 1 && result.inlined_calls == 1 && result.decisions[0] == IR_INLINE_SELECTED,
                                      "a call whose ABI records are its callee's own is inlined");
                // The callee's three slots and a fresh one per aggregate argument; its constant appended.
                ir_inline_test_expect(test, ir_inline_simd_count(out, IR_CALL) == 0 && out->slot_count == 3 + 3 + 2 && out->vector_constant_count == 1 &&
                                          memcmp(&out->vector_constants[0], &functions[1].vector_constants[0], sizeof(IrSimdValue)) == 0 && out->block_count == 1,
                                      "the expansion takes the callee's slots and vector constant, and copies each argument");
                ir_inline_test_expect(test, ir_inline_simd_count(out, IR_LOAD) == (copies == 0 ? 2 + 2 + 2 + 2 + 1 + 1 : 2 + 16 + 2 + 2 + 8 + 1),
                                      "each aggregate's copy is one vector, or eight i64's without vectors");
                IrFunction inlined = *out;
                ir_inline_test_expect(test, ir_inline_simd_run(&inlined, 1, seed, bytes, scratch) && memcmp(bytes, expected, sizeof(bytes)) == 0, "the inlined caller runs as the unit");
                // SROA (#1282's options) and promotion leave the parameter's accesses alone: two loads and a store.
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    IrSroaOptions sroa = ir_sroa_options();
                    sroa.scalar = path == 1;
                    sroa.simd = true;
                    sroa.flagged = true;
                    IrSroaResult split = { 0 };
                    const uint64_t mark = output->length;
                    const IrCheck checked = ir_optimize_sroa(&inlined, output, scratch, sroa, &split);
                    IrFunction promoted = { 0 };
                    // Promotion's output owns no input: the twin's arena, emptied after.
                    const uint64_t twin_mark = twin->length;
                    const IrCheck promotion = checked.error == IR_OK ? (path == 1 ? ssa_ir_promote_scalar(&split.function, twin, scratch, &promoted) : ssa_ir_promote(&split.function, twin, scratch, &promoted)) : checked;
                    // Copied by integers, an argument's slot has eight i64 stores beside the callee's vector load: SROA
                    // keeps it, and the copies stay memory, correctly.
                    ir_inline_test_expect(test, checked.error == IR_OK && promotion.error == IR_OK && (copies != 0 || (split.refused_slots == 0 && split.split_slots == 8)),
                                          "SROA takes every slot of the expansion");
                    ir_inline_test_expect(test, promotion.error == IR_OK && (copies != 0 || (promoted.slot_count == 0 && ir_inline_simd_count(&promoted, IR_LOAD) == 2 && ir_inline_simd_count(&promoted, IR_STORE) == 1)),
                                          "promotion leaves no copy of a by-value vector");
                    ir_inline_test_expect(test, promotion.error == IR_OK && ir_inline_simd_run(&promoted, 1, seed, bytes, scratch) && memcmp(bytes, expected, sizeof(bytes)) == 0,
                                          "the promoted caller runs as the unit");
                    arena_pop_to(twin, twin_mark);
                    arena_pop_to(output, mark);
                }
                // Without its options, SROA keeps SIMD functions' and address-taken slots' refusals (#139's contract).
                IrSroaResult refused = { 0 };
                const uint64_t mark = output->length;
                ir_inline_test_expect(test, ir_optimize_sroa(&inlined, output, scratch, ir_sroa_options(), &refused).error == IR_ERROR_UNSUPPORTED, "SROA without `simd` refuses a SIMD function");
                arena_pop_to(output, mark);
            }
            arena_pop_to(output, 0);
            arena_pop_to(twin, 0);
        }
        // A callee that returns through a hidden pointer, or takes another aggregate's bytes, keeps its call.
        const uint32_t size = functions[1].passed[1].size;
        functions[1].passed[1].size = 32;
        IrInlineResult other = { 0 };
        if (ir_inline_test_run(test, &unit, ir_inline_options(), output, twin, scratch, &other))
        {
            ir_inline_test_expect(test, other.inlined_calls == 0 && other.decisions[0] == IR_INLINE_ABI, "records of other sizes than the callee's keep the call");
        }
        functions[1].passed[1].size = size;
        arena_pop_to(output, 0);
        arena_pop_to(twin, 0);
        // A 256 MiB aggregate by value on both sides (#1324's review): its copy's size is counted, not walked, and is
        // past the instruction bound, so the call stays a call whatever the cost threshold.
        const uint32_t first = functions[1].passed[0].size;
        functions[0].passed[0].size = UINT32_C(1) << 28;
        functions[1].passed[0].size = UINT32_C(1) << 28;
        IrInlineOptions unbounded = ir_inline_options();
        unbounded.maximum_cost = UINT32_MAX - 1;
        unbounded.vector_copies = 7;
        IrInlineResult huge = { 0 };
        if (ir_inline_test_run(test, &unit, unbounded, output, twin, scratch, &huge))
        {
            ir_inline_test_expect(test, huge.inlined_calls == 0 && huge.decisions[0] == IR_INLINE_BODY && huge.costs[0] == UINT32_MAX,
                                  "a huge by-value aggregate's call is refused by its counted size");
        }
        functions[0].passed[0].size = first;
        functions[1].passed[0].size = first;
        arena_pop_to(output, 0);
        arena_pop_to(twin, 0);
    }
    arena_pop_to(input, 0);
}

static void ir_inline_test_rows(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    static const char caller_text[] =
        "function (v16i32, m64) -> v16i32 {\n"
        "  row #0 (vector64, vector64) -> mask8 [VPCMPB_K_Z_ZM512_IMM8 0x90 0x80 0x81 0x00 0x00 0x00 4 0]\n"
        "  slot $0 size=8 align=8 type=i64 flags=0x00\n"
        "b0:\n"
        "  %0 = param.v16i32 0\n"
        "  %1 = param.m64 1\n"
        "  %2 = call.m64 row #0(%0, %0) fixed=(v16i32, v16i32) variadic=0\n"
        "  %3 = vconst.v16i32 0x" BUSTER_IR_INLINE_SIMD_CONSTANT "\n"
        "  %4 = and.m64 %1, %2\n"
        "  %5 = call.v16i32 @2(%0, %3, %4) fixed=(v16i32, v16i32, m64) variadic=0\n"
        "  %6 = stack_addr.ptr $0\n"
        "  %7 = ret.void %5\n"
        "}\n";
    static const char callee_text[] =
        "function (v16i32, v16i32, m64) -> v16i32 {\n"
        "  row #0 (vector64, vector64, mask8) -> vector64 [VPADDB_Z_Z_ZM512 0x80 0x80 0x81 0x00 0x00 0x0a 0 0]\n"
        "  slot $0 size=64 align=64 type=v16i32 flags=0x00\n"
        "b0:\n"
        "  %0 = param.v16i32 0\n"
        "  %1 = param.v16i32 1\n"
        "  %2 = param.m64 2\n"
        "  %3 = stack_addr.ptr $0\n"
        "  %4 = lifetime.void $0 start\n"
        "  %5 = call.v16i32 row #0(%0, %1, %2) fixed=(v16i32, v16i32, m64) variadic=0\n"
        "  %6 = store.void %3, %5 align=64\n"
        "  %7 = load.v16i32 %3 align=64\n"
        "  %8 = lifetime.void $0 end\n"
        "  %9 = vconst.v16i32 0x" BUSTER_IR_INLINE_SIMD_CONSTANT "\n"
        "  %10 = add.v16i32 %7, %9\n"
        "  %11 = ret.void %10\n"
        "}\n";
    IrFunction functions[2];
    if (ir_inline_simd_parse(test, caller_text, input, scratch, &functions[0]) && ir_inline_simd_parse(test, callee_text, input, scratch, &functions[1]))
    {
        const uint32_t symbols[] = { 1, 2 };
        const uint8_t flags[] = { 0, BUSTER_IR_INLINE_INTERNAL };
        const IrInlineUnit unit = { functions, symbols, flags, 2, 128 };
        IrInlineResult result = { 0 };
        if (ir_inline_test_run(test, &unit, ir_inline_options(), output, twin, scratch, &result))
        {
            const IrFunction* out = &result.functions[0];
            // A row call is no call site; the callee's, cloned, is a row call of the callee's row, appended.
            ir_inline_test_expect(test, result.call_count == 1 && result.inlined_calls == 1 && out->row_count == 2 &&
                                      memcmp(&out->rows[1], &functions[1].rows[0], sizeof(Tier0Operation)) == 0 && memcmp(&out->rows[0], &functions[0].rows[0], sizeof(Tier0Operation)) == 0,
                                  "the callee's machine rows follow the caller's");
            // The caller's own: its row call (row 1), vconst (0) and slot address ($0); the callee's, cloned: its row call
            // (row 2), vconst (1), slot address and two lifetime markers ($1).
            uint32_t rows[3] = { 0, 0, 0 };
            uint32_t vectors[2] = { 0, 0 };
            uint32_t slots[2] = { 0, 0 };
            for (uint32_t i = 0; i < out->instruction_count; i += 1)
            {
                const uint32_t op = out->op[i];
                const uint32_t row = op == IR_CALL && out->a[i] == 0 ? out->calls[out->b[i]].row : 0;
                rows[row < 3 ? row : 0] += op == IR_CALL;
                vectors[out->a[i] < 2 ? out->a[i] : 0] += op == IR_VCONST && out->a[i] < 2;
                slots[out->a[i] < 2 ? out->a[i] : 0] += (op == IR_STACK_ADDR || op == IR_LIFETIME) && out->a[i] < 2;
            }
            ir_inline_test_expect(test, ir_inline_simd_count(out, IR_CALL) == 2 && rows[0] == 0 && rows[1] == 1 && rows[2] == 1, "each row call keeps its row, a cloned one's rebased");
            ir_inline_test_expect(test, out->vector_constant_count == 2 && vectors[0] == 1 && vectors[1] == 1 && memcmp(&out->vector_constants[1], &functions[1].vector_constants[0], sizeof(IrSimdValue)) == 0,
                                  "the callee's vector constants follow the caller's, its vconst rebased");
            ir_inline_test_expect(test, out->slot_count == 2 && slots[0] == 1 && slots[1] == 3 && out->slots[1].size == 64 && out->slots[1].type == IR_V16I32 && out->slots[0].type == IR_I64,
                                  "the callee's slots follow the caller's, its addresses and lifetimes rebased");
        }
        arena_pop_to(output, 0);
        arena_pop_to(twin, 0);
    }
    arena_pop_to(input, 0);
}

// The permutes of two tables (#1840), whose third operand the extra array holds: a caller with its own calls a callee
// with two, one of whose third operand is the callee's parameter. Inlined, every permute2's third operand is in a slot of
// the result's own, remapped: the callee's to the caller's value, and the parameter's to the caller's permute2.
static void ir_inline_test_permute2(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    static const char caller_text[] =
        "function (ptr) -> void {\n"
        "b0:\n"
        "  %0 = param.ptr 0\n"
        "  %1 = load.v16i32 %0 align=1\n"
        "  %2 = address.ptr %0 offset=64\n"
        "  %3 = load.v16i32 %2 align=1\n"
        "  %4 = intrinsic.v16i32 %1, %3, %3 permute2 2\n"
        "  %5 = call.v16i32 @2(%4, %3) fixed=(v16i32, v16i32) variadic=0\n"
        "  %6 = address.ptr %0 offset=128\n"
        "  %7 = store.void %6, %5 align=1\n"
        "  %8 = ret.void\n"
        "}\n";
    static const char callee_text[] =
        "function (v16i32, v16i32) -> v16i32 {\n"
        "b0:\n"
        "  %0 = param.v16i32 0\n"
        "  %1 = param.v16i32 1\n"
        "  %2 = intrinsic.v16i32 %0, %1, %1 permute2 2\n"
        "  %3 = intrinsic.v16i32 %1, %2, %0 permute2 3\n"
        "  %4 = ret.void %3\n"
        "}\n";
    IrFunction functions[2];
    if (ir_inline_simd_parse(test, caller_text, input, scratch, &functions[0]) && ir_inline_simd_parse(test, callee_text, input, scratch, &functions[1]))
    {
        const uint32_t symbols[] = { 1, 2 };
        const uint8_t flags[] = { 0, BUSTER_IR_INLINE_INTERNAL };
        const IrInlineUnit unit = { functions, symbols, flags, 2, 128 };
        IrInlineResult result = { 0 };
        if (ir_inline_test_run(test, &unit, ir_inline_options(), output, twin, scratch, &result))
        {
            const IrFunction* out = &result.functions[0];
            uint32_t permutes = 0;
            uint32_t chained = 0;
            uint32_t slots_valid = 0;
            for (uint32_t i = 0; i < out->instruction_count; i += 1)
            {
                if (ir_ternary_instruction(out, i))
                {
                    permutes += 1;
                    const uint32_t third = ir_intrinsic_third(out, i);
                    slots_valid += ir_intrinsic_extra(out->c[i]) < out->extra_count && third < i && out->type[third] == IR_V16I32;
                    chained += out->op[third] == IR_INTRINSIC && ir_intrinsic_ternary(out->c[third]);
                }
            }
            ir_inline_test_expect(test, result.inlined_calls == 1 && ir_inline_simd_count(out, IR_CALL) == 0 && permutes == 3 && slots_valid == 3 && chained == 1,
                                  "an inlined permute2's third operand is remapped into a slot of the caller's");
        }
        arena_pop_to(output, 0);
        arena_pop_to(twin, 0);
    }
    arena_pop_to(input, 0);
}

// One more line of a fixture's text, or nothing once it has overflowed (the caller checks the length).
static void ir_inline_assembled_line(char* text, uint64_t capacity, uint64_t* length, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int written = *length < capacity ? vsnprintf(text + *length, capacity - *length, format, arguments) : -1;
    va_end(arguments);
    *length = written >= 0 && (uint64_t)written < capacity - *length ? *length + (uint64_t)written : capacity;
}

// #1344's fixtures: a vector assembled in memory, as the typed stream initializes a vector compound literal (clang's
// _mm512_set1_epi8, _mm512_setzero_si512 and _mm512_set_epi64 are such literals): each element's store into the
// literal's object, then one load of the whole vector, here stored at p + 64. SROA's #1282 options make the load the
// value the stores wrote where the block's stores decide it, and promotion then leaves no slot:
// - kind 0, a splat: 64 bytes, each a load of the parameter's object (the typed stream reads a local again for each
//   use), and a second load of 16 of the object's bytes, stored at p + 128;
// - kind 1, a constant: the object zeroed by quadwords, then eight quadword constants;
// - kind 2, quadwords with the parameter's object written between the fourth and the fifth load: two scalars, so the
//   vector stays in memory;
// - kind 3, quadwords of which the first is another scalar: memory too.
// Each runs on the interpreter before SROA and after promotion, on both twins, against the bytes computed here.
static void ir_inline_test_assembled(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    for (uint32_t kind = 0; kind < 4; kind += 1)
    {
        enum { CAPACITY = 16384 };
        char text[CAPACITY];
        uint64_t length = 0;
        uint32_t next = 0;
        ir_inline_assembled_line(text, CAPACITY, &length, "function (ptr) -> void {\n  slot $0 size=8 align=8 type=void flags=0x01\n  slot $1 size=64 align=64 type=void flags=0x01\nb0:\n");
        ir_inline_assembled_line(text, CAPACITY, &length, "  %%0 = stack_addr.ptr $0\n  %%1 = stack_addr.ptr $1\n  %%2 = param.ptr 0\n");
        next = 3;
        const uint32_t lanes = kind == 0 ? 64 : 8;
        const char* lane_type = kind == 0 ? "i8" : "i64";
        const uint32_t lane_bytes = kind == 0 ? 1 : 8;
        uint32_t second = BUSTER_IR_NONE;
        if (kind == 1)
        {
            // The zeroing a literal's object starts with, which the element stores then cover.
            ir_inline_assembled_line(text, CAPACITY, &length, "  %%%u = const.i64 0x00000000000000000000000000000000\n", next);
            const uint32_t zero = next++;
            for (uint32_t lane = 0; lane < 8; lane += 1)
            {
                ir_inline_assembled_line(text, CAPACITY, &length, "  %%%u = address.ptr %%1 offset=%u\n  %%%u = store.void %%%u, %%%u align=8\n", next, lane * 8, next + 1, next, zero);
                next += 2;
            }
        }
        else
        {
            // The parameter's object: p's first scalar.
            ir_inline_assembled_line(text, CAPACITY, &length, "  %%%u = load.%s %%2 align=1\n  %%%u = store.void %%0, %%%u align=1\n", next, lane_type, next + 1, next);
            next += 2;
            if (kind == 3)
            {
                ir_inline_assembled_line(text, CAPACITY, &length, "  %%%u = address.ptr %%2 offset=8\n  %%%u = load.i64 %%%u align=1\n", next, next + 1, next);
                second = next + 1;
                next += 2;
            }
        }
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            if (kind == 2 && lane == 4)
            {
                ir_inline_assembled_line(text, CAPACITY, &length, "  %%%u = address.ptr %%2 offset=8\n  %%%u = load.i64 %%%u align=1\n  %%%u = store.void %%0, %%%u align=1\n", next, next + 1, next, next + 2,
                                         next + 1);
                next += 3;
            }
            uint32_t value = next;
            if (kind == 1)
            {
                ir_inline_assembled_line(text, CAPACITY, &length, "  %%%u = const.i64 0x0000000000000000%016" PRIx64 "\n", next, UINT64_C(0x0123456789abcdef) * (lane + 1));
                next += 1;
            }
            else if (kind == 3 && lane == 0)
            {
                value = second;
            }
            else
            {
                ir_inline_assembled_line(text, CAPACITY, &length, "  %%%u = load.%s %%0 align=1\n", next, lane_type);
                next += 1;
            }
            ir_inline_assembled_line(text, CAPACITY, &length, "  %%%u = address.ptr %%1 offset=%u\n  %%%u = store.void %%%u, %%%u align=1\n", next, lane * lane_bytes, next + 1, next, value);
            next += 2;
        }
        ir_inline_assembled_line(text, CAPACITY, &length, "  %%%u = load.v16i32 %%1 align=64\n  %%%u = address.ptr %%2 offset=64\n  %%%u = store.void %%%u, %%%u align=1\n", next, next + 1, next + 2, next + 1, next);
        next += 3;
        if (kind == 0)
        {
            ir_inline_assembled_line(text, CAPACITY, &length, "  %%%u = address.ptr %%1 offset=16\n  %%%u = load.v4i32 %%%u align=16\n  %%%u = address.ptr %%2 offset=128\n  %%%u = store.void %%%u, %%%u align=1\n", next,
                                     next + 1, next, next + 2, next + 3, next + 2, next + 1);
            next += 4;
        }
        ir_inline_assembled_line(text, CAPACITY, &length, "  %%%u = ret.void\n}\n", next);
        ir_inline_test_expect(test, length < CAPACITY, "an assembled vector's fixture fits its text");
        IrFunction function;
        if (length < CAPACITY && ir_inline_simd_parse(test, text, input, scratch, &function))
        {
            uint8_t seed[128];
            for (uint32_t i = 0; i < sizeof(seed); i += 1)
            {
                seed[i] = (uint8_t)ir_inline_test_random(test);
            }
            uint8_t expected[192];
            memcpy(expected, seed, 128);
            memset(expected + 128, 0x5a, 64);
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                const uint64_t constant = UINT64_C(0x0123456789abcdef) * (lane + 1);
                const uint8_t* source = kind == 1 ? (const uint8_t*)&constant : kind == 2 && lane >= 4 ? seed + 8 : kind == 3 && lane == 0 ? seed + 8 : seed;
                memcpy(expected + 64 + lane * lane_bytes, source, lane_bytes);
            }
            if (kind == 0)
            {
                memset(expected + 128, seed[0], 16);
            }
            uint8_t bytes[192];
            ir_inline_test_expect(test, ir_inline_simd_run(&function, 1, seed, bytes, scratch) && memcmp(bytes, expected, sizeof(bytes)) == 0, "an assembled vector's fixture runs as its lanes say");
            for (uint32_t path = 0; path < 2; path += 1)
            {
                IrSroaOptions sroa = ir_sroa_options();
                sroa.scalar = path == 1;
                sroa.simd = true;
                sroa.flagged = true;
                IrSroaResult split = { 0 };
                const IrCheck checked = ir_optimize_sroa(&function, output, scratch, sroa, &split);
                IrFunction promoted = { 0 };
                const IrCheck promotion = checked.error == IR_OK ? (path == 1 ? ssa_ir_promote_scalar(&split.function, twin, scratch, &promoted) : ssa_ir_promote(&split.function, twin, scratch, &promoted)) : checked;
                ir_inline_test_expect(test, checked.error == IR_OK && promotion.error == IR_OK, "SROA and promotion take an assembled vector's function");
                if (checked.error == IR_OK && promotion.error == IR_OK)
                {
                    ir_inline_test_expect(test, split.assembled == (kind == 0 ? 2u : kind == 1 ? 1u : 0u), "the block's stores decide a vector load where they are constants or one scalar, and only there");
                    if (kind == 0)
                    {
                        // The parameter's load, two splats of it and their stores.
                        ir_inline_test_expect(test, promoted.slot_count == 0 && ir_inline_simd_count(&promoted, IR_LOAD) == 1 && ir_inline_simd_count(&promoted, IR_STORE) == 2 &&
                                                  ir_inline_simd_count(&promoted, IR_INTRINSIC) == 2,
                                              "a splat's 64 stores and its loads are two splats of the scalar");
                    }
                    else if (kind == 1)
                    {
                        ir_inline_test_expect(test, promoted.slot_count == 0 && ir_inline_simd_count(&promoted, IR_LOAD) == 0 && ir_inline_simd_count(&promoted, IR_STORE) == 1 &&
                                                  ir_inline_simd_count(&promoted, IR_VCONST) == 1 && promoted.vector_constant_count == 1 &&
                                                  memcmp(promoted.vector_constants[0].words, expected + 64, 64) == 0,
                                              "a zeroed object's constant stores are one vector constant of their bytes");
                    }
                    else
                    {
                        ir_inline_test_expect(test, ir_inline_simd_count(&promoted, IR_VCONST) == 0 && ir_inline_simd_count(&promoted, IR_INTRINSIC) == 0, "two scalars' lanes stay stores and a load");
                    }
                    ir_inline_test_expect(test, ir_inline_simd_run(&promoted, 1, seed, bytes, scratch) && memcmp(bytes, expected, sizeof(bytes)) == 0, "the promoted function runs as the fixture");
                }
                arena_pop_to(twin, 0);
                arena_pop_to(output, 0);
            }
        }
        arena_pop_to(input, 0);
    }
}

static void ir_inline_test_simd(IrInlineTest* test, Arena* input, Arena* output, Arena* twin, Arena* scratch)
{
    ir_inline_test_by_value(test, input, output, twin, scratch);
    ir_inline_test_rows(test, input, output, twin, scratch);
    ir_inline_test_permute2(test, input, output, twin, scratch);
    ir_inline_test_assembled(test, input, output, twin, scratch);
}
