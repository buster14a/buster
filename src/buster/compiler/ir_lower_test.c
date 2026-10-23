// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Test-only execution of the IR emitted by ir_lower_typed, independent of the
// typed-stream interpreter. Calls use an explicit frame stack. Memory accesses
// are checked against live frame slots and the generated program's globals.
// This is an oracle for #131's adapter, not #176's production IR interpreter.
// Include after ir.c, ir_lower.c, tier0_generate.c and tier0_test.c.

typedef struct IrLowerTestTally IrLowerTestTally;
struct IrLowerTestTally
{
    uint64_t checks;
    uint64_t failures;
};

static bool ir_lower_test_expect(IrLowerTestTally* tally, bool condition, const char* name, uint64_t seed, uint32_t function)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 20)
        {
            fprintf(stderr, "test-ir-lowering: %s (seed %" PRIu64 ", function %u)\n", name, seed, function);
        }
        tally->failures += 1;
    }
    return condition;
}

static uint64_t ir_lower_test_bits(uint8_t type, uint64_t value)
{
    const uint32_t bits = ir_type_bits[type];
    return bits < 64 ? value & ((1ull << bits) - 1) : value;
}

static int64_t ir_lower_test_signed(uint8_t type, uint64_t value)
{
    const uint32_t shift = 64 - ir_type_bits[type];
    return (int64_t)(value << shift) >> shift;
}

static double ir_lower_test_float(uint8_t type, uint64_t bits)
{
    double result = 0;
    if (type == IR_F32)
    {
        const uint32_t low = (uint32_t)bits;
        float value = 0;
        memcpy(&value, &low, sizeof(value));
        result = value;
    }
    else
    {
        memcpy(&result, &bits, sizeof(result));
    }
    return result;
}

static uint64_t ir_lower_test_float_bits(uint8_t type, double value)
{
    uint64_t bits = 0;
    if (type == IR_F32)
    {
        const float narrowed = (float)value;
        uint32_t low = 0;
        memcpy(&low, &narrowed, sizeof(low));
        bits = low;
    }
    else
    {
        memcpy(&bits, &value, sizeof(bits));
    }
    return bits;
}

#define BUSTER_IR_LOWER_TEST_FRAMES 64
#define BUSTER_IR_LOWER_TEST_STEPS 10000000u

typedef struct IrLowerTestFrame IrLowerTestFrame;
struct IrLowerTestFrame
{
    const IrFunction* function;
    uint64_t* values;
    uint64_t* slots;
    uint64_t arguments[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
    uint64_t arena_mark;
    uint32_t instruction;
    uint32_t destination;
};

static bool ir_lower_test_frame(IrLowerTestFrame* frame, const IrFunction* function, const uint64_t* arguments, uint32_t count, uint32_t destination, Arena* arena)
{
    memset(frame, 0, sizeof(*frame));
    frame->function = function;
    frame->arena_mark = arena->length;
    frame->destination = destination;
    frame->instruction = function->block_offsets[0];
    frame->values = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)function->instruction_count + 1);
    frame->slots = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)function->slot_count + 1);
    bool ok = frame->values != 0 && frame->slots != 0 && count == function->parameter_count && count <= BUSTER_TIER0_MAX_CALL_ARGUMENTS;
    if (ok)
    {
        memset(frame->values, 0, ((uint64_t)function->instruction_count + 1) * sizeof(uint64_t));
        memcpy(frame->arguments, arguments, (size_t)count * sizeof(uint64_t));
        for (uint32_t s = 0; s < function->slot_count && ok; s += 1)
        {
            void* slot = arena_push(arena, function->slots[s].size, function->slots[s].alignment);
            ok = slot != 0;
            if (ok)
            {
                memset(slot, 0, function->slots[s].size);
                frame->slots[s] = (uintptr_t)slot;
            }
        }
    }
    return ok;
}

