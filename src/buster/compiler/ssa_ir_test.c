// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Shared-IR promotion integration tests. Include after ir_lower_test.c and ssa_ir.c.
// Generated typed programs execute through Tier 0 and through both promoted IRs,
// comparing return bits and global memory. The test-only IR executor adapts the
// project's ir_lower_test interpreter with simultaneous edge-selected phis,
// switches and symbolic function addresses; no production interpreter is claimed.

typedef struct SsaIrTestTally SsaIrTestTally;
struct SsaIrTestTally
{
    uint64_t checks;
    uint64_t failures;
    uint64_t seed;
    uint32_t function;
};

static bool ssa_ir_test_expect(SsaIrTestTally* tally, bool condition, const char* description)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 20)
        {
            fprintf(stderr, "test-ssa-ir: %s (seed=%" PRIu64 ", function=%u)\n", description, tally->seed, tally->function);
        }
        tally->failures += 1;
    }
    return condition;
}

// Read every incoming value before replacing any phi result. Backedges can name
// another phi in this same prefix, including swaps, so sequential assignment fails.
static bool ssa_ir_test_enter(IrLowerTestFrame* frame, uint32_t previous, uint32_t next, uint64_t* incoming)
{
    const IrFunction* function = frame->function;
    bool ok = next < function->block_count;
    if (ok)
    {
        const uint32_t first = function->block_offsets[next];
        uint32_t end = first;
        while (ok && end < function->block_offsets[next + 1] && function->op[end] == IR_PHI)
        {
            const uint32_t offset = function->a[end];
            const uint32_t count = function->b[end];
            bool found = false;
            for (uint32_t e = 0; e < count; e += 1)
            {
                if (function->extra[offset + e * 2] == previous)
                {
                    ok = !found;
                    found = true;
                    incoming[end - first] = frame->values[function->extra[offset + e * 2 + 1]];
                }
            }
            ok = ok && found;
            end += 1;
        }
        for (uint32_t i = first; ok && i < end; i += 1)
        {
            frame->values[i] = incoming[i - first];
        }
        frame->instruction = end;
    }
    return ok;
}

#define BUSTER_SSA_IR_FUNCTION_BASE UINT64_C(0xffff000000000000)