static bool ir_lower_test_memory(const IrLowerTestFrame* frames, uint32_t depth, const uint8_t* globals, uint32_t global_bytes, uint64_t address, uint32_t bytes)
{
    const uint64_t global_address = (uintptr_t)globals;
    bool found = address >= global_address && address - global_address <= global_bytes && bytes <= global_bytes - (address - global_address);
    for (uint32_t f = 0; f < depth && !found; f += 1)
    {
        const IrLowerTestFrame* frame = &frames[f];
        for (uint32_t s = 0; s < frame->function->slot_count && !found; s += 1)
        {
            const uint64_t start = frame->slots[s];
            const uint64_t size = frame->function->slots[s].size;
            found = address >= start && address - start <= size && bytes <= size - (address - start);
        }
    }
    return found;
}

// Only the adapter's <=64-bit types execute here. A malformed address, unsupported
// operation or exhausted step/frame budget reports a fault rather than executing
// an unchecked host load or making this test interpreter recurse.
static uint64_t ir_lower_test_interpret(const IrFunction* functions, uint32_t function_count, uint32_t entry, uint32_t global_count, const Tier0TestData* data, uint8_t* globals, const uint64_t* arguments, uint32_t argument_count, Arena* arena, bool* fault)
{
    const uint64_t mark = arena->length;
    IrLowerTestFrame* frames = BUSTER_ARENA_PUSH_ARRAY(arena, IrLowerTestFrame, BUSTER_IR_LOWER_TEST_FRAMES);
    bool ok = frames != 0 && entry < function_count;
    uint32_t depth = ok ? 1 : 0;
    uint32_t steps = 0;
    uint64_t result = 0;
    if (ok)
    {
        ok = ir_lower_test_frame(&frames[0], &functions[entry], arguments, argument_count, BUSTER_IR_NONE, arena);
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
                ok = a != 0 && a <= global_count;
                if (ok)
                {
                    value = (uintptr_t)(globals + data->offsets[a - 1]);
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
            {
                const IrCall* call = &function->calls[b];
                const uint32_t callee = a - global_count - 1;
                uint64_t passed[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
                ok = a > global_count && callee < function_count && depth < BUSTER_IR_LOWER_TEST_FRAMES && call->argument_count <= BUSTER_TIER0_MAX_CALL_ARGUMENTS;
                for (uint32_t p = 0; ok && p < call->argument_count; p += 1)
                {
                    passed[p] = frame->values[function->extra[call->argument_first + p]];
                }
                if (ok)
                {
                    ok = ir_lower_test_frame(&frames[depth], &functions[callee], passed, call->argument_count, i, arena);
                    depth += ok;
                }
                has_value = false;
                break;
            }
            case IR_BR:
                frame->instruction = function->block_offsets[a];
                break;
            case IR_CBR:
                frame->instruction = function->block_offsets[av != 0 ? b : c];
                break;
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
                fprintf(stderr, "  IR execution fault: function %td instruction %u %s.%s, a=%016" PRIx64 ", b=%016" PRIx64 "\n",
                        function - functions, i, ir_op_names[op], ir_type_names[type], av, bv);
            }
        }
    }
    *fault = !ok || depth != 0;
    arena_pop_to(arena, mark);
    return result;
}

static bool ir_lower_test_equal(const IrFunction* a, const IrFunction* b)
{
    bool equal = a->instruction_count == b->instruction_count && a->block_count == b->block_count && a->slot_count == b->slot_count && a->constant_count == b->constant_count &&
                 a->address_count == b->address_count && a->call_count == b->call_count && a->extra_count == b->extra_count && a->call_type_count == b->call_type_count &&
                 a->parameter_count == b->parameter_count && a->result_type == b->result_type && a->symbol_count == b->symbol_count;
    if (equal)
    {
        const size_t count = a->instruction_count;
        equal = memcmp(a->op, b->op, count) == 0 && memcmp(a->type, b->type, count) == 0 && memcmp(a->flags, b->flags, count) == 0 &&
                memcmp(a->alignment_log2, b->alignment_log2, count) == 0 && memcmp(a->a, b->a, count * 4) == 0 && memcmp(a->b, b->b, count * 4) == 0 &&
                memcmp(a->c, b->c, count * 4) == 0 && memcmp(a->block_offsets, b->block_offsets, ((size_t)a->block_count + 1) * 4) == 0;
        for (uint32_t k = 0; k < a->constant_count && equal; k += 1)
        {
            equal = a->constants[k].low == b->constants[k].low && a->constants[k].high == b->constants[k].high;
        }
        for (uint32_t s = 0; s < a->slot_count && equal; s += 1)
        {
            equal = a->slots[s].size == b->slots[s].size && a->slots[s].alignment == b->slots[s].alignment && a->slots[s].type == b->slots[s].type && a->slots[s].flags == b->slots[s].flags;
        }
        for (uint32_t d = 0; d < a->address_count && equal; d += 1)
        {
            equal = a->addresses[d].offset == b->addresses[d].offset && a->addresses[d].scale == b->addresses[d].scale;
        }
        for (uint32_t c = 0; c < a->call_count && equal; c += 1)
        {
            const IrCall ac = a->calls[c];
            const IrCall bc = b->calls[c];
            equal = ac.argument_first == bc.argument_first && ac.argument_count == bc.argument_count && ac.type_first == bc.type_first && ac.parameter_count == bc.parameter_count &&
                    ac.result_type == bc.result_type && ac.variadic == bc.variadic;
        }
        for (uint32_t e = 0; e < a->extra_count && equal; e += 1)
        {
            equal = a->extra[e] == b->extra[e];
        }
        for (uint32_t t = 0; t < a->call_type_count && equal; t += 1)
        {
            equal = a->call_types[t] == b->call_types[t];
        }
        for (uint32_t p = 0; p < a->parameter_count && equal; p += 1)
        {
            equal = a->parameter_types[p] == b->parameter_types[p];
        }
    }
    return equal;
}

static bool ir_lower_test_lower(IrLowerTestTally* tally, const Tier0Function* source, IrType result_type, uint32_t symbols, Arena* output, Arena* twin_output, Arena* scratch, IrFunction* function, uint64_t seed, uint32_t index)
{
    IrFunction twin =
    {
        0
    };
    const IrCheck vector = ir_lower_typed(source, result_type, symbols, output, scratch, function);
    const IrCheck scalar = ir_lower_typed_scalar(source, result_type, symbols, twin_output, scratch, &twin);
    bool ok = ir_lower_test_expect(tally, vector.error == IR_OK && scalar.error == IR_OK, "both lowering paths accept", seed, index);
    if (!ok && tally->failures < 20)
    {
        fprintf(stderr, "  vector error %u at %u, scalar error %u at %u\n", vector.error, vector.instruction, scalar.error, scalar.instruction);
    }
    if (ok)
    {
        ok = ir_lower_test_expect(tally, ir_lower_test_equal(function, &twin), "identical IR columns and side tables", seed, index);
        ok = ir_lower_test_expect(tally, ir_verify(function, scratch, 0).error == IR_OK && ir_verify_scalar(function, scratch, 0).error == IR_OK &&
                                       ir_verify(&twin, scratch, 0).error == IR_OK && ir_verify_scalar(&twin, scratch, 0).error == IR_OK,
                                  "both verifiers accept both lowerings", seed, index) && ok;
    }
    return ok;
}