// Only the adapter's <=64-bit types execute here. A malformed address, unsupported
// operation or exhausted step/frame budget reports a fault rather than executing
// an unchecked host load or making this test interpreter recurse.
static uint64_t ssa_ir_test_interpret(const IrFunction* functions, uint32_t function_count, uint32_t entry, uint32_t global_count, const Tier0TestData* data, uint8_t* globals, const uint64_t* arguments, uint32_t argument_count, Arena* arena, bool* fault)
{
    const uint64_t mark = arena->length;
    IrLowerTestFrame* frames = BUSTER_ARENA_PUSH_ARRAY(arena, IrLowerTestFrame, BUSTER_IR_LOWER_TEST_FRAMES);
    uint32_t* blocks = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_IR_LOWER_TEST_FRAMES);
    uint32_t maximum = 1;
    for (uint32_t f = 0; f < function_count; f += 1)
    {
        if (functions[f].instruction_count > maximum)
        {
            maximum = functions[f].instruction_count;
        }
    }
    uint64_t* incoming = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, maximum);
    bool ok = frames != 0 && blocks != 0 && incoming != 0 && entry < function_count;
    uint32_t depth = ok ? 1 : 0;
    uint32_t steps = 0;
    uint64_t result = 0;
    if (ok)
    {
        ok = ir_lower_test_frame(&frames[0], &functions[entry], arguments, argument_count, BUSTER_IR_NONE, arena);
        blocks[0] = 0;
        ok = ok && ssa_ir_test_enter(&frames[0], BUSTER_IR_NONE, 0, incoming);
    }
    while (ok && depth != 0 && steps < BUSTER_IR_LOWER_TEST_STEPS)
    {
        IrLowerTestFrame* frame = &frames[depth - 1];
        const IrFunction* function = frame->function;
        const uint32_t i = frame->instruction;
        steps += 1;
        if (i >= function->instruction_count)
        {
            ok = false;
        }
        else
        {
            frame->instruction += 1;
            const uint8_t op = function->op[i];
            const uint8_t type = function->type[i];
            const uint32_t a = function->a[i];
            const uint32_t b = function->b[i];
            const uint32_t c = function->c[i];
            const uint64_t av = a < function->instruction_count ? frame->values[a] : 0;
            const uint64_t bv = b < function->instruction_count ? frame->values[b] : 0;
            const uint8_t at = a < function->instruction_count ? function->type[a] : IR_VOID;
            uint64_t value = 0;
            bool has_value = type != IR_VOID;
            switch (op)
            {
            case IR_CONST:
                value = function->constants[a].low;
                break;
            case IR_UNDEF:
                value = 0;
                break;
            case IR_PARAM:
                value = frame->arguments[a];
                break;
            case IR_COPY:
            case IR_BITCAST:
            case IR_TRUNC:
            case IR_ZEXT:
            case IR_INTTOPTR:
            case IR_PTRTOINT:
                value = av;
                break;
            case IR_SEXT:
                value = (uint64_t)ir_lower_test_signed(at, av);
                break;
            case IR_ADD:
                value = av + bv;
                break;
            case IR_SUB:
                value = av - bv;
                break;
            case IR_MUL:
                value = av * bv;
                break;
            case IR_UDIV:
            case IR_UREM:
                ok = bv != 0;
                if (ok)
                {
                    value = op == IR_UDIV ? av / bv : av % bv;
                }
                break;
            case IR_SDIV:
            case IR_SREM:
            {
                const int64_t left = ir_lower_test_signed(type, av);
                const int64_t right = ir_lower_test_signed(type, bv);
                ok = right != 0 && !(left == INT64_MIN && right == -1);
                if (ok)
                {
                    value = (uint64_t)(op == IR_SDIV ? left / right : left % right);
                }
                break;
            }
            case IR_AND:
                value = av & bv;
                break;
            case IR_OR:
                value = av | bv;
                break;
            case IR_XOR:
                value = av ^ bv;
                break;
            case IR_SHL:
            case IR_LSHR:
            case IR_ASHR:
                ok = bv < ir_type_bits[type];
                if (ok)
                {
                    value = op == IR_SHL ? av << bv : op == IR_LSHR ? av >> bv : (uint64_t)(ir_lower_test_signed(type, av) >> bv);
                }
                break;
            case IR_NEG:
                value = 0 - av;
                break;
            case IR_NOT:
                value = ~av;
                break;
            case IR_FADD:
            case IR_FSUB:
            case IR_FMUL:
            case IR_FDIV:
            case IR_FNEG:
            {
                if (type == IR_F32)
                {
                    const float left = (float)ir_lower_test_float(type, av);
                    const float right = op == IR_FNEG ? 0 : (float)ir_lower_test_float(type, bv);
                    float floating = 0;
                    switch (op)
                    {
                    case IR_FADD: floating = left + right; break;
                    case IR_FSUB: floating = left - right; break;
                    case IR_FMUL: floating = left * right; break;
                    case IR_FDIV: floating = left / right; break;
                    default: floating = -left; break;
                    }
                    uint32_t bits = 0;
                    memcpy(&bits, &floating, sizeof(bits));
                    value = bits;
                }
                else
                {
                    const double left = ir_lower_test_float(type, av);
                    const double right = op == IR_FNEG ? 0 : ir_lower_test_float(type, bv);
                    double floating = 0;
                    switch (op)
                    {
                    case IR_FADD: floating = left + right; break;
                    case IR_FSUB: floating = left - right; break;
                    case IR_FMUL: floating = left * right; break;
                    case IR_FDIV: floating = left / right; break;
                    default: floating = -left; break;
                    }
                    value = ir_lower_test_float_bits(type, floating);
                }
                break;
            }
            case IR_FTRUNC:
            case IR_FEXT:
                value = ir_lower_test_float_bits(type, ir_lower_test_float(at, av));
                break;
            case IR_SITOFP:
            case IR_UITOFP:
                if (type == IR_F32)
                {
                    const float floating = op == IR_SITOFP ? (float)ir_lower_test_signed(at, av) : (float)av;
                    uint32_t bits = 0;
                    memcpy(&bits, &floating, sizeof(bits));
                    value = bits;
                }
                else
                {
                    value = ir_lower_test_float_bits(type, op == IR_SITOFP ? (double)ir_lower_test_signed(at, av) : (double)av);
                }
                break;
            case IR_FPTOSI:
            case IR_FPTOUI:
            {
                const long double floating = ir_lower_test_float(at, av);
                const uint32_t bits = ir_type_bits[type];
                const long double unsigned_limit = bits == 64 ? 18446744073709551616.0L : (long double)(1ull << bits);
                const long double signed_limit = (long double)(1ull << (bits - 1));
                const bool signed_value = op == IR_FPTOSI;
                const bool in_range = signed_value ? floating >= -signed_limit && floating < signed_limit : floating >= 0 && floating < unsigned_limit;
                if (in_range)
                {
                    value = signed_value ? (uint64_t)(int64_t)floating : (uint64_t)floating;
                }
                else
                {
                    // Generated streams may exceed a conversion's range even with defined=true.
                    // Choose Tier 0's CVTT intermediate/truncation convention for those otherwise
                    // undefined results. Representable conversions above retain IR signedness.
                    const bool wide = bits == 64 || (bits == 32 && !signed_value);
                    const long double limit = wide ? 9223372036854775808.0L : 2147483648.0L;
                    value = floating >= -limit && floating < limit ? (uint64_t)(int64_t)floating : wide ? 1ull << 63 : 1ull << 31;
                }
                break;
            }
            case IR_ICMP:
            {
                const int64_t left = ir_lower_test_signed(at, av);
                const int64_t right = ir_lower_test_signed(at, bv);
                switch (c)
                {
                case IR_EQ: value = av == bv; break;
                case IR_NE: value = av != bv; break;
                case IR_LT: value = left < right; break;
                case IR_LE: value = left <= right; break;
                case IR_GT: value = left > right; break;
                case IR_GE: value = left >= right; break;
                case IR_ULT: value = av < bv; break;
                case IR_ULE: value = av <= bv; break;
                case IR_UGT: value = av > bv; break;
                case IR_UGE: value = av >= bv; break;
                default: ok = false; break;
                }
                break;
            }
            case IR_FCMP:
            {
                const double left = ir_lower_test_float(at, av);
                const double right = ir_lower_test_float(at, bv);
                switch (c)
                {
                case IR_EQ: value = left == right; break;
                case IR_NE: value = left != right; break;
                case IR_LT: value = left < right; break;
                case IR_LE: value = left <= right; break;
                case IR_GT: value = left > right; break;
                case IR_GE: value = left >= right; break;
                default: ok = false; break;
                }
                break;
            }
            case IR_SELECT:
                value = av != 0 ? bv : frame->values[c];
                break;
            case IR_STACK_ADDR:
                value = frame->slots[a];
                break;
            case IR_SYMBOL_ADDR:
                ok = a != 0 && a <= global_count + function_count;
                if (ok && a <= global_count)
                {
                    value = (uintptr_t)(globals + data->offsets[a - 1]);
                }
                else if (ok)
                {
                    value = BUSTER_SSA_IR_FUNCTION_BASE | a;
                }
                break;
            case IR_ADDRESS:
                value = av + (b == BUSTER_IR_NONE ? 0 : bv * function->addresses[c].scale) + (uint64_t)function->addresses[c].offset;
                break;
            case IR_LOAD:
            case IR_STORE:
            {
                const uint8_t memory_type = op == IR_LOAD ? type : function->type[b];
                const uint32_t bytes = (ir_type_bits[memory_type] + 7) / 8;
                ok = bytes <= 8 && ir_lower_test_memory(frames, depth, globals, data->size, av, bytes);
                if (ok)
                {
                    if (op == IR_LOAD)
                    {
                        memcpy(&value, (const void*)(uintptr_t)av, bytes);
                    }
                    else
                    {
                        memcpy((void*)(uintptr_t)av, &bv, bytes);
                    }
                }
                break;
            }
            case IR_CALL:
            case IR_CALL_INDIRECT:
            {
                const IrCall* call = &function->calls[b];
                const uint32_t symbol = op == IR_CALL ? a : (uint32_t)av;
                const uint32_t callee = symbol - global_count - 1;
                uint64_t passed[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
                ok = (op == IR_CALL || (av & UINT64_C(0xffffffff00000000)) == BUSTER_SSA_IR_FUNCTION_BASE) && symbol > global_count && callee < function_count && depth < BUSTER_IR_LOWER_TEST_FRAMES && call->argument_count <= BUSTER_TIER0_MAX_CALL_ARGUMENTS;
                for (uint32_t p = 0; ok && p < call->argument_count; p += 1)
                {
                    passed[p] = frame->values[function->extra[call->argument_first + p]];
                }
                if (ok)
                {
                    ok = ir_lower_test_frame(&frames[depth], &functions[callee], passed, call->argument_count, i, arena);
                    blocks[depth] = 0;
                    ok = ok && ssa_ir_test_enter(&frames[depth], BUSTER_IR_NONE, 0, incoming);
                    depth += ok;
                }
                has_value = false;
                break;
            }
            case IR_BR:
            case IR_CBR:
            case IR_SWITCH:
            {
                uint32_t target = op == IR_BR ? a : av != 0 ? b : c;
                if (op == IR_SWITCH)
                {
                    target = function->extra[b];
                    for (uint32_t e = 0; e < c; e += 1)
                    {
                        const uint32_t constant = function->extra[b + e * 2 + 1];
                        if (function->constants[constant].low == av)
                        {
                            target = function->extra[b + e * 2 + 2];
                        }
                    }
                }
                ok = ssa_ir_test_enter(frame, blocks[depth - 1], target, incoming);
                blocks[depth - 1] = target;
                break;
            }
            case IR_RET:
            {
                const uint32_t destination = frame->destination;
                const uint64_t returned = a == BUSTER_IR_NONE ? 0 : av;
                arena_pop_to(arena, frame->arena_mark);
                depth -= 1;
                if (depth == 0)
                {
                    result = returned;
                }
                else
                {
                    frames[depth - 1].values[destination] = returned;
                }
                break;
            }
            default:
                ok = false;
                break;
            }
            if (ok && has_value)
            {
                frame->values[i] = ir_lower_test_bits(type, value);
            }
            if (!ok)
            {
                fprintf(stderr, "  SSA IR execution fault: function %td instruction %u %s.%s, a=%016" PRIx64 ", b=%016" PRIx64 "\n",
                        function - functions, i, ir_op_names[op], ir_type_names[type], av, bv);
            }
        }
    }
    *fault = !ok || depth != 0;
    arena_pop_to(arena, mark);
    return result;
}