static void ir_lower_test_programs(IrLowerTestTally* tally, uint64_t seed, uint32_t count)
{
    Tier0Tables tables =
    {
        0
    };
    Arena input = arena_reserve(1ull << 30);
    Arena output = arena_reserve(1ull << 30);
    Arena twin_output = arena_reserve(1ull << 30);
    Arena scratch = arena_reserve(1ull << 30);
    Arena execution = arena_reserve(1ull << 30);
    bool ready = input.base != 0 && output.base != 0 && twin_output.base != 0 && scratch.base != 0 && execution.base != 0 && tier0_tables_open(&tables);
    ready = ir_lower_test_expect(tally, ready, "program arenas and ABI tables", seed, 0);
    uint64_t entries = 0;
    uint64_t instructions = 0;
    uint64_t functions_seen = 0;
    for (uint32_t p = 0; ready && p < count; p += 1)
    {
        const uint64_t program_seed = seed + p;
        Tier0Program* program = BUSTER_ARENA_PUSH_ARRAY(&input, Tier0Program, 1);
        Tier0Functions* sources = BUSTER_ARENA_PUSH_ARRAY(&input, Tier0Functions, 1);
        IrFunction* functions = BUSTER_ARENA_PUSH_ARRAY(&input, IrFunction, BUSTER_TIER0_GENERATE_MAX_FUNCTIONS);
        uint8_t* globals = BUSTER_ARENA_PUSH_ARRAY(&input, uint8_t, BUSTER_TIER0_GENERATE_MAX_GLOBALS * BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS * 8 + 64);
        uint8_t* expected_globals = BUSTER_ARENA_PUSH_ARRAY(&input, uint8_t, BUSTER_TIER0_GENERATE_MAX_GLOBALS * BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS * 8 + 64);
        const Tier0ProgramOptions options =
        {
            .functions = 8,
            .statements = 40,
            .expression_nodes = 30,
            .calls = true,
            .loops = true,
            .floating = true,
            .pointers = true,
            .defined = true,
        };
        bool ok = program != 0 && sources != 0 && functions != 0 && globals != 0 && expected_globals != 0;
        ok = ok && tier0_generate_program(program, &input, program_seed, options, "ir_c_") && tier0_program_functions(program, &tables, &input, sources);
        ok = ir_lower_test_expect(tally, ok, "generated typed program", program_seed, 0);
        for (uint32_t f = 0; ok && f < sources->count; f += 1)
        {
            const IrType result_type = (IrType)ir_typed_types[program->functions[f].result_class];
            ok = ir_lower_test_lower(tally, &sources->functions[f], result_type, program->global_count + program->function_count + 1, &output, &twin_output, &scratch, &functions[f], program_seed, f);
            if (ok)
            {
                entries += sources->functions[f].count;
                instructions += functions[f].instruction_count;
                functions_seen += 1;
            }
            arena_pop_to(&twin_output, 0);
        }
        if (ok)
        {
            const Tier0TestData data = tier0_test_layout(program);
            uint64_t arguments[BUSTER_TIER0_TEST_INPUTS][3];
            tier0_test_inputs(program_seed, arguments);
            for (uint32_t r = 0; ok && r < BUSTER_TIER0_TEST_INPUTS; r += 1)
            {
                bool source_fault = false;
                bool ir_fault = false;
                tier0_test_initial(program, &data, globals);
                const uint64_t expected = tier0_test_interpret(program, sources, &data, globals, arguments[r], &source_fault);
                memcpy(expected_globals, globals, data.size);
                tier0_test_initial(program, &data, globals);
                const uint64_t actual = ir_lower_test_interpret(functions, sources->count, sources->count - 1, program->global_count, &data, globals, arguments[r], 3, &execution, &ir_fault);
                ok = ir_lower_test_expect(tally, !source_fault && !ir_fault, "typed and IR execution finish", program_seed, r);
                ok = ir_lower_test_expect(tally, expected == actual && memcmp(expected_globals, globals, data.size) == 0, "typed and IR results and global bytes agree", program_seed, r) && ok;
                if (!ok && tally->failures < 20)
                {
                    fprintf(stderr, "  expected %016" PRIx64 ", IR %016" PRIx64 ", faults %u/%u\n", expected, actual, source_fault, ir_fault);
                }
            }
        }
        arena_pop_to(&input, 0);
        arena_pop_to(&output, 0);
        arena_pop_to(&twin_output, 0);
    }
    printf("IR lowering generated programs: %" PRIu64 " functions, %" PRIu64 " typed entries, %" PRIu64 " IR instructions\n", functions_seen, entries, instructions);
    tier0_tables_close(&tables);
    arena_release(&input);
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
    arena_release(&execution);
}

static void ir_lower_test_fixture(IrLowerTestTally* tally, const char* name, const Tier0Function* source, IrType result_type, const uint64_t* arguments, uint64_t expected)
{
    Arena output = arena_reserve(1ull << 20);
    Arena twin_output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    Arena execution = arena_reserve(1ull << 20);
    IrFunction function =
    {
        0
    };
    if (ir_lower_test_lower(tally, source, result_type, 1, &output, &twin_output, &scratch, &function, 0, 0))
    {
        Tier0TestData data =
        {
            0
        };
        uint8_t globals[1] =
        {
            0
        };
        bool fault = false;
        const uint64_t actual = ir_lower_test_interpret(&function, 1, 0, 0, &data, globals, arguments, source->parameter_count, &execution, &fault);
        ir_lower_test_expect(tally, !fault && actual == expected, name, 0, 0);
    }
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
    arena_release(&execution);
}

static void ir_lower_test_fixtures(IrLowerTestTally* tally)
{
    // 10 + (outer ? (inner ? 20 : 30) : 40), preserving the outer value at both joins.
    const uint8_t conditional_kinds[] =
    {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_JUMP_VALUE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_JUMP_VALUE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_ADD,
        BUSTER_TYPED_RETURN
    };
    const uint8_t conditional_classes[] =
    {
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64
    };
    uint64_t conditional_values[] =
    {
        10,
        0,
        9,
        0,
        7,
        20,
        8,
        30,
        10,
        40,
        0,
        0
    };
    const Tier0Function conditional =
    {
        .kinds = conditional_kinds,
        .classes = conditional_classes,
        .values = conditional_values,
        .count = BUSTER_ARRAY_LENGTH(conditional_kinds),
    };
    uint64_t arguments[BUSTER_TIER0_MAX_CALL_ARGUMENTS] =
    {
        0
    };
    ir_lower_test_fixture(tally, "nested false arm restores live outer operand", &conditional, IR_I64, arguments, 50);
    conditional_values[1] = 1;
    ir_lower_test_fixture(tally, "nested inner false arm crosses both joins", &conditional, IR_I64, arguments, 40);
    conditional_values[3] = 1;
    ir_lower_test_fixture(tally, "nested true arm carries two JUMP_VALUE edges", &conditional, IR_I64, arguments, 30);

    const uint8_t loop_kinds[] =
    {
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_SUB,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_BRANCH_TRUE,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_RETURN
    };
    const uint8_t loop_classes[] =
    {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64
    };
    const uint64_t loop_values[] =
    {
        0,
        1,
        0,
        0,
        0,
        0,
        0,
        0
    };
    const uint8_t local_classes[] =
    {
        BUSTER_TYPED_CLASS_U64
    };
    const uint8_t local_flags[] =
    {
        BUSTER_TYPED_LOCAL_ADDRESS_TAKEN
    };
    const AbiLocation parameter =
    {
        .kind = BUSTER_ABI_LOCATION_REGISTERS,
        .registers =
        {
            X86_REGISTER_RDI
        },
    };
    const Tier0Function loop =
    {
        .kinds = loop_kinds,
        .classes = loop_classes,
        .values = loop_values,
        .count = BUSTER_ARRAY_LENGTH(loop_kinds),
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 1,
        .parameter_count = 1,
        .parameters = &parameter,
    };
    arguments[0] = 3;
    ir_lower_test_fixture(tally, "backedge to entry zero does not reinitialize parameters", &loop, IR_I64, arguments, 0);

    const uint8_t shift_kinds[] =
    {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_SHR,
        BUSTER_TYPED_RETURN
    };
    const uint8_t shift_classes[] =
    {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U8,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64
    };
    const uint64_t shift_values[] =
    {
        240,
        3,
        0,
        0
    };
    const Tier0Function shift =
    {
        .kinds = shift_kinds,
        .classes = shift_classes,
        .values = shift_values,
        .count = BUSTER_ARRAY_LENGTH(shift_kinds),
    };
    ir_lower_test_fixture(tally, "shift count of a narrower integer class", &shift, IR_I64, arguments, 30);

    const uint8_t pointer_kinds[] =
    {
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_DUP,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_PTR_ADD,
        BUSTER_TYPED_SUB,
        BUSTER_TYPED_RETURN
    };
    const uint8_t pointer_classes[] =
    {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64
    };
    const uint64_t scale = (1ull << 40) + 3;
    const uint64_t pointer_values[] =
    {
        0,
        0,
        1,
        scale,
        0,
        0
    };
    const Tier0Function pointer =
    {
        .kinds = pointer_kinds,
        .classes = pointer_classes,
        .values = pointer_values,
        .count = BUSTER_ARRAY_LENGTH(pointer_kinds),
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 1,
    };
    ir_lower_test_fixture(tally, "wide pointer scale retains all 64 bits", &pointer, IR_I64, arguments, 0 - scale);

    const Tier0Function empty =
    {
        0
    };
    ir_lower_test_fixture(tally, "empty void body reaches its return", &empty, IR_VOID, arguments, 0);
}