static uint64_t ssa_ir_test_hash_bytes(uint64_t hash, const void* pointer, uint64_t size)
{
    const uint8_t* bytes = pointer;
    for (uint64_t i = 0; i < size; i += 1)
    {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t ssa_ir_test_hash(const IrFunction* function)
{
    const void* pointers[] = {
        function,
        function->op,
        function->type,
        function->flags,
        function->alignment_log2,
        function->a,
        function->b,
        function->c,
        function->block_offsets,
        function->extra,
        function->constants,
        function->slots,
        function->addresses,
        function->calls,
        function->call_types,
        function->parameter_types,
    };
    const uint64_t sizes[] = {
        sizeof(*function),
        function->instruction_count,
        function->instruction_count,
        function->instruction_count,
        function->instruction_count,
        (uint64_t)function->instruction_count * 4,
        (uint64_t)function->instruction_count * 4,
        (uint64_t)function->instruction_count * 4,
        ((uint64_t)function->block_count + 1) * 4,
        (uint64_t)function->extra_count * 4,
        (uint64_t)function->constant_count * sizeof(IrConstant),
        (uint64_t)function->slot_count * sizeof(IrSlot),
        (uint64_t)function->address_count * sizeof(IrAddress),
        (uint64_t)function->call_count * sizeof(IrCall),
        function->call_type_count,
        function->parameter_count,
    };
    uint64_t hash = UINT64_C(14695981039346656037);
    for (uint32_t i = 0; i < sizeof(pointers) / sizeof(pointers[0]); i += 1)
    {
        hash = ssa_ir_test_hash_bytes(hash, pointers[i], sizes[i]);
    }
    return hash;
}

static uint32_t ssa_ir_test_count(const IrFunction* function, IrOp op)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < function->instruction_count; i += 1)
    {
        count += function->op[i] == op;
    }
    return count;
}

static bool ssa_ir_test_promote(SsaIrTestTally* tally, const IrFunction* input, Arena* output, Arena* twin_output, Arena* scratch, IrFunction* result, IrFunction* twin)
{
    const uint64_t hash = ssa_ir_test_hash(input);
    const uint64_t mark = scratch->length;
    const IrCheck vector = ssa_ir_promote(input, output, scratch, result);
    bool ok = ssa_ir_test_expect(tally, scratch->length == mark, "vector restores scratch");
    const IrCheck scalar = ssa_ir_promote_scalar(input, twin_output, scratch, twin);
    ok = ssa_ir_test_expect(tally, scratch->length == mark, "scalar restores scratch") && ok;
    ok = ssa_ir_test_expect(tally, ssa_ir_test_hash(input) == hash, "promotion leaves input columns and metadata unchanged") && ok;
    ok = ssa_ir_test_expect(tally, vector.error == IR_OK && scalar.error == IR_OK, "both promotion paths accept") && ok;
    if (!ok && tally->failures < 20)
    {
        fprintf(stderr, "  vector error=%u instruction=%u detail=%u; scalar error=%u instruction=%u detail=%u\n",
                vector.error, vector.instruction, vector.detail, scalar.error, scalar.instruction, scalar.detail);
    }
    if (ok)
    {
        ok = ssa_ir_test_expect(tally, vector.dominance_checked && scalar.dominance_checked, "both promotion paths check real dominance");
        ok = ssa_ir_test_expect(tally, ir_verify(result, scratch, 0).error == IR_OK && ir_verify_scalar(result, scratch, 0).error == IR_OK &&
                                      ir_verify(twin, scratch, 0).error == IR_OK && ir_verify_scalar(twin, scratch, 0).error == IR_OK,
                                 "both IR verifiers accept both outputs") && ok;
    }
    return ok;
}

// Generated programs executed as typed streams, all-memory IR and both promoted forms. With `objects`, the generator
// adds frame objects (arrays and structs, some of twin layouts) and COPY between them, which stay memory through
// promotion while the locals around them are promoted.
static void ssa_ir_test_programs(SsaIrTestTally* tally, uint64_t seed, uint32_t count, bool objects)
{
    Tier0Tables tables = {0};
    Arena input = arena_reserve(1ull << 30);
    Arena lowered = arena_reserve(1ull << 30);
    Arena output = arena_reserve(1ull << 30);
    Arena twin_output = arena_reserve(1ull << 30);
    Arena scratch = arena_reserve(1ull << 30);
    Arena execution = arena_reserve(1ull << 30);
    bool ready = input.base != 0 && lowered.base != 0 && output.base != 0 && twin_output.base != 0 && scratch.base != 0 && execution.base != 0 && tier0_tables_open(&tables);
    ready = ssa_ir_test_expect(tally, ready, "generated-program arenas and ABI tables");
    uint64_t functions_seen = 0;
    uint64_t before = 0;
    uint64_t after = 0;
    uint64_t slots_before = 0;
    uint64_t slots_after = 0;
    uint64_t phis = 0;
    for (uint32_t p = 0; ready && p < count; p += 1)
    {
        tally->seed = seed + p;
        Tier0Program* program = BUSTER_ARENA_PUSH_ARRAY(&input, Tier0Program, 1);
        Tier0Functions* sources = BUSTER_ARENA_PUSH_ARRAY(&input, Tier0Functions, 1);
        IrFunction* memory = BUSTER_ARENA_PUSH_ARRAY(&input, IrFunction, BUSTER_TIER0_GENERATE_MAX_FUNCTIONS);
        IrFunction* promoted = BUSTER_ARENA_PUSH_ARRAY(&input, IrFunction, BUSTER_TIER0_GENERATE_MAX_FUNCTIONS);
        IrFunction* twin = BUSTER_ARENA_PUSH_ARRAY(&input, IrFunction, BUSTER_TIER0_GENERATE_MAX_FUNCTIONS);
        uint8_t* globals = BUSTER_ARENA_PUSH_ARRAY(&input, uint8_t, BUSTER_TIER0_GENERATE_MAX_GLOBALS * BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS * 8 + 64);
        uint8_t* expected_globals = BUSTER_ARENA_PUSH_ARRAY(&input, uint8_t, BUSTER_TIER0_GENERATE_MAX_GLOBALS * BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS * 8 + 64);
        const Tier0ProgramOptions options = {
            .functions = 8,
            .statements = 40,
            .expression_nodes = 30,
            .calls = true,
            .loops = true,
            .floating = true,
            .pointers = true,
            .defined = true,
            .objects = objects,
        };
        bool ok = program != 0 && sources != 0 && memory != 0 && promoted != 0 && twin != 0 && globals != 0 && expected_globals != 0;
        ok = ok && tier0_generate_program(program, &input, tally->seed, options, "ssa_ir_") && tier0_program_functions(program, &tables, &input, sources);
        ok = ssa_ir_test_expect(tally, ok, "generated typed program");
        for (uint32_t f = 0; ok && f < sources->count; f += 1)
        {
            tally->function = f;
            const IrType result_type = (IrType)ir_typed_types[program->functions[f].result_class];
            const IrCheck check = ir_lower_typed(&sources->functions[f], result_type, program->global_count + program->function_count + 1, &lowered, &scratch, &memory[f]);
            ok = ssa_ir_test_expect(tally, check.error == IR_OK, "actual Tier 0 stream lowers to shared IR");
            if (ok)
            {
                ok = ssa_ir_test_promote(tally, &memory[f], &output, &twin_output, &scratch, &promoted[f], &twin[f]);
            }
            if (ok)
            {
                functions_seen += 1;
                before += memory[f].instruction_count;
                after += promoted[f].instruction_count;
                slots_before += memory[f].slot_count;
                slots_after += promoted[f].slot_count;
                phis += ssa_ir_test_count(&promoted[f], IR_PHI);
            }
        }
        if (ok)
        {
            const Tier0TestData data = tier0_test_layout(program);
            uint64_t arguments[BUSTER_TIER0_TEST_INPUTS][3];
            tier0_test_inputs(tally->seed, arguments);
            for (uint32_t r = 0; ok && r < BUSTER_TIER0_TEST_INPUTS; r += 1)
            {
                bool source_fault = false;
                tier0_test_initial(program, &data, globals);
                const uint64_t expected = tier0_test_interpret(program, sources, &data, globals, arguments[r], &source_fault);
                memcpy(expected_globals, globals, data.size);
                const IrFunction* versions[] = {
                    memory,
                    promoted,
                    twin,
                };
                for (uint32_t v = 0; ok && v < 3; v += 1)
                {
                    bool fault = false;
                    tier0_test_initial(program, &data, globals);
                    const uint64_t actual = ssa_ir_test_interpret(versions[v], sources->count, sources->count - 1, program->global_count, &data, globals, arguments[r], 3, &execution, &fault);
                    ok = ssa_ir_test_expect(tally, !source_fault && !fault, "typed and IR execution finish");
                    ok = ssa_ir_test_expect(tally, expected == actual && memcmp(expected_globals, globals, data.size) == 0, "typed, memory IR and promoted IR return bits and global bytes agree") && ok;
                    if (!ok && tally->failures < 20)
                    {
                        fprintf(stderr, "  input=%u version=%u expected=%016" PRIx64 " actual=%016" PRIx64 " faults=%u/%u\n", r, v, expected, actual, source_fault, fault);
                    }
                }
            }
        }
        arena_pop_to(&input, 0);
        arena_pop_to(&lowered, 0);
        arena_pop_to(&output, 0);
        arena_pop_to(&twin_output, 0);
    }
    ssa_ir_test_expect(tally, functions_seen == (uint64_t)count * 8 && slots_after < slots_before && phis != 0, "generated path actually promotes locals and constructs phis");
    printf("SSA IR generated programs%s: %" PRIu64 " functions, instructions %" PRIu64 " -> %" PRIu64 ", slots %" PRIu64 " -> %" PRIu64 ", %" PRIu64 " phis\n", objects ? " with objects" : "", functions_seen, before, after,
           slots_before, slots_after, phis);
    tier0_tables_close(&tables);
    arena_release(&input);
    arena_release(&lowered);
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
    arena_release(&execution);
}