static void ir_lower_test_failures(IrLowerTestTally* tally)
{
    Arena output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    const uint8_t good_kinds[] =
    {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_RETURN
    };
    const uint8_t good_classes[] =
    {
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64
    };
    const uint64_t good_values[] =
    {
        7,
        0
    };
    Tier0Function source =
    {
        .kinds = good_kinds,
        .classes = good_classes,
        .values = good_values,
        .count = 2,
    };
    IrFunction preserved =
    {
        .instruction_count = 123,
        .block_count = 17,
    };
    for (uint32_t vector = 0; vector < 2; vector += 1)
    {
        IrFunction result = preserved;
        const uint64_t output_mark = output.length;
        const uint64_t scratch_mark = scratch.length;
        IrFunction measured =
        {
            0
        };
        IrCheck check = ir_lower_typed_path(&source, IR_I64, 1, &output, &scratch, &measured, vector != 0);
        const uint64_t required = output.length - output_mark;
        ir_lower_test_expect(tally, check.error == IR_OK && required != 0, "measure allocation before exhaustion", 0, vector);
        arena_pop_to(&output, output_mark);
        Arena no_output = output;
        no_output.capacity = no_output.length;
        check = ir_lower_typed_path(&source, IR_I64, 1, &no_output, &scratch, &result, vector != 0);
        ir_lower_test_expect(tally, check.error == IR_ERROR_MEMORY && no_output.length == output_mark && scratch.length == scratch_mark &&
                                  result.instruction_count == preserved.instruction_count && result.block_count == preserved.block_count,
                             "output exhaustion rolls back and preserves result", 0, vector);
        if (required != 0)
        {
            no_output.capacity = output_mark + required - 1;
            check = ir_lower_typed_path(&source, IR_I64, 1, &no_output, &scratch, &result, vector != 0);
            ir_lower_test_expect(tally, check.error == IR_ERROR_MEMORY && no_output.length == output_mark && scratch.length == scratch_mark &&
                                      result.instruction_count == preserved.instruction_count && result.block_count == preserved.block_count,
                                 "partial output allocation rolls back and preserves result", 0, vector);
        }
        Arena no_scratch = scratch;
        no_scratch.capacity = no_scratch.length;
        check = ir_lower_typed_path(&source, IR_I64, 1, &output, &no_scratch, &result, vector != 0);
        ir_lower_test_expect(tally, check.error == IR_ERROR_MEMORY && output.length == output_mark && no_scratch.length == scratch_mark &&
                                  result.instruction_count == preserved.instruction_count,
                             "scratch exhaustion rolls back and preserves result", 0, vector);
        no_scratch.capacity = scratch_mark + 32;
        check = ir_lower_typed_path(&source, IR_I64, 1, &output, &no_scratch, &result, vector != 0);
        ir_lower_test_expect(tally, check.error == IR_ERROR_MEMORY && output.length == output_mark && no_scratch.length == scratch_mark &&
                                  result.instruction_count == preserved.instruction_count,
                             "partial scratch allocation rolls back and preserves result", 0, vector);
        check = ir_lower_typed_path(&source, IR_I32, 1, &output, &scratch, &result, vector != 0);
        ir_lower_test_expect(tally, check.error != IR_OK && output.length == output_mark && scratch.length == scratch_mark,
                             "explicit result type mismatch rejected", 0, vector);
        source.count = BUSTER_TIER0_MAX_ENTRIES + 1;
        check = ir_lower_typed_path(&source, IR_I64, 1, &output, &scratch, &result, vector != 0);
        ir_lower_test_expect(tally, check.error == IR_ERROR_LIMIT && output.length == output_mark && scratch.length == scratch_mark,
                             "oversized count rejected before reading columns", 0, vector);
        source.count = 2;
        source.kinds = 0;
        check = ir_lower_typed_path(&source, IR_I64, 1, &output, &scratch, &result, vector != 0);
        ir_lower_test_expect(tally, check.error == IR_ERROR_STRUCTURE && output.length == output_mark && scratch.length == scratch_mark,
                             "missing source column rejected", 0, vector);
        source.kinds = good_kinds;
        const uint8_t bad_kinds[] =
        {
            BUSTER_TYPED_ADD,
            BUSTER_TYPED_RETURN
        };
        source.kinds = bad_kinds;
        check = ir_lower_typed_path(&source, IR_I64, 1, &output, &scratch, &result, vector != 0);
        ir_lower_test_expect(tally, check.error != IR_OK && output.length == output_mark && scratch.length == scratch_mark,
                             "source operand underflow rejected", 0, vector);
        source.kinds = good_kinds;
        check = ir_lower_typed_path(&source, IR_I64, 1, &output, &output, &result, vector != 0);
        ir_lower_test_expect(tally, check.error == IR_ERROR_STRUCTURE && output.length == output_mark,
                             "aliased output and scratch rejected", 0, vector);
    }
    arena_release(&output);
    arena_release(&scratch);
}

// Local flags past ADDRESS_TAKEN (frame roles the adapter doesn't model) are
// refused by both paths before validation or allocation, leaving the caller's
// result and both arenas as they were; ordinary and address-taken locals lower.
static void ir_lower_test_local_roles(IrLowerTestTally* tally)
{
    Arena output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    const uint8_t kinds[] =
    {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_RETURN
    };
    const uint8_t classes[] =
    {
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64
    };
    const uint64_t values[] =
    {
        7,
        0
    };
    const uint8_t local_classes[] =
    {
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64
    };
    uint8_t local_flags[3] =
    {
        0
    };
    Tier0Function source =
    {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = 2,
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 3,
    };
    const IrFunction preserved =
    {
        .instruction_count = 123,
        .block_count = 17,
    };
    // Nonzero marks, so that a rollback to zero would show.
    const bool ready = BUSTER_ARENA_PUSH_ARRAY(&output, uint8_t, 24) != 0 && BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, 40) != 0;
    ir_lower_test_expect(tally, ready, "local role arenas", 0, 0);
    for (uint32_t vector = 0; ready && vector < 2; vector += 1)
    {
        const uint64_t output_mark = output.length;
        const uint64_t scratch_mark = scratch.length;
        local_flags[1] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
        IrFunction result = preserved;
        IrCheck check = ir_lower_typed_path(&source, IR_I64, 1, &output, &scratch, &result, vector != 0);
        ir_lower_test_expect(tally, check.error == IR_OK && scratch.length == scratch_mark, "ordinary and address-taken locals lower", 0, vector);
        arena_pop_to(&output, output_mark);
        // Each role bit #148's frames define, alone and with ADDRESS_TAKEN, on
        // every local, and a bit past them.
        const uint8_t roles[] =
        {
            0x02,
            0x04,
            0x08,
            0x10,
            0x80
        };
        for (uint32_t r = 0; r < sizeof(roles); r += 1)
        {
            for (uint32_t local = 0; local < 3; local += 1)
            {
                for (uint32_t taken = 0; taken < 2; taken += 1)
                {
                    local_flags[0] = 0;
                    local_flags[1] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
                    local_flags[2] = 0;
                    local_flags[local] = (uint8_t)(roles[r] | (taken != 0 ? BUSTER_TYPED_LOCAL_ADDRESS_TAKEN : 0));
                    result = preserved;
                    check = ir_lower_typed_path(&source, IR_I64, 1, &output, &scratch, &result, vector != 0);
                    ir_lower_test_expect(tally, check.error == IR_ERROR_METADATA && check.detail == local && output.length == output_mark &&
                                                    scratch.length == scratch_mark && result.instruction_count == preserved.instruction_count && result.block_count == preserved.block_count,
                                         "local frame role refused without allocation", r, vector * 8 + local * 2 + taken);
                }
            }
        }
        // A role on a local the stream never names is refused too, but the
        // earlier count and pointer checks keep their precedence.
        local_flags[0] = 0;
        local_flags[1] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
        local_flags[2] = 0x02;
        source.count = BUSTER_TIER0_MAX_ENTRIES + 1;
        check = ir_lower_typed_path(&source, IR_I64, 1, &output, &scratch, &result, vector != 0);
        ir_lower_test_expect(tally, check.error == IR_ERROR_LIMIT, "count limit precedes local roles", 0, vector);
        source.count = 2;
        source.kinds = 0;
        check = ir_lower_typed_path(&source, IR_I64, 1, &output, &scratch, &result, vector != 0);
        ir_lower_test_expect(tally, check.error == IR_ERROR_STRUCTURE, "missing column precedes local roles", 0, vector);
        source.kinds = kinds;
        source.local_count = 2;
        check = ir_lower_typed_path(&source, IR_I64, 1, &output, &scratch, &result, vector != 0);
        ir_lower_test_expect(tally, check.error == IR_OK, "flags past local_count are not read", 0, vector);
        arena_pop_to(&output, output_mark);
        source.local_count = 3;
        local_flags[2] = 0;
    }
    arena_release(&output);
    arena_release(&scratch);
}