static bool ssa_ir_test_execute(SsaIrTestTally* tally, const IrFunction* functions, uint32_t count, uint32_t entry, const uint64_t* arguments, uint32_t argument_count, Arena* execution, uint64_t expected, const char* description)
{
    const Tier0TestData data = {0};
    uint8_t globals[1] = {0};
    bool fault = false;
    const uint64_t actual = ssa_ir_test_interpret(functions, count, entry, 0, &data, globals, arguments, argument_count, execution, &fault);
    const bool ok = ssa_ir_test_expect(tally, !fault && actual == expected, description);
    if (!ok && tally->failures < 20)
    {
        fprintf(stderr, "  expected=%016" PRIx64 " actual=%016" PRIx64 " fault=%u\n", expected, actual, fault);
    }
    return ok;
}

static void ssa_ir_test_memory(SsaIrTestTally* tally)
{
    Arena input = arena_reserve(1ull << 24);
    Arena output = arena_reserve(1ull << 24);
    Arena twin_output = arena_reserve(1ull << 24);
    Arena scratch = arena_reserve(1ull << 24);
    Arena execution = arena_reserve(1ull << 24);
    // Each row changes one reason a direct memory slot may or may not be promoted.
    typedef struct SsaIrTestPolicy SsaIrTestPolicy;
    struct SsaIrTestPolicy
    {
        uint8_t slot_type;
        uint8_t slot_flags;
        uint8_t load_type;
        uint8_t store_type;
        uint8_t load_flags;
        uint8_t store_flags;
        uint8_t escape;
        bool promote;
    };
    const SsaIrTestPolicy policies[] = {
        {
            .slot_type = IR_I64,
            .load_type = IR_I64,
            .store_type = IR_I64,
            .promote = true,
        },
        {
            .slot_type = IR_I64,
            .slot_flags = BUSTER_IR_SLOT_ADDRESS_TAKEN,
            .load_type = IR_I64,
            .store_type = IR_I64,
        },
        {
            .slot_type = IR_I64,
            .slot_flags = BUSTER_IR_SLOT_VOLATILE,
            .load_type = IR_I64,
            .store_type = IR_I64,
        },
        {
            .slot_type = IR_I64,
            .load_type = IR_I64,
            .store_type = IR_I64,
            .load_flags = BUSTER_IR_VOLATILE,
        },
        {
            .slot_type = IR_I64,
            .load_type = IR_I64,
            .store_type = IR_I64,
            .store_flags = BUSTER_IR_VOLATILE,
        },
        {
            .slot_type = IR_I64,
            .slot_flags = BUSTER_IR_SLOT_TEMPORARY,
            .load_type = IR_I64,
            .store_type = IR_I64,
            .promote = true,
        },
        {
            .slot_type = IR_VOID,
            .load_type = IR_I64,
            .store_type = IR_I64,
        },
        {
            .slot_type = IR_VOID,
            .slot_flags = BUSTER_IR_SLOT_TEMPORARY,
            .load_type = IR_I64,
            .store_type = IR_I64,
        },
        {
            .slot_type = IR_I64,
            .load_type = IR_I32,
            .store_type = IR_I64,
        },
        {
            .slot_type = IR_I64,
            .load_type = IR_I64,
            .store_type = IR_I32,
        },
        {
            .slot_type = IR_I64,
            .load_type = IR_I64,
            .store_type = IR_I64,
            .escape = IR_COPY,
        },
        {
            .slot_type = IR_I64,
            .load_type = IR_I64,
            .store_type = IR_I64,
            .escape = IR_ADDRESS,
        },
        {
            .slot_type = IR_I8,
            .load_type = IR_I8,
            .store_type = IR_I8,
            .promote = true,
        },
        {
            .slot_type = IR_I16,
            .load_type = IR_I16,
            .store_type = IR_I16,
            .promote = true,
        },
        {
            .slot_type = IR_I32,
            .load_type = IR_I32,
            .store_type = IR_I32,
            .promote = true,
        },
        {
            .slot_type = IR_F32,
            .load_type = IR_F32,
            .store_type = IR_F32,
            .promote = true,
        },
        {
            .slot_type = IR_F64,
            .load_type = IR_F64,
            .store_type = IR_F64,
            .promote = true,
        },
        {
            .slot_type = IR_PTR,
            .load_type = IR_PTR,
            .store_type = IR_PTR,
            .promote = true,
        },
    };
    bool ready = ssa_ir_test_expect(tally, input.base != 0 && output.base != 0 && twin_output.base != 0 && scratch.base != 0 && execution.base != 0, "memory-policy arenas");
    for (uint32_t p = 0; ready && p < sizeof(policies) / sizeof(policies[0]); p += 1)
    {
        tally->function = p;
        const SsaIrTestPolicy policy = policies[p];
        const uint32_t load = policy.escape == 0 ? 3 : 4;
        const IrSizes sizes = {
            .instruction_count = load + 2,
            .block_count = 1,
            .constant_count = 1,
            .slot_count = 1,
            .address_count = policy.escape == IR_ADDRESS,
        };
        IrFunction function = {0};
        IrFunction promoted = {0};
        IrFunction twin = {0};
        bool ok = ssa_ir_test_expect(tally, ir_create(&input, &sizes, &function) == IR_OK, "allocate memory-policy fixture");
        if (ok)
        {
            function.result_type = policy.load_type;
            function.symbol_count = 1;
            function.block_offsets[1] = load + 2;
            function.constants[0].low = 7;
            function.slots[0] = (IrSlot){
                .size = 8,
                .alignment = 8,
                .type = policy.slot_type,
                .flags = policy.slot_flags,
            };
            ir_test_instruction(&function, 0, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 1, IR_CONST, policy.store_type, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&function, 2, IR_STORE, IR_VOID, 0, 1, BUSTER_IR_NONE);
            function.flags[2] = policy.store_flags;
            function.alignment_log2[2] = 3;
            if (policy.escape == IR_COPY)
            {
                ir_test_instruction(&function, 3, IR_COPY, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            else if (policy.escape == IR_ADDRESS)
            {
                ir_test_instruction(&function, 3, IR_ADDRESS, IR_PTR, 0, BUSTER_IR_NONE, 0);
                function.addresses[0].scale = 1;
            }
            ir_test_instruction(&function, load, IR_LOAD, policy.load_type, policy.escape == 0 ? 0 : 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
            function.flags[load] = policy.load_flags;
            function.alignment_log2[load] = 3;
            ir_test_instruction(&function, load + 1, IR_RET, IR_VOID, load, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ok = ssa_ir_test_promote(tally, &function, &output, &twin_output, &scratch, &promoted, &twin);
        }
        const IrFunction* versions[] = {
            &promoted,
            &twin,
        };
        for (uint32_t v = 0; ok && v < 2; v += 1)
        {
            const IrFunction* result = versions[v];
            const uint32_t retained = !policy.promote;
            ssa_ir_test_expect(tally, result->slot_count == retained && ssa_ir_test_count(result, IR_STACK_ADDR) == retained &&
                                     ssa_ir_test_count(result, IR_STORE) == retained && ssa_ir_test_count(result, IR_LOAD) == retained,
                               "slot policy preserves memory exclusions and promotes eligible scalars");
            for (uint32_t i = 0; i < result->instruction_count; i += 1)
            {
                if (result->op[i] == IR_LOAD || result->op[i] == IR_STORE)
                {
                    const uint8_t flags = result->op[i] == IR_LOAD ? policy.load_flags : policy.store_flags;
                    ssa_ir_test_expect(tally, result->flags[i] == flags && result->alignment_log2[i] == 3, "retained accesses preserve flags and alignment");
                }
            }
            const uint64_t arguments[1] = {0};
            ssa_ir_test_execute(tally, result, 1, 0, arguments, 0, &execution, 7, "memory-policy result is preserved");
        }
        arena_pop_to(&input, 0);
        arena_pop_to(&output, 0);
        arena_pop_to(&twin_output, 0);
    }
    // The index is promoted, while an untyped backing allocation stays in memory.
    // Both value columns of ADDRESS and the retained STACK_ADDR slot ID must move.
    const IrSizes address_sizes = {
        .instruction_count = 10,
        .block_count = 1,
        .constant_count = 2,
        .slot_count = 2,
        .address_count = 1,
    };
    IrFunction address = {0};
    IrFunction promoted = {0};
    IrFunction twin = {0};
    if (ready && ssa_ir_test_expect(tally, ir_create(&input, &address_sizes, &address) == IR_OK, "allocate indexed-memory fixture"))
    {
        address.result_type = IR_I64;
        address.symbol_count = 1;
        address.block_offsets[1] = 10;
        address.constants[0].low = 1;
        address.constants[1].low = 77;
        address.slots[0] = (IrSlot){
            .size = 8,
            .alignment = 8,
            .type = IR_I64,
        };
        address.slots[1] = (IrSlot){
            .size = 16,
            .alignment = 8,
            .type = IR_VOID,
        };
        address.addresses[0].scale = 8;
        ir_test_instruction(&address, 0, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&address, 1, IR_STACK_ADDR, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&address, 2, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&address, 3, IR_STORE, IR_VOID, 0, 2, BUSTER_IR_NONE);
        ir_test_instruction(&address, 4, IR_LOAD, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&address, 5, IR_ADDRESS, IR_PTR, 1, 4, 0);
        ir_test_instruction(&address, 6, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&address, 7, IR_STORE, IR_VOID, 5, 6, BUSTER_IR_NONE);
        ir_test_instruction(&address, 8, IR_LOAD, IR_I64, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&address, 9, IR_RET, IR_VOID, 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
        if (ssa_ir_test_promote(tally, &address, &output, &twin_output, &scratch, &promoted, &twin))
        {
            const IrFunction* versions[] = {
                &promoted,
                &twin,
            };
            for (uint32_t v = 0; v < 2; v += 1)
            {
                ssa_ir_test_expect(tally, versions[v]->slot_count == 1 && versions[v]->slots[0].type == IR_VOID && ssa_ir_test_count(versions[v], IR_LOAD) == 1, "indexed backing memory remains and its scalar index is promoted");
                const uint64_t arguments[1] = {0};
                ssa_ir_test_execute(tally, versions[v], 1, 0, arguments, 0, &execution, 77, "indexed address survives value and slot remapping");
            }
        }
    }
    // STORE's pointer operand and stored-value operand can be the same address.
    // The latter is an escape and must not be hidden by classifying the former.
    const IrSizes self_store_sizes = {
        .instruction_count = 5,
        .block_count = 1,
        .slot_count = 1,
    };
    IrFunction self_store = {0};
    if (ready && ssa_ir_test_expect(tally, ir_create(&input, &self_store_sizes, &self_store) == IR_OK, "allocate self-address store fixture"))
    {
        self_store.result_type = IR_I1;
        self_store.block_offsets[1] = 5;
        self_store.slots[0] = (IrSlot){
            .size = 8,
            .alignment = 8,
            .type = IR_PTR,
        };
        ir_test_instruction(&self_store, 0, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&self_store, 1, IR_STORE, IR_VOID, 0, 0, BUSTER_IR_NONE);
        ir_test_instruction(&self_store, 2, IR_LOAD, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&self_store, 3, IR_ICMP, IR_I1, 2, 2, IR_EQ);
        ir_test_instruction(&self_store, 4, IR_RET, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        if (ssa_ir_test_promote(tally, &self_store, &output, &twin_output, &scratch, &promoted, &twin))
        {
            const IrFunction* versions[] = {
                &promoted,
                &twin,
            };
            for (uint32_t v = 0; v < 2; v += 1)
            {
                ssa_ir_test_expect(tally, versions[v]->slot_count == 1 && ssa_ir_test_count(versions[v], IR_STORE) == 1, "storing an address to itself keeps its storage");
                const uint64_t arguments[1] = {0};
                ssa_ir_test_execute(tally, versions[v], 1, 0, arguments, 0, &execution, 1, "self-address store executes with retained memory");
            }
        }
    }
    const IrSizes undefined_sizes = {
        .instruction_count = 3,
        .block_count = 1,
        .slot_count = 1,
    };
    IrFunction undefined = {0};
    if (ready && ssa_ir_test_expect(tally, ir_create(&input, &undefined_sizes, &undefined) == IR_OK, "allocate undefined-read fixture"))
    {
        undefined.result_type = IR_I64;
        undefined.block_offsets[1] = 3;
        undefined.slots[0] = (IrSlot){
            .size = 8,
            .alignment = 8,
            .type = IR_I64,
        };
        ir_test_instruction(&undefined, 0, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&undefined, 1, IR_LOAD, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(&undefined, 2, IR_RET, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        if (ssa_ir_test_promote(tally, &undefined, &output, &twin_output, &scratch, &promoted, &twin))
        {
            ssa_ir_test_expect(tally, promoted.slot_count == 0 && twin.slot_count == 0 && ssa_ir_test_count(&promoted, IR_UNDEF) == 1 && ssa_ir_test_count(&twin, IR_UNDEF) == 1, "uninitialized promoted reads produce a typed undef");
        }
    }
    arena_release(&input);
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
    arena_release(&execution);
}

static void ssa_ir_test_variable_operands(SsaIrTestTally* tally)
{
    Arena input = arena_reserve(1ull << 24);
    Arena output = arena_reserve(1ull << 24);
    Arena twin_output = arena_reserve(1ull << 24);
    Arena scratch = arena_reserve(1ull << 24);
    Arena execution = arena_reserve(1ull << 24);
    IrFunction functions[2] = {0};
    IrFunction promoted[2] = {0};
    IrFunction twin[2] = {0};
    const IrSizes callee_sizes = {
        .instruction_count = 4,
        .block_count = 1,
        .constant_count = 1,
        .parameter_count = 1,
    };
    const IrSizes caller_sizes = {
        .instruction_count = 31,
        .block_count = 8,
        .extra_count = 17,
        .constant_count = 7,
        .slot_count = 1,
        .call_count = 2,
        .call_type_count = 1,
        .parameter_count = 1,
    };
    bool ok = input.base != 0 && output.base != 0 && twin_output.base != 0 && scratch.base != 0 && execution.base != 0;
    ok = ssa_ir_test_expect(tally, ok && ir_create(&input, &callee_sizes, &functions[0]) == IR_OK && ir_create(&input, &caller_sizes, &functions[1]) == IR_OK, "allocate phi/call/switch fixture");
    if (ok)
    {
        IrFunction* callee = &functions[0];
        callee->result_type = IR_I64;
        callee->symbol_count = 3;
        callee->parameter_types[0] = IR_I64;
        callee->block_offsets[1] = 4;
        callee->constants[0].low = 1;
        ir_test_instruction(callee, 0, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(callee, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(callee, 2, IR_ADD, IR_I64, 0, 1, BUSTER_IR_NONE);
        ir_test_instruction(callee, 3, IR_RET, IR_VOID, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        IrFunction* caller = &functions[1];
        caller->result_type = IR_I64;
        caller->symbol_count = 3;
        caller->parameter_types[0] = IR_I64;
        const uint32_t offsets[] = {
            0,
            7,
            10,
            13,
            16,
            23,
            26,
            29,
            31,
        };
        memcpy(caller->block_offsets, offsets, sizeof(offsets));
        const uint64_t constants[] = {
            5,
            0,
            99,
            10,
            20,
            100,
            200,
        };
        for (uint32_t c = 0; c < 7; c += 1)
        {
            caller->constants[c].low = constants[c];
        }
        caller->slots[0] = (IrSlot){
            .size = 8,
            .alignment = 8,
            .type = IR_I64,
        };
        // The first phi includes a dead predecessor. The switch has two distinct
        // case values targeting one block, so phi coverage uses deduplicated edges.
        const uint32_t extra[] = {
            1,
            7,
            2,
            10,
            3,
            13,
            18,
            19,
            6,
            1,
            5,
            0,
            5,
            5,
            24,
            6,
            27,
        };
        memcpy(caller->extra, extra, sizeof(extra));
        caller->call_types[0] = IR_I64;
        for (uint32_t c = 0; c < 2; c += 1)
        {
            caller->calls[c] = (IrCall){
                .argument_first = 6 + c,
                .argument_count = 1,
                .type_first = 0,
                .parameter_count = 1,
                .result_type = IR_I64,
            };
        }
        ir_test_instruction(caller, 0, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 2, IR_STORE, IR_VOID, 0, 1, BUSTER_IR_NONE);
        ir_test_instruction(caller, 3, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 4, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 5, IR_ICMP, IR_I1, 3, 4, IR_NE);
        ir_test_instruction(caller, 6, IR_CBR, IR_VOID, 5, 2, 3);
        ir_test_instruction(caller, 7, IR_CONST, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 8, IR_STORE, IR_VOID, 0, 7, BUSTER_IR_NONE);
        ir_test_instruction(caller, 9, IR_BR, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 10, IR_CONST, IR_I64, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 11, IR_STORE, IR_VOID, 0, 10, BUSTER_IR_NONE);
        ir_test_instruction(caller, 12, IR_BR, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 13, IR_CONST, IR_I64, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 14, IR_STORE, IR_VOID, 0, 13, BUSTER_IR_NONE);
        ir_test_instruction(caller, 15, IR_BR, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 16, IR_PHI, IR_I64, 0, 3, BUSTER_IR_NONE);
        ir_test_instruction(caller, 17, IR_LOAD, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 18, IR_SELECT, IR_I64, 5, 17, 16);
        ir_test_instruction(caller, 19, IR_CALL, IR_I64, 1, 0, BUSTER_IR_NONE);
        ir_test_instruction(caller, 20, IR_SYMBOL_ADDR, IR_PTR, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 21, IR_CALL_INDIRECT, IR_I64, 20, 1, BUSTER_IR_NONE);
        ir_test_instruction(caller, 22, IR_SWITCH, IR_VOID, 3, 8, 2);
        ir_test_instruction(caller, 23, IR_CONST, IR_I64, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 24, IR_ADD, IR_I64, 21, 23, BUSTER_IR_NONE);
        ir_test_instruction(caller, 25, IR_BR, IR_VOID, 7, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 26, IR_CONST, IR_I64, 6, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 27, IR_ADD, IR_I64, 21, 26, BUSTER_IR_NONE);
        ir_test_instruction(caller, 28, IR_BR, IR_VOID, 7, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 29, IR_PHI, IR_I64, 13, 2, BUSTER_IR_NONE);
        ir_test_instruction(caller, 30, IR_RET, IR_VOID, 29, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t f = 0; ok && f < 2; f += 1)
        {
            tally->function = f;
            ok = ssa_ir_test_promote(tally, &functions[f], &output, &twin_output, &scratch, &promoted[f], &twin[f]);
        }
    }
    if (ok)
    {
        const IrFunction* versions[] = {
            functions,
            promoted,
            twin,
        };
        const uint64_t selectors[] = {
            0,
            5,
            7,
        };
        const uint64_t expected[] = {
            122,
            112,
            212,
        };
        for (uint32_t v = 0; v < 3; v += 1)
        {
            for (uint32_t r = 0; r < 3; r += 1)
            {
                ssa_ir_test_execute(tally, versions[v], 2, 1, &selectors[r], 1, &execution, expected[r], "phi/call/select/switch values and branch targets survive remapping");
            }
            if (v != 0)
            {
                const IrFunction* result = &versions[v][1];
                ssa_ir_test_expect(tally, result->block_count == 7 && result->slot_count == 0 && ssa_ir_test_count(result, IR_LOAD) == 0 &&
                                         ssa_ir_test_count(result, IR_STORE) == 0 && ssa_ir_test_count(result, IR_STACK_ADDR) == 0 && ssa_ir_test_count(result, IR_PHI) >= 2,
                                   "dead block and promoted memory disappear while existing phis survive");
            }
        }
    }
    // One extra word is simultaneously a call value and a switch block. Two
    // instructions also share the same IrCall record. Their output slices must
    // be rebuilt independently: the value and block acquire different new IDs.
    const IrSizes shared_sizes = {
        .instruction_count = 14,
        .block_count = 4,
        .extra_count = 3,
        .constant_count = 2,
        .slot_count = 1,
        .call_count = 1,
        .call_type_count = 1,
        .parameter_count = 1,
    };
    if (ok && ssa_ir_test_expect(tally, ir_create(&input, &shared_sizes, &functions[1]) == IR_OK, "allocate overlapping extra slices"))
    {
        IrFunction* caller = &functions[1];
        caller->result_type = IR_I64;
        caller->symbol_count = 3;
        caller->parameter_types[0] = IR_I64;
        caller->block_offsets[1] = 8;
        caller->block_offsets[2] = 10;
        caller->block_offsets[3] = 12;
        caller->block_offsets[4] = 14;
        caller->constants[0].low = 5;
        caller->constants[1].low = 99;
        caller->slots[0] = (IrSlot){
            .size = 8,
            .alignment = 8,
            .type = IR_I64,
        };
        caller->extra[0] = 3;
        caller->extra[1] = 0;
        caller->extra[2] = 2;
        caller->call_types[0] = IR_I64;
        caller->calls[0] = (IrCall){
            .argument_first = 0,
            .argument_count = 1,
            .type_first = 0,
            .parameter_count = 1,
            .result_type = IR_I64,
        };
        ir_test_instruction(caller, 0, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 2, IR_STORE, IR_VOID, 0, 1, BUSTER_IR_NONE);
        ir_test_instruction(caller, 3, IR_LOAD, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 4, IR_CALL, IR_I64, 1, 0, BUSTER_IR_NONE);
        ir_test_instruction(caller, 5, IR_CALL, IR_I64, 1, 0, BUSTER_IR_NONE);
        ir_test_instruction(caller, 6, IR_PARAM, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 7, IR_SWITCH, IR_VOID, 6, 0, 1);
        ir_test_instruction(caller, 8, IR_CONST, IR_I64, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 9, IR_RET, IR_VOID, 8, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 10, IR_ADD, IR_I64, 4, 5, BUSTER_IR_NONE);
        ir_test_instruction(caller, 11, IR_RET, IR_VOID, 10, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 12, IR_SUB, IR_I64, 4, 5, BUSTER_IR_NONE);
        ir_test_instruction(caller, 13, IR_RET, IR_VOID, 12, BUSTER_IR_NONE, BUSTER_IR_NONE);
        if (ssa_ir_test_promote(tally, caller, &output, &twin_output, &scratch, &promoted[1], &twin[1]))
        {
            const IrFunction* versions[] = {
                promoted,
                twin,
            };
            for (uint32_t v = 0; v < 2; v += 1)
            {
                const uint64_t match = 5;
                const uint64_t fallback = 7;
                ssa_ir_test_execute(tally, versions[v], 2, 1, &match, 1, &execution, 12, "shared call arguments and switch case remap independently");
                ssa_ir_test_execute(tally, versions[v], 2, 1, &fallback, 1, &execution, 0, "overlapping switch default and call value remap independently");
                ssa_ir_test_expect(tally, versions[v][1].call_count == 2 && versions[v][1].block_count == 3, "shared call record is rebuilt per kept call and dead target numbering compacts");
            }
        }
    }
    arena_pop_to(&input, 0);
    arena_pop_to(&output, 0);
    arena_pop_to(&twin_output, 0);
    // A local address passed in a variable call-argument slice escapes even if
    // every direct use in its defining function is an ordinary load or store.
    const IrSizes escape_callee_sizes = {
        .instruction_count = 4,
        .block_count = 1,
        .constant_count = 1,
        .parameter_count = 1,
    };
    const IrSizes escape_caller_sizes = {
        .instruction_count = 6,
        .block_count = 1,
        .extra_count = 1,
        .constant_count = 1,
        .slot_count = 1,
        .call_count = 1,
        .call_type_count = 1,
    };
    ok = ssa_ir_test_expect(tally, ir_create(&input, &escape_callee_sizes, &functions[0]) == IR_OK && ir_create(&input, &escape_caller_sizes, &functions[1]) == IR_OK, "allocate escaping-call fixture");
    if (ok)
    {
        IrFunction* callee = &functions[0];
        callee->result_type = IR_VOID;
        callee->symbol_count = 3;
        callee->parameter_types[0] = IR_PTR;
        callee->block_offsets[1] = 4;
        callee->constants[0].low = 55;
        ir_test_instruction(callee, 0, IR_PARAM, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(callee, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(callee, 2, IR_STORE, IR_VOID, 0, 1, BUSTER_IR_NONE);
        ir_test_instruction(callee, 3, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE);
        IrFunction* caller = &functions[1];
        caller->result_type = IR_I64;
        caller->symbol_count = 3;
        caller->block_offsets[1] = 6;
        caller->constants[0].low = 7;
        caller->slots[0] = (IrSlot){
            .size = 8,
            .alignment = 8,
            .type = IR_I64,
        };
        caller->extra[0] = 0;
        caller->call_types[0] = IR_PTR;
        caller->calls[0] = (IrCall){
            .argument_first = 0,
            .argument_count = 1,
            .type_first = 0,
            .parameter_count = 1,
            .result_type = IR_VOID,
        };
        ir_test_instruction(caller, 0, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 2, IR_STORE, IR_VOID, 0, 1, BUSTER_IR_NONE);
        ir_test_instruction(caller, 3, IR_CALL, IR_VOID, 1, 0, BUSTER_IR_NONE);
        ir_test_instruction(caller, 4, IR_LOAD, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_test_instruction(caller, 5, IR_RET, IR_VOID, 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        for (uint32_t f = 0; ok && f < 2; f += 1)
        {
            ok = ssa_ir_test_promote(tally, &functions[f], &output, &twin_output, &scratch, &promoted[f], &twin[f]);
        }
        const IrFunction* versions[] = {
            promoted,
            twin,
        };
        for (uint32_t v = 0; ok && v < 2; v += 1)
        {
            ssa_ir_test_expect(tally, versions[v][1].slot_count == 1 && ssa_ir_test_count(&versions[v][1], IR_LOAD) == 1, "call-argument escape retains local storage");
            const uint64_t arguments[1] = {0};
            ssa_ir_test_execute(tally, versions[v], 2, 1, arguments, 0, &execution, 55, "callee mutation remains visible after promotion");
        }
    }
    arena_release(&input);
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
    arena_release(&execution);
}

static void ssa_ir_test_failures(SsaIrTestTally* tally)
{
    Arena input = arena_reserve(1ull << 20);
    Arena output = arena_reserve(1ull << 20);
    Arena twin_output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    IrFunction function = {0};
    bool ready = input.base != 0 && output.base != 0 && twin_output.base != 0 && scratch.base != 0;
    ready = ssa_ir_test_expect(tally, ready && ir_test_simple(&input, &function), "allocate transactional failure fixture");
    if (ready)
    {
        uint8_t* output_prefix = arena_push(&output, 64, 1);
        uint8_t* scratch_prefix = arena_push(&scratch, 64, 1);
        memset(output_prefix, 0xa5, 64);
        memset(scratch_prefix, 0x5a, 64);
        const uint64_t output_mark = output.length;
        const uint64_t scratch_mark = scratch.length;
        const IrFunction sentinel = {
            .instruction_count = 123,
            .symbol_count = 456,
        };
        for (uint32_t v = 0; v < 2; v += 1)
        {
            for (uint32_t failure = 0; failure < 4; failure += 1)
            {
                Arena limited_output = output;
                Arena limited_scratch = scratch;
                IrFunction result = sentinel;
                if (failure == 0)
                {
                    limited_output.capacity = limited_output.length;
                }
                else if (failure == 1)
                {
                    limited_scratch.capacity = limited_scratch.length;
                }
                else if (failure == 2)
                {
                    function.b[1] = BUSTER_IR_NONE;
                }
                else
                {
                    function.result_type = IR_VOID;
                    ir_test_instruction(&function, 2, IR_BR, IR_VOID, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
                }
                const IrCheck check = v == 0 ? ssa_ir_promote(&function, &limited_output, &limited_scratch, &result) : ssa_ir_promote_scalar(&function, &limited_output, &limited_scratch, &result);
                const bool expected = failure < 2 ? check.error == IR_ERROR_MEMORY : check.error != IR_OK;
                ssa_ir_test_expect(tally, expected && limited_output.length == output_mark && limited_scratch.length == scratch_mark && memcmp(&result, &sentinel, sizeof(result)) == 0, "allocation failure, invalid operand and entry backedge roll back arenas and result");
                bool prefix_ok = true;
                for (uint32_t b = 0; b < 64; b += 1)
                {
                    prefix_ok = prefix_ok && output_prefix[b] == 0xa5 && scratch_prefix[b] == 0x5a;
                }
                ssa_ir_test_expect(tally, prefix_ok, "failure preserves preexisting arena bytes");
                function.result_type = IR_I64;
                function.b[1] = 0;
                ir_test_instruction(&function, 2, IR_RET, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
            }
            IrFunction result = sentinel;
            const IrCheck overlap = v == 0 ? ssa_ir_promote(&function, &output, &output, &result) : ssa_ir_promote_scalar(&function, &output, &output, &result);
            ssa_ir_test_expect(tally, overlap.error != IR_OK && output.length == output_mark && memcmp(&result, &sentinel, sizeof(result)) == 0, "aliased output and scratch arenas are rejected transactionally");
            Arena aliased_input = input;
            const uint64_t input_mark = input.length;
            const IrCheck conflict = v == 0 ? ssa_ir_promote(&function, &aliased_input, &scratch, &result) : ssa_ir_promote_scalar(&function, &aliased_input, &scratch, &result);
            ssa_ir_test_expect(tally, conflict.error != IR_OK && aliased_input.length == input_mark && scratch.length == scratch_mark && memcmp(&result, &sentinel, sizeof(result)) == 0, "output overlapping input storage is rejected transactionally");
        }
        // The input verifier without a dominator view accepts this shape. The
        // adapter must discover that a hidden COPY operand comes from one arm
        // of the diamond and is unavailable on the other reaching edge.
        const IrSizes dominance_sizes = {
            .instruction_count = 7,
            .block_count = 4,
            .constant_count = 1,
            .parameter_count = 1,
        };
        IrFunction nondominating = {0};
        if (ssa_ir_test_expect(tally, ir_create(&input, &dominance_sizes, &nondominating) == IR_OK, "allocate hidden-operand dominance fixture"))
        {
            nondominating.result_type = IR_I64;
            nondominating.parameter_types[0] = IR_I1;
            nondominating.constants[0].low = 17;
            nondominating.block_offsets[1] = 2;
            nondominating.block_offsets[2] = 4;
            nondominating.block_offsets[3] = 5;
            nondominating.block_offsets[4] = 7;
            ir_test_instruction(&nondominating, 0, IR_PARAM, IR_I1, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&nondominating, 1, IR_CBR, IR_VOID, 0, 1, 2);
            ir_test_instruction(&nondominating, 2, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&nondominating, 3, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&nondominating, 4, IR_BR, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&nondominating, 5, IR_COPY, IR_I64, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&nondominating, 6, IR_RET, IR_VOID, 5, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ssa_ir_test_expect(tally, ir_verify(&nondominating, &scratch, 0).error == IR_OK, "hidden-operand fixture requires real cross-block dominance");
            for (uint32_t v = 0; v < 2; v += 1)
            {
                IrFunction result = sentinel;
                const IrCheck check = v == 0 ? ssa_ir_promote(&nondominating, &output, &scratch, &result) : ssa_ir_promote_scalar(&nondominating, &output, &scratch, &result);
                ssa_ir_test_expect(tally, check.error == IR_ERROR_DOMINANCE && check.instruction == 5 && output.length == output_mark && scratch.length == scratch_mark && memcmp(&result, &sentinel, sizeof(result)) == 0, "hidden non-dominating value use is rejected transactionally");
            }
        }
        // Tier 0's frame roles (incoming variadic anchors, va_list objects) never reach promotion: ir_lower_typed
        // refuses them, and a slot flag outside IR's three must fail the adapter's input verification rather than
        // be promoted as an ordinary scalar.
        const IrSizes role_sizes = {
            .instruction_count = 5,
            .block_count = 1,
            .constant_count = 1,
            .slot_count = 1,
        };
        IrFunction role = {0};
        if (ssa_ir_test_expect(tally, ir_create(&input, &role_sizes, &role) == IR_OK, "allocate unknown slot-role fixture"))
        {
            role.result_type = IR_I64;
            role.block_offsets[1] = 5;
            role.constants[0].low = 7;
            role.slots[0] = (IrSlot){
                .size = 8,
                .alignment = 8,
                .type = IR_I64,
                .flags = BUSTER_IR_SLOT_TEMPORARY << 1,
            };
            ir_test_instruction(&role, 0, IR_STACK_ADDR, IR_PTR, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&role, 1, IR_CONST, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&role, 2, IR_STORE, IR_VOID, 0, 1, BUSTER_IR_NONE);
            ir_test_instruction(&role, 3, IR_LOAD, IR_I64, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_test_instruction(&role, 4, IR_RET, IR_VOID, 3, BUSTER_IR_NONE, BUSTER_IR_NONE);
            for (uint32_t v = 0; v < 2; v += 1)
            {
                IrFunction result = sentinel;
                const IrCheck check = v == 0 ? ssa_ir_promote(&role, &output, &scratch, &result) : ssa_ir_promote_scalar(&role, &output, &scratch, &result);
                ssa_ir_test_expect(tally, check.error != IR_OK && output.length == output_mark && scratch.length == scratch_mark && memcmp(&result, &sentinel, sizeof(result)) == 0, "a slot flag outside IR's set is refused transactionally");
            }
            role.slots[0].flags = 0;
            IrFunction control = {0};
            IrFunction control_twin = {0};
            if (ssa_ir_test_promote(tally, &role, &output, &twin_output, &scratch, &control, &control_twin))
            {
                ssa_ir_test_expect(tally, control.slot_count == 0 && control_twin.slot_count == 0, "the same slot without the unknown flag promotes");
            }
            arena_pop_to(&output, output_mark);
            arena_pop_to(&twin_output, 0);
        }
        IrFunction empty = {0};
        IrFunction promoted = {0};
        IrFunction twin = {0};
        const IrSizes empty_sizes = {0};
        if (ssa_ir_test_expect(tally, ir_create(&input, &empty_sizes, &empty) == IR_OK, "allocate empty shared IR"))
        {
            if (ssa_ir_test_promote(tally, &empty, &output, &twin_output, &scratch, &promoted, &twin))
            {
                ssa_ir_test_expect(tally, promoted.block_count == 0 && promoted.instruction_count == 0 && twin.block_count == 0 && twin.instruction_count == 0, "empty function stays empty");
            }
        }
    }
    arena_release(&input);
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
}

static int run_ssa_ir_tests(int argc, char** argv)
{
    uint64_t seed = 134;
    uint32_t count = 16;
    bool valid = true;
    for (int i = 0; i < argc && valid; i += 1)
    {
        if ((strcmp(argv[i], "--seed") == 0 || strcmp(argv[i], "--count") == 0) && i + 1 < argc)
        {
            const bool is_seed = strcmp(argv[i], "--seed") == 0;
            char* end = 0;
            errno = 0;
            const uint64_t value = strtoull(argv[++i], &end, 0);
            valid = errno == 0 && end != argv[i] && *end == 0 && argv[i][0] != '-' && (is_seed || (value > 0 && value <= 100000));
            if (valid)
            {
                if (is_seed)
                {
                    seed = value;
                }
                else
                {
                    count = (uint32_t)value;
                }
            }
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-ssa-ir [--seed N] [--count N]\n");
    }
    else
    {
        SsaIrTestTally tally = {0};
        ssa_ir_test_memory(&tally);
        ssa_ir_test_variable_operands(&tally);
        ssa_ir_test_failures(&tally);
        ssa_ir_test_programs(&tally, seed, count, false);
        ssa_ir_test_programs(&tally, seed, count, true);
        printf("test-ssa-ir: %" PRIu64 " checks, %" PRIu64 " failed\n", tally.checks, tally.failures);
        result = tally.failures != 0;
    }
    return result;
}