static void ir_lower_test_guarded(IrLowerTestTally* tally)
{
    const uint64_t page = 4096;
    uint8_t* mapping = mmap(0, page * 6, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Arena output = arena_reserve(1ull << 20);
    Arena twin_output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    const bool ready = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0 &&
                       mprotect(mapping + 3 * page, page, PROT_NONE) == 0 && mprotect(mapping + 5 * page, page, PROT_NONE) == 0;
    if (ir_lower_test_expect(tally, ready, "guarded typed column setup", 0, 0))
    {
        for (uint32_t count = 0; count <= 67; count += 1)
        {
            uint8_t* kinds = mapping + page - count;
            uint8_t* classes = mapping + 3 * page - count;
            uint64_t* values = (uint64_t*)(mapping + 5 * page - count * sizeof(uint64_t));
            for (uint32_t i = 0; i < count; i += 1)
            {
                kinds[i] = i == 0 ? BUSTER_TYPED_CONST : BUSTER_TYPED_NEG;
                classes[i] = BUSTER_TYPED_CLASS_I64;
                values[i] = i == 0 ? 7 : 0;
            }
            if (count != 0)
            {
                kinds[count - 1] = BUSTER_TYPED_RETURN;
                classes[count - 1] = count > 1 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_VOID;
            }
            const Tier0Function source =
            {
                .kinds = kinds,
                .classes = classes,
                .values = values,
                .count = count,
            };
            IrFunction function =
            {
                0
            };
            ir_lower_test_lower(tally, &source, count > 1 ? IR_I64 : IR_VOID, 1, &output, &twin_output, &scratch, &function, 0, count);
            arena_pop_to(&output, 0);
            arena_pop_to(&twin_output, 0);
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 6);
    }
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
}

static int run_ir_lower_tests(int argc, char** argv)
{
    uint64_t seed = 131;
    uint32_t count = 32;
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
        fprintf(stderr, "usage: test-ir-lowering [--seed N] [--count N]\n");
    }
    else
    {
        IrLowerTestTally tally =
        {
            0
        };
        ir_lower_test_fixtures(&tally);
        ir_lower_test_failures(&tally);
        ir_lower_test_local_roles(&tally);
        ir_lower_test_guarded(&tally);
        ir_lower_test_programs(&tally, seed, count);
        printf("test-ir-lowering: %" PRIu64 " checks, %" PRIu64 " failed\n", tally.checks, tally.failures);
        result = tally.failures != 0;
    }
    return result;
}
