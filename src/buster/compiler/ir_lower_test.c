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
    // The function's own copies of its aggregate parameters' bytes (IrPassed.size), which those parameters point to.
    uint64_t* copies;
    uint64_t arena_mark;
    uint32_t instruction;
    uint32_t destination;
};

// A frame of `function` called with `arguments`: an aggregate parameter's argument is the address of the caller's bytes,
// which the frame copies for the callee to own, as a by-value aggregate's callee does.
static bool ir_lower_test_frame(IrLowerTestFrame* frame, const IrFunction* function, const uint64_t* arguments, uint32_t count, uint32_t destination, Arena* arena)
{
    memset(frame, 0, sizeof(*frame));
    frame->function = function;
    frame->arena_mark = arena->length;
    frame->destination = destination;
    frame->instruction = function->block_offsets[0];
    frame->values = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)function->instruction_count + 1);
    frame->slots = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)function->slot_count + 1);
    bool ok = frame->values != 0 && frame->slots != 0 && (count == function->parameter_count || (function->variadic && count > function->parameter_count)) &&
              count <= BUSTER_TIER0_MAX_CALL_ARGUMENTS;
    if (ok)
    {
        memset(frame->values, 0, ((uint64_t)function->instruction_count + 1) * sizeof(uint64_t));
        memcpy(frame->arguments, arguments, (size_t)count * sizeof(uint64_t));
        for (uint32_t s = 0; s < function->slot_count && ok; s += 1)
        {
            // A variadic definition's anchors, as an all-integer call lays them out: the arguments' first six in the
            // register save area's general slots, in order, and the rest where the overflow area starts.
            const uint8_t flags = function->slots[s].flags;
            const uint64_t size = (flags & BUSTER_IR_SLOT_OVERFLOW_AREA) != 0 ? 8 * (count > 6 ? count - 6 : 1) : function->slots[s].size;
            uint8_t* slot = arena_push(arena, size, function->slots[s].alignment);
            ok = slot != 0;
            if (ok)
            {
                memset(slot, 0, size);
                for (uint32_t p = 0; (flags & BUSTER_IR_SLOT_REGISTER_SAVE_AREA) != 0 && p < count && p < 6; p += 1)
                {
                    memcpy(slot + 8 * p, &arguments[p], 8);
                }
                for (uint32_t p = 6; (flags & BUSTER_IR_SLOT_OVERFLOW_AREA) != 0 && p < count; p += 1)
                {
                    memcpy(slot + 8 * (p - 6), &arguments[p], 8);
                }
                frame->slots[s] = (uintptr_t)slot;
            }
        }
        frame->copies = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)count + 1);
        ok = ok && frame->copies != 0;
        for (uint32_t p = 0; ok && p < count; p += 1)
        {
            const uint32_t size = function->parameter_passed != 0 && p < function->parameter_count ? function->passed[function->parameter_passed - 1 + p].size : 0;
            uint8_t* copy = size != 0 ? arena_push(arena, size, 16) : 0;
            ok = size == 0 || copy != 0;
            if (ok && size != 0)
            {
                memcpy(copy, (const void*)(uintptr_t)arguments[p], size);
                frame->arguments[p] = (uintptr_t)copy;
            }
            frame->copies[p] = (uintptr_t)copy;
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
        for (uint32_t p = 0; frame->function->parameter_passed != 0 && p < frame->function->parameter_count && !found; p += 1)
        {
            const uint64_t start = frame->copies[p];
            const uint64_t size = frame->function->passed[frame->function->parameter_passed - 1 + p].size;
            found = size != 0 && address >= start && address - start <= size && bytes <= size - (address - start);
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
            {
                // A global's bytes, a function's address (its IrFunction's, which only an indirect call reads), or past them
                // label k of the executing function, the symbol global_count + function_count + 1 + k (its labels entry's
                // address, which only an indirect branch reads).
                const uint64_t label = (uint64_t)a - global_count - function_count - 1;
                ok = a != 0 && (a - 1 < (uint64_t)global_count + function_count || label < function->label_count);
                if (ok)
                {
                    value = a <= global_count ? (uintptr_t)(globals + data->offsets[a - 1]) : a - 1 < (uint64_t)global_count + function_count ? (uintptr_t)&functions[a - global_count - 1]
                                                                                                                               : (uintptr_t)&function->labels[label];
                }
                break;
            }
            case IR_BR_INDIRECT:
            {
                const uint64_t offset = av - (uintptr_t)function->labels;
                ok = offset % sizeof(uint32_t) == 0 && offset / sizeof(uint32_t) < function->label_count;
                if (ok)
                {
                    frame->instruction = function->block_offsets[function->labels[offset / sizeof(uint32_t)]];
                }
                break;
            }
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
                // An indirect call's pointer is a function's IrFunction (IR_SYMBOL_ADDR above), at a whole element.
                const uint64_t offset = av - (uintptr_t)functions;
                const bool element = op == IR_CALL || offset % sizeof(IrFunction) == 0;
                const uint64_t callee = op == IR_CALL ? (uint64_t)a - global_count - 1 : offset / sizeof(IrFunction);
                uint64_t passed[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
                ok = (op == IR_CALL_INDIRECT || a > global_count) && element && callee < function_count && depth < BUSTER_IR_LOWER_TEST_FRAMES &&
                     call->argument_count < BUSTER_TIER0_MAX_CALL_ARGUMENTS;
                // A callee that returns an aggregate through a hidden pointer takes the call's destination as parameter 0;
                // an aggregate argument's bytes must be the caller's to read.
                const IrFunction* target = ok ? &functions[(uint32_t)callee] : 0;
                const IrAbiCall* abi = call->abi != 0 ? &function->abi_calls[call->abi - 1] : 0;
                const bool hidden = ok && c != BUSTER_IR_NONE && target->parameter_passed != 0 && target->result_abi.kind == BUSTER_ABI_RESULT_MEMORY;
                uint32_t count = 0;
                if (hidden)
                {
                    passed[count++] = frame->values[c];
                }
                for (uint32_t p = 0; ok && p < call->argument_count; p += 1)
                {
                    const uint32_t size = abi != 0 ? function->passed[abi->passed + p].size : 0;
                    passed[count] = frame->values[function->extra[call->argument_first + p]];
                    ok = size == 0 || ir_lower_test_memory(frames, depth, globals, data->size, passed[count], size);
                    count += 1;
                }
                if (ok)
                {
                    ok = ir_lower_test_frame(&frames[depth], target, passed, count, i, arena);
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
                // An aggregate returned in registers: its bytes (IrFunction.result_size, at most the call's destination's)
                // go where the call's c points, before this frame's slots go.
                if (depth > 1 && function->result_size != 0)
                {
                    const IrLowerTestFrame* caller = &frames[depth - 2];
                    const IrCall* call = &caller->function->calls[caller->function->b[destination]];
                    const uint32_t into = caller->function->c[destination];
                    const uint32_t room = call->abi != 0 ? caller->function->abi_calls[call->abi - 1].result_size : 0;
                    const uint32_t bytes = function->result_size < room ? function->result_size : room;
                    ok = into != BUSTER_IR_NONE && ir_lower_test_memory(frames, depth, globals, data->size, returned, bytes) &&
                         ir_lower_test_memory(frames, depth, globals, data->size, caller->values[into], bytes);
                    if (ok)
                    {
                        memmove((void*)(uintptr_t)caller->values[into], (const void*)(uintptr_t)returned, bytes);
                    }
                }
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
    bool equal = a->instruction_count == b->instruction_count && a->block_count == b->block_count && a->slot_count == b->slot_count && a->constant_count == b->constant_count && a->vector_constant_count == b->vector_constant_count &&
                 a->address_count == b->address_count && a->call_count == b->call_count && a->extra_count == b->extra_count && a->call_type_count == b->call_type_count &&
                 a->parameter_count == b->parameter_count && a->result_type == b->result_type && a->symbol_count == b->symbol_count &&
                 a->label_count == b->label_count && a->abi_call_count == b->abi_call_count && a->passed_count == b->passed_count &&
                 a->parameter_passed == b->parameter_passed && a->result_size == b->result_size && memcmp(&a->result_abi, &b->result_abi, sizeof(AbiReturn)) == 0 &&
                 a->variadic == b->variadic && memcmp(&a->incoming, &b->incoming, sizeof(AbiCall)) == 0;
    if (equal)
    {
        const size_t count = a->instruction_count;
        equal = memcmp(a->op, b->op, count) == 0 && memcmp(a->type, b->type, count) == 0 && memcmp(a->flags, b->flags, count) == 0 &&
                memcmp(a->alignment_log2, b->alignment_log2, count) == 0 && memcmp(a->a, b->a, count * 4) == 0 && memcmp(a->b, b->b, count * 4) == 0 &&
                memcmp(a->c, b->c, count * 4) == 0 && memcmp(a->block_offsets, b->block_offsets, ((size_t)a->block_count + 1) * 4) == 0;
        for (uint32_t k = 0; k < a->vector_constant_count && equal; k += 1)
        {
            equal = memcmp(a->vector_constants + k, b->vector_constants + k, sizeof(IrSimdValue)) == 0;
        }
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
                    ac.result_type == bc.result_type && ac.variadic == bc.variadic && ac.abi == bc.abi;
        }
        for (uint32_t l = 0; l < a->label_count && equal; l += 1)
        {
            equal = a->labels[l] == b->labels[l];
        }
        for (uint32_t c = 0; c < a->abi_call_count && equal; c += 1)
        {
            equal = memcmp(&a->abi_calls[c].call, &b->abi_calls[c].call, sizeof(AbiCall)) == 0 && a->abi_calls[c].passed == b->abi_calls[c].passed &&
                    a->abi_calls[c].result_size == b->abi_calls[c].result_size;
        }
        for (uint32_t p = 0; p < a->passed_count && equal; p += 1)
        {
            equal = memcmp(&a->passed[p].location, &b->passed[p].location, sizeof(AbiLocation)) == 0 && a->passed[p].size == b->passed[p].size;
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

// Whether `promoted` keeps every object slot of `lowered` (a VOID slot that isn't a temporary) as it was, in order:
// promotion compacts the slots it removes and must never take an object's.
static bool ir_lower_test_objects_kept(const IrFunction* lowered, const IrFunction* promoted)
{
    uint32_t next = 0;
    bool same = true;
    for (uint32_t s = 0; same && s < lowered->slot_count; s += 1)
    {
        const IrSlot slot = lowered->slots[s];
        if (slot.type == IR_VOID && (slot.flags & BUSTER_IR_SLOT_TEMPORARY) == 0)
        {
            while (next < promoted->slot_count && !(promoted->slots[next].type == IR_VOID && (promoted->slots[next].flags & BUSTER_IR_SLOT_TEMPORARY) == 0))
            {
                next += 1;
            }
            same = next < promoted->slot_count && promoted->slots[next].size == slot.size && promoted->slots[next].alignment == slot.alignment &&
                   promoted->slots[next].flags == slot.flags;
            next += 1;
        }
    }
    return same;
}

// #134's promotion of a lowered function, by both paths, which may order their output differently (test-ssa-ir checks
// their semantics): each must accept it with real dominance, pass both verifiers and keep every object's slot.
static void ir_lower_test_promote(IrLowerTestTally* tally, const IrFunction* lowered, Arena* output, Arena* twin_output, Arena* scratch, uint64_t seed, uint32_t index)
{
    const uint64_t output_mark = output->length;
    const uint64_t twin_mark = twin_output->length;
    IrFunction vector =
    {
        0
    };
    IrFunction scalar =
    {
        0
    };
    const IrCheck vector_check = ssa_ir_promote(lowered, output, scratch, &vector);
    const IrCheck scalar_check = ssa_ir_promote_scalar(lowered, twin_output, scratch, &scalar);
    bool ok = ir_lower_test_expect(tally, vector_check.error == IR_OK && scalar_check.error == IR_OK && vector_check.dominance_checked && scalar_check.dominance_checked,
                                   "both SSA promotion paths accept the lowering", seed, index);
    ok = ok && ir_lower_test_expect(tally, ir_verify(&vector, scratch, 0).error == IR_OK && ir_verify_scalar(&vector, scratch, 0).error == IR_OK &&
                                               ir_verify(&scalar, scratch, 0).error == IR_OK && ir_verify_scalar(&scalar, scratch, 0).error == IR_OK,
                                    "both verifiers accept both promotions", seed, index);
    ok = ok && ir_lower_test_expect(tally, ir_lower_test_objects_kept(lowered, &vector) && ir_lower_test_objects_kept(lowered, &scalar), "promotion keeps every object's slot", seed, index);
    arena_pop_to(output, output_mark);
    arena_pop_to(twin_output, twin_mark);
}

// Generated programs, lowered by both paths and executed as IR, against the typed-stream interpreter and Tier 0's
// native code of the same streams (test-tier0's harness, which checks the two of them against each other too): their
// results and global bytes; and each lowered function promoted to SSA by both paths. `options` choose the shapes;
// `name` labels the report.
// The shared IR's interpreter on a generated program, strictly or, with `padding`, without undefined-byte checks: a
// program's COPY entries carry its structs' padding, which nothing reads.
static uint64_t ir_lower_test_shared_interpret(const IrFunction* functions, const Tier0Program* generated, const Tier0TestData* data, uint8_t* globals, const uint64_t* inputs, bool padding, Arena* execution, bool* fault)
{
    IrInterpreterFunction code[BUSTER_TIER0_GENERATE_MAX_FUNCTIONS];
    InterpreterBinding bindings[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    for (uint32_t f = 0; f < generated->function_count; f += 1)
    {
        code[f] = (IrInterpreterFunction){ .symbol = generated->global_count + f + 1, .function = &functions[f] };
    }
    for (uint32_t g = 0; g < generated->global_count; g += 1)
    {
        const uint64_t size = (uint64_t)generated->global_elements[g] * tier0_class_bytes(generated->global_classes[g]);
        bindings[g] = (InterpreterBinding){ .symbol = g + 1, .bytes = globals + data->offsets[g], .size = size, .writable = true };
    }
    const IrInterpreterProgram program = { .functions = code, .function_count = generated->function_count, .bindings = bindings, .binding_count = generated->global_count };
    InterpreterValue arguments[3];
    for (uint32_t a = 0; a < 3; a += 1)
    {
        arguments[a] = (InterpreterValue){ .low = inputs[a] };
    }
    InterpreterOptions options = interpreter_default_options();
    options.check_undefined = !padding;
    const InterpreterResult result = ir_interpret(&program, code[generated->function_count - 1].symbol, arguments, 3, &options, execution);
    *fault = result.status != INTERPRETER_OK;
    if (*fault)
    {
        fprintf(stderr, "  shared IR status %u, function %u, instruction %u\n", result.status, result.function, result.entry);
        if (result.function < generated->function_count && result.entry < functions[result.function].instruction_count)
        {
            const IrFunction* function = &functions[result.function];
            fprintf(stderr, "  %s.%s a=%u b=%u c=%u\n", ir_op_names[function->op[result.entry]], ir_type_names[function->type[result.entry]], function->a[result.entry], function->b[result.entry], function->c[result.entry]);
            ir_print(function, execution, stderr);
        }
    }
    return result.value.low;
}

// A generated function's semantic result type: its RETURN entries' class, which a record's makes its address (U64),
// or the class the generator gave it.
static IrType ir_lower_test_result_type(const Tier0Function* source, uint8_t result_class)
{
    uint8_t class = result_class;
    bool found = false;
    for (uint32_t i = 0; !found && i < source->count; i += 1)
    {
        found = source->kinds[i] == BUSTER_TYPED_RETURN;
        class = found ? source->classes[i] : class;
    }
    return (IrType)ir_typed_types[class];
}

static InterpreterResult ir_lower_test_production_typed(const Tier0Functions* sources, const Tier0Program* generated, const Tier0TestData* data, uint8_t* globals, const uint64_t* inputs, bool padding, Arena* execution)
{
    SymbolId symbols[BUSTER_TIER0_GENERATE_MAX_FUNCTIONS];
    InterpreterBinding bindings[BUSTER_TIER0_GENERATE_MAX_GLOBALS];
    for (uint32_t f = 0; f < generated->function_count; f += 1)
    {
        symbols[f] = generated->global_count + f + 1;
    }
    for (uint32_t g = 0; g < generated->global_count; g += 1)
    {
        bindings[g] = (InterpreterBinding){ .symbol = g + 1, .bytes = globals + data->offsets[g],
                                           .size = (uint64_t)generated->global_elements[g] * tier0_class_bytes(generated->global_classes[g]), .writable = true };
    }
    const InterpreterProgram program = { .functions = sources->functions, .function_symbols = symbols, .function_count = generated->function_count,
                                         .bindings = bindings, .binding_count = generated->global_count };
    const InterpreterValue arguments[3] = { { .low = inputs[0] }, { .low = inputs[1] }, { .low = inputs[2] } };
    InterpreterOptions options = interpreter_default_options();
    options.check_undefined = !padding;
    InterpreterResult result;
    interpreter_typed_run(&program, symbols[generated->function_count - 1], arguments, 3, execution, &options, &result);
    return result;
}

// With `records`, the generator's catalog of structs and unions passed and returned by value (#137) joins `options`.
static void ir_lower_test_programs(IrLowerTestTally* tally, uint64_t seed, uint32_t count, Tier0ProgramOptions options, bool records, const char* name)
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
    Arena promoted = arena_reserve(1ull << 30);
    Arena twin_promoted = arena_reserve(1ull << 30);
    Arena native = arena_reserve(1ull << 32);
    Tier0TestCode* code = native.base != 0 ? BUSTER_ARENA_PUSH_ARRAY(&native, Tier0TestCode, 1) : 0;
    uint8_t* code_bytes = native.base != 0 ? BUSTER_ARENA_PUSH_ARRAY(&native, uint8_t, BUSTER_TIER0_TEST_CODE_BYTES) : 0;
    bool ready = input.base != 0 && output.base != 0 && twin_output.base != 0 && scratch.base != 0 && execution.base != 0 && promoted.base != 0 &&
                 twin_promoted.base != 0 && code != 0 && code_bytes != 0 && tier0_tables_open(&tables);
    ready = ir_lower_test_expect(tally, ready, "program arenas and ABI tables", seed, 0);
    options.records = records && ready ? &tables.records : 0;
    const uint64_t native_mark = native.length;
    // What the records' lowering covered: calls with ABI records, aggregate parameters, and both kinds of result.
    uint64_t aggregate_calls = 0;
    uint64_t aggregate_parameters = 0;
    uint64_t register_results = 0;
    uint64_t hidden_results = 0;
    uint64_t entries = 0;
    uint64_t instructions = 0;
    uint64_t functions_seen = 0;
    uint64_t copies = 0;
    uint64_t objects = 0;
    uint64_t indirect_calls = 0;
    uint64_t stack_ops[3] = { 0 };
    for (uint32_t p = 0; ready && p < count; p += 1)
    {
        const uint64_t program_seed = seed + p;
        Tier0Program* program = BUSTER_ARENA_PUSH_ARRAY(&input, Tier0Program, 1);
        Tier0Functions* sources = BUSTER_ARENA_PUSH_ARRAY(&input, Tier0Functions, 1);
        IrFunction* functions = BUSTER_ARENA_PUSH_ARRAY(&input, IrFunction, BUSTER_TIER0_GENERATE_MAX_FUNCTIONS);
        uint8_t* globals = BUSTER_ARENA_PUSH_ARRAY(&input, uint8_t, BUSTER_TIER0_GENERATE_MAX_GLOBALS * BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS * 8 + 64);
        uint8_t* expected_globals = BUSTER_ARENA_PUSH_ARRAY(&input, uint8_t, BUSTER_TIER0_GENERATE_MAX_GLOBALS * BUSTER_TIER0_GENERATE_ARRAY_ELEMENTS * 8 + 64);
        bool ok = program != 0 && sources != 0 && functions != 0 && globals != 0 && expected_globals != 0;
        ok = ok && tier0_generate_program(program, &input, program_seed, options, "ir_c_") && tier0_program_functions(program, &tables, &input, sources);
        ok = ir_lower_test_expect(tally, ok, "generated typed program", program_seed, 0);
        for (uint32_t f = 0; ok && f < sources->count; f += 1)
        {
            const IrType result_type = ir_lower_test_result_type(&sources->functions[f], program->functions[f].result_class);
            ok = ir_lower_test_lower(tally, &sources->functions[f], result_type, program->global_count + program->function_count + 1, &output, &twin_output, &scratch, &functions[f], program_seed, f);
            if (ok)
            {
                const Tier0Function* source = &sources->functions[f];
                entries += source->count;
                instructions += functions[f].instruction_count;
                functions_seen += 1;
                for (uint32_t i = 0; i < source->count; i += 1)
                {
                    copies += source->kinds[i] == BUSTER_TYPED_COPY;
                    if (tier0_moves_stack(source->kinds[i]))
                    {
                        stack_ops[source->kinds[i] - BUSTER_TYPED_ALLOCA] += 1;
                    }
                }
                for (uint32_t l = 0; l < source->local_count; l += 1)
                {
                    objects += tier0_is_object(source, l);
                }
                for (uint32_t i = 0; i < functions[f].instruction_count; i += 1)
                {
                    indirect_calls += functions[f].op[i] == IR_CALL_INDIRECT;
                }
                aggregate_calls += functions[f].abi_call_count;
                for (uint32_t parameter = 0; functions[f].parameter_passed != 0 && parameter < functions[f].parameter_count; parameter += 1)
                {
                    aggregate_parameters += functions[f].passed[functions[f].parameter_passed - 1 + parameter].size != 0;
                }
                register_results += functions[f].result_size != 0;
                hidden_results += functions[f].parameter_passed != 0 && functions[f].result_abi.kind == BUSTER_ABI_RESULT_MEMORY;
                ir_lower_test_promote(tally, &functions[f], &promoted, &twin_promoted, &scratch, program_seed, f);
            }
            arena_pop_to(&twin_output, 0);
        }
        const Tier0TestData data = ok ? tier0_test_layout(program) : (Tier0TestData){ 0 };
        uint64_t arguments[BUSTER_TIER0_TEST_INPUTS][3];
        tier0_test_inputs(program_seed, arguments);
        Tier0TestOutcome outcomes[BUSTER_TIER0_TEST_INPUTS];
        memset(outcomes, 0, sizeof(outcomes));
        if (ok)
        {
            Tier0TestTally native_tally =
            {
                0
            };
            int status = 0;
            code->code = code_bytes;
            code->records = 0;
            code->entries = 0;
            code->deferred = 0;
            ok = tier0_test_build(&native_tally, sources, &native, code, program_seed);
            tally->checks += native_tally.checks;
            tally->failures += native_tally.failures;
            ok = ok && ir_lower_test_expect(tally, tier0_test_run(program, code, &data, arguments, outcomes, &status), "Tier 0's native code exits normally", program_seed, 0);
            arena_pop_to(&native, native_mark);
        }
        for (uint32_t r = 0; ok && r < BUSTER_TIER0_TEST_INPUTS; r += 1)
        {
            bool source_fault = false;
            bool ir_fault = false;
            tier0_test_initial(program, &data, globals);
            const uint64_t expected = tier0_test_interpret(program, sources, &data, globals, arguments[r], &source_fault);
            memcpy(expected_globals, globals, data.size);
            tier0_test_initial(program, &data, globals);
            // The shared IR's interpreter runs the dynamic stack's programs, and the records' without floating values
            // (#788: their calls with ABI records), whose out-of-range conversions it refuses as C does.
            const bool shared = options.vlas || (records && !options.floating);
            const uint64_t actual = shared ? ir_lower_test_shared_interpret(functions, program, &data, globals, arguments[r], !options.vlas, &execution, &ir_fault)
                                           : ir_lower_test_interpret(functions, sources->count, sources->count - 1, program->global_count, &data, globals, arguments[r], 3, &execution, &ir_fault);
            ok = ir_lower_test_expect(tally, !source_fault && !ir_fault, "typed and IR execution finish", program_seed, r);
            ok = ir_lower_test_expect(tally, expected == actual && memcmp(expected_globals, globals, data.size) == 0, "typed and IR results and global bytes agree", program_seed, r) && ok;
            ok = ir_lower_test_expect(tally, outcomes[r].result == actual && outcomes[r].globals == tier0_test_hash(globals, data.size), "native and IR results and global bytes agree", program_seed, r) && ok;
            if (shared)
            {
                tier0_test_initial(program, &data, globals);
                const InterpreterResult typed = ir_lower_test_production_typed(sources, program, &data, globals, arguments[r], !options.vlas, &execution);
                ok = ir_lower_test_expect(tally, typed.status == INTERPRETER_OK && typed.value.low == expected && memcmp(expected_globals, globals, data.size) == 0,
                                         "production typed execution agrees with the fixture, IR and native results and bytes", program_seed, r) && ok;
                if (typed.status != INTERPRETER_OK)
                {
                    fprintf(stderr, "  production typed status %u, function %u, entry %u\n", typed.status, typed.function, typed.entry);
                }
            }
            if (!ok && tally->failures < 20)
            {
                fprintf(stderr, "  expected %016" PRIx64 ", native %016" PRIx64 ", IR %016" PRIx64 ", faults %u/%u\n", expected, outcomes[r].result, actual, source_fault, ir_fault);
            }
        }
        arena_pop_to(&input, 0);
        arena_pop_to(&output, 0);
        arena_pop_to(&twin_output, 0);
    }
    if (options.vlas)
    {
        ir_lower_test_expect(tally, stack_ops[0] != 0 && stack_ops[1] != 0 && stack_ops[2] != 0, "generated execution covers all three stack operations", seed, count);
    }
    if (options.indirect)
    {
        ir_lower_test_expect(tally, indirect_calls != 0, "generated execution covers indirect calls", seed, count);
    }
    if (records)
    {
        ir_lower_test_expect(tally, aggregate_calls != 0 && aggregate_parameters != 0 && register_results != 0 && hidden_results != 0,
                             "generated execution covers aggregate calls, parameters and both kinds of result", seed, count);
        printf("IR lowering generated programs (%s): %" PRIu64 " calls with ABI records, %" PRIu64 " aggregate parameters, %" PRIu64 " results in registers, %" PRIu64
               " through hidden pointers\n", name, aggregate_calls, aggregate_parameters, register_results, hidden_results);
    }
    printf("IR lowering generated programs (%s): %" PRIu64 " functions, %" PRIu64 " typed entries, %" PRIu64 " IR instructions, %" PRIu64 " objects, %" PRIu64 " copies, %" PRIu64
           " indirect calls; stack ops=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "\n", name, functions_seen, entries, instructions, objects, copies, indirect_calls, stack_ops[0],
           stack_ops[1], stack_ops[2]);
    tier0_tables_close(&tables);
    arena_release(&native);
    arena_release(&promoted);
    arena_release(&twin_promoted);
    arena_release(&input);
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
    arena_release(&execution);
}

// The shared IR's interpreter on one lowered body of Tier 0's wide programs: its parameters the addresses of `count`
// buffers, each bound as writable memory, and the x87 programs' C functions as natives at their addresses.
static bool ir_lower_test_wide_run(const IrFunction* function, uint8_t* const* buffers, const uint64_t* sizes, uint32_t count, Arena* execution)
{
    const IrInterpreterFunction code = { .symbol = 1, .function = function };
    InterpreterBinding bindings[4];
    InterpreterValue arguments[4];
    for (uint32_t b = 0; b < count; b += 1)
    {
        bindings[b] = (InterpreterBinding){ .symbol = 2 + b, .bytes = buffers[b], .size = sizes[b], .writable = true };
        arguments[b] = (InterpreterValue){ .low = (uintptr_t)buffers[b] };
    }
    const InterpreterNativeBinding natives[2] = {
        { .symbol = 8, .address = (void*)&tier0_test_x87_mix },
        { .symbol = 9, .address = (void*)&tier0_test_x87_scale },
    };
    const IrInterpreterProgram program = { .functions = &code, .function_count = 1, .bindings = bindings, .binding_count = count, .natives = natives, .native_count = 2 };
    const InterpreterResult result = ir_interpret(&program, 1, arguments, count, 0, execution);
    if (result.status != INTERPRETER_OK)
    {
        fprintf(stderr, "  shared IR status %u, instruction %u\n", result.status, result.entry);
    }
    return result.status == INTERPRETER_OK;
}

// Exercise the original typed columns too: comparing only lowered IR with a private fixture machine cannot check
// the production typed oracle's independent execution or its object-backed/wide local storage (#176).
static bool ir_lower_test_wide_typed_run(const Tier0Function* function, uint8_t* const* buffers, const uint64_t* sizes, uint32_t count, Arena* execution)
{
    const SymbolId symbol = 1;
    InterpreterBinding bindings[4];
    InterpreterValue arguments[4];
    for (uint32_t b = 0; b < count; b += 1)
    {
        bindings[b] = (InterpreterBinding){ .symbol = 2 + b, .bytes = buffers[b], .size = sizes[b], .writable = true };
        arguments[b] = (InterpreterValue){ .low = (uintptr_t)buffers[b] };
    }
    const InterpreterNativeBinding natives[2] = {
        { .symbol = 8, .address = (void*)&tier0_test_x87_mix },
        { .symbol = 9, .address = (void*)&tier0_test_x87_scale },
    };
    const InterpreterProgram program = { .functions = function, .function_symbols = &symbol, .function_count = 1, .bindings = bindings, .binding_count = count, .natives = natives, .native_count = 2 };
    InterpreterResult result;
    const bool ran = interpreter_typed_run(&program, 1, arguments, count, execution, 0, &result);
    if (!ran)
    {
        fprintf(stderr, "  typed wide status %u, entry %u\n", result.status, result.entry);
    }
    return ran;
}

// Long double and the 128-bit integers (#131): Tier 0's own random programs of each class (tier0_x87_test.c and
// tier0_int128_test.c, their inputs drawn as those tests draw them) lowered by both paths and promoted, then run by the
// shared IR's interpreter, every output slot they write compared with their stack machines': the x87 programs' calls of C
// functions through their addresses, NaNs alike, conversions both ways, and the wide integers' arithmetic, shifts,
// compares and conversions, in frame objects and across ?:'s blocks.
static void ir_lower_test_wide(IrLowerTestTally* tally, uint64_t seed, uint32_t count)
{
    Tier0Tables tables =
    {
        0
    };
    Tier0TestInt128Abi int128_abi;
    Tier0TestX87Abi x87_abi;
    Tier0TestInt128Program* wide = calloc(1, sizeof(Tier0TestInt128Program));
    Tier0TestX87Program* x87 = calloc(1, sizeof(Tier0TestX87Program));
    Arena output = arena_reserve(1ull << 26);
    Arena twin_output = arena_reserve(1ull << 26);
    Arena promoted = arena_reserve(1ull << 26);
    Arena twin_promoted = arena_reserve(1ull << 26);
    Arena scratch = arena_reserve(1ull << 26);
    Arena execution = arena_reserve(1ull << 26);
    const bool ready = wide != 0 && x87 != 0 && output.base != 0 && twin_output.base != 0 && promoted.base != 0 && twin_promoted.base != 0 && scratch.base != 0 &&
                       execution.base != 0 && tier0_tables_open(&tables) && tier0_test_int128_abi(&tables, &int128_abi) && tier0_test_x87_abi(&tables, &x87_abi);
    ir_lower_test_expect(tally, ready, "wide program arenas and signatures", seed, 0);
    uint64_t checked[2] = { 0 };
    for (uint32_t p = 0; ready && p < count; p += 1)
    {
        uint64_t state = seed ^ (0x128ull << 32) ^ p;
        tier0_test_int128_program(wide, &state);
        const Tier0Function source = tier0_test_int128_function(wide, &int128_abi);
        IrFunction function =
        {
            0
        };
        if (!wide->overflow && ir_lower_test_lower(tally, &source, IR_VOID, 1, &output, &twin_output, &scratch, &function, seed, p))
        {
            ir_lower_test_promote(tally, &function, &promoted, &twin_promoted, &scratch, seed, p);
            Tier0TestWide in[BUSTER_TIER0_TEST_INT128_IN];
            uint64_t ints[BUSTER_TIER0_TEST_INT128_INTS];
            uint8_t out[BUSTER_TIER0_TEST_INT128_OUTPUTS * 16];
            uint8_t expected[sizeof(out)];
            for (uint32_t k = 0; k < BUSTER_TIER0_TEST_INT128_IN; k += 1)
            {
                in[k] = tier0_test_int128_random(&state);
            }
            for (uint32_t k = 0; k < BUSTER_TIER0_TEST_INT128_INTS; k += 1)
            {
                ints[k] = tier0_test_int128_random64(&state);
            }
            memset(out, 0xa5, sizeof(out));
            memset(expected, 0xa5, sizeof(expected));
            const uint64_t parameters[3] = { (uintptr_t)in, (uintptr_t)ints, (uintptr_t)expected };
            uint8_t visited[BUSTER_TIER0_TEST_INT128_ENTRIES];
            uint8_t* buffers[3] = { (uint8_t*)in, (uint8_t*)ints, out };
            const uint64_t sizes[3] = { sizeof(in), sizeof(ints), sizeof(out) };
            const bool interpreted = tier0_test_int128_interpret(wide, parameters, visited);
            const bool ran = interpreted && ir_lower_test_wide_run(&function, buffers, sizes, 3, &execution);
            bool same = ir_lower_test_expect(tally, interpreted && ran, "the shared IR's interpreter runs a 128-bit program", seed, p);
            for (uint32_t o = 0; same && o < BUSTER_TIER0_TEST_INT128_OUTPUTS; o += 1)
            {
                same = memcmp(out + 16 * o, expected + 16 * o, tier0_class_bytes(wide->outputs[o])) == 0;
            }
            checked[0] += ir_lower_test_expect(tally, same, "a 128-bit program's outputs are its stack machine's", seed, p);
            memset(out, 0xa5, sizeof(out));
            const bool typed_ran = interpreted && ir_lower_test_wide_typed_run(&source, buffers, sizes, 3, &execution);
            same = ir_lower_test_expect(tally, typed_ran, "the production typed interpreter runs a 128-bit program", seed, p);
            for (uint32_t o = 0; same && o < BUSTER_TIER0_TEST_INT128_OUTPUTS; o += 1)
            {
                same = memcmp(out + 16 * o, expected + 16 * o, tier0_class_bytes(wide->outputs[o])) == 0;
            }
            ir_lower_test_expect(tally, same, "typed 128-bit outputs match the independent stack machine", seed, p);
        }
        arena_pop_to(&output, 0);
        arena_pop_to(&twin_output, 0);
        state = seed ^ (0x87ull << 32) ^ p;
        tier0_test_x87_program(x87, &x87_abi, &state);
        const Tier0Function long_double = tier0_test_x87_function(x87, &x87_abi);
        if (!x87->overflow && ir_lower_test_lower(tally, &long_double, IR_VOID, 1, &output, &twin_output, &scratch, &function, seed, p))
        {
            ir_lower_test_promote(tally, &function, &promoted, &twin_promoted, &scratch, seed, p);
            long double in[BUSTER_TIER0_TEST_X87_IN];
            uint64_t ints[BUSTER_TIER0_TEST_X87_INTS];
            uint64_t floats[BUSTER_TIER0_TEST_X87_FLOATS];
            uint8_t out[BUSTER_TIER0_TEST_X87_OUTPUTS * 16];
            uint8_t expected[sizeof(out)];
            memset(in, 0, sizeof(in));
            for (uint32_t k = 0; k < BUSTER_TIER0_TEST_X87_IN; k += 1)
            {
                in[k] = tier0_test_x87_random(&state, true);
            }
            for (uint32_t k = 0; k < BUSTER_TIER0_TEST_X87_INTS; k += 1)
            {
                const uint64_t random = tier0_generate_random(&state);
                ints[k] = k < 4 ? random % 9 : random;
            }
            for (uint32_t k = 0; k < BUSTER_TIER0_TEST_X87_FLOATS; k += 1)
            {
                const double d = (double)(int64_t)tier0_generate_random(&state) / 4096.0;
                const float f = (float)d;
                uint32_t f_bits = 0;
                memcpy(&f_bits, &f, sizeof(f_bits));
                floats[k] = 0;
                memcpy(&floats[k], k >= 8 ? (const void*)&f_bits : (const void*)&d, k >= 8 ? 4 : 8);
            }
            memset(out, 0xa5, sizeof(out));
            memset(expected, 0xa5, sizeof(expected));
            const uint64_t parameters[4] = { (uintptr_t)in, (uintptr_t)ints, (uintptr_t)floats, (uintptr_t)expected };
            uint8_t visited[BUSTER_TIER0_TEST_X87_ENTRIES];
            uint8_t* buffers[4] = { (uint8_t*)in, (uint8_t*)ints, (uint8_t*)floats, out };
            const uint64_t sizes[4] = { sizeof(in), sizeof(ints), sizeof(floats), sizeof(out) };
            const bool interpreted = tier0_test_x87_interpret(x87, parameters, visited);
            const bool ran = interpreted && ir_lower_test_wide_run(&function, buffers, sizes, 4, &execution);
            bool same = ir_lower_test_expect(tally, interpreted && ran, "the shared IR's interpreter runs a long double program", seed, p);
            for (uint32_t o = 0; same && o < BUSTER_TIER0_TEST_X87_OUTPUTS; o += 1)
            {
                same = tier0_test_x87_same_output(x87->outputs[o], out + 16 * o, expected + 16 * o);
            }
            checked[1] += ir_lower_test_expect(tally, same, "a long double program's outputs are its stack machine's", seed, p);
            memset(out, 0xa5, sizeof(out));
            const bool typed_ran = interpreted && ir_lower_test_wide_typed_run(&long_double, buffers, sizes, 4, &execution);
            same = ir_lower_test_expect(tally, typed_ran, "the production typed interpreter runs a long double program", seed, p);
            for (uint32_t o = 0; same && o < BUSTER_TIER0_TEST_X87_OUTPUTS; o += 1)
            {
                same = tier0_test_x87_same_output(x87->outputs[o], out + 16 * o, expected + 16 * o);
            }
            ir_lower_test_expect(tally, same, "typed long-double outputs match the independent stack machine", seed, p);
        }
        arena_pop_to(&output, 0);
        arena_pop_to(&twin_output, 0);
    }
    printf("IR lowering wide programs: %" PRIu64 " of 128-bit integers and %" PRIu64 " of long doubles checked against their stack machines\n", checked[0], checked[1]);
    tier0_tables_close(&tables);
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&promoted);
    arena_release(&twin_promoted);
    arena_release(&scratch);
    arena_release(&execution);
    free(x87);
    free(wide);
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

// The objects and results of ir_lower_test_cas_native's child, on a page the parent shares.
typedef struct IrLowerTestCasShared IrLowerTestCasShared;
struct IrLowerTestCasShared
{
    int32_t object;
    uint32_t results[2];
};

// Runs a function's code (`length` bytes) in a child, which must exit with status 0: mapped executable at a page of its
// own and called twice from C on the shared object, its address in rdi, or with `second` in rsi and rdi 0.
static __attribute__((no_sanitize("function"))) bool ir_lower_test_cas_native(const uint8_t* code, uint32_t length, bool second, IrLowerTestCasShared* shared)
{
    const pid_t child = fork();
    if (child == 0)
    {
        alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
        uint8_t* page = mmap(0, BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED || length > BUSTER_TIER0_TEST_PAGE)
        {
            _exit(3);
        }
        memcpy(page, code, length);
        if (mprotect(page, BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_EXEC) != 0)
        {
            _exit(4);
        }
        typedef uint64_t (*Body)(uint64_t, uint64_t);
        Body body = 0;
        const void* entry = page;
        memcpy(&body, &entry, sizeof(body));
        const uint64_t address = (uint64_t)(uintptr_t)&shared->object;
        for (uint32_t r = 0; r < 2; r += 1)
        {
            shared->results[r] = (uint32_t)(second ? body(0, address) : body(address, 0));
        }
        _exit(0);
    }
    int status = 0;
    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
    return waited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// #1246's review: an ATOMIC_CAS's address local is a use of that local, which Tier 0's homes count
// (tier0_count_references and its vector twin), so that a pointer parameter whose only use is a compare-exchange's
// address gets its home and its incoming register's save. Two functions whose pointer parameter is used only so: the
// first's local 0 at depth 2 (its expected value is rax's), the second's local 1 (local 0 another parameter, unused) at
// depth 3 under a value it keeps (its expected value swapped into rax). Each is compiled by both Tier 0 paths into the
// same code, run natively twice on an int of 5 (cas 5, 9: the first succeeds and stores 9, the second fails), and run so
// by the typed interpreter and, through both IR lowerings, by the IR interpreter: each its old values (plus 100, the
// second's) and the int.
static void ir_lower_test_cas_parameter(IrLowerTestTally* tally)
{
    static const uint8_t kinds[2][6] = {
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CONST, BUSTER_TYPED_ATOMIC_CAS, BUSTER_TYPED_RETURN },
        { BUSTER_TYPED_CONST, BUSTER_TYPED_CONST, BUSTER_TYPED_CONST, BUSTER_TYPED_ATOMIC_CAS, BUSTER_TYPED_ADD, BUSTER_TYPED_RETURN },
    };
    static const uint8_t classes[6] = { BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32 };
    static const uint64_t values[2][6] = {
        { 5, 9, BUSTER_TYPED_FENCE_SEQ_CST, 0 },
        { 100, 5, 9, 2 | UINT64_C(1) << BUSTER_TYPED_CAS_LOCAL_SHIFT, 0, 0 },
    };
    static const uint32_t counts[2] = { 4, 6 };
    static const uint32_t expected[2][2] = { { 5, 9 }, { 105, 109 } };
    static const uint8_t local_classes[2] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
    static const uint8_t local_flags[2] = { 0, 0 };
    static const AbiLocation parameters[2] = {
        { .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        { .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
    };
    Arena arena = arena_reserve(1ull << 24);
    Arena output = arena_reserve(1ull << 20);
    Arena twin_output = arena_reserve(1ull << 20);
    Arena execution = arena_reserve(1ull << 30);
    IrLowerTestCasShared* shared = mmap(0, sizeof(IrLowerTestCasShared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ir_lower_test_expect(tally, shared != MAP_FAILED, "a compare-exchange parameter's shared page", 0, 0);
    for (uint32_t f = 0; f < 2 && shared != MAP_FAILED; f += 1)
    {
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = kinds[f];
        function.classes = classes;
        function.values = values[f];
        function.count = counts[f];
        function.local_classes = local_classes;
        function.local_flags = local_flags;
        function.local_count = f + 1;
        function.parameter_count = f + 1;
        function.parameters = parameters;
        const bool second = f == 1;
        const Tier0Check check = tier0_validate(&function);
        const Tier0Code scalar = tier0_compile_scalar(&function, &arena);
        const Tier0Code vector = tier0_compile(&function, &arena);
        bool ready = ir_lower_test_expect(tally, check.error == TIER0_OK && scalar.error == TIER0_OK && vector.error == TIER0_OK && tier0_test_same_code(&scalar, &vector),
                                          "a compare-exchange whose address is a parameter's only use compiles alike by both Tier 0 paths", 0, f);
        ready = ready && ir_lower_test_expect(tally, x86_validate(scalar.records, scalar.count) == scalar.count && scalar.branch_count == 0 && scalar.reference_count == 0,
                                              "a compare-exchange parameter's records are valid, without branches or references", 0, f);
        const uint64_t room = ready ? BUSTER_X86_ENCODER_CAPACITY(scalar.count) : 0;
        uint8_t* image = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, room) : 0;
        uint8_t* twin = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint8_t, room) : 0;
        const int64_t length = image != 0 ? x86_encode(vector.records, vector.count, image, room, 0, 0) : -1;
        const int64_t twin_length = twin != 0 ? x86_encode_scalar(scalar.records, scalar.count, twin, room, 0, 0) : -1;
        ready = ready && ir_lower_test_expect(tally, length > 0 && length == twin_length && memcmp(image, twin, (size_t)length) == 0,
                                              "a compare-exchange parameter's code encodes alike by both encoders", 0, f);
        if (ready)
        {
            shared->object = 5;
            const bool ran = ir_lower_test_cas_native(image, (uint32_t)length, second, shared);
            ir_lower_test_expect(tally, ran && shared->results[0] == expected[f][0] && shared->results[1] == expected[f][1] && shared->object == 9,
                                 "Tier 0's compare-exchange reads its address from a parameter used only so", 0, f);
            if (!(ran && shared->results[0] == expected[f][0] && shared->results[1] == expected[f][1] && shared->object == 9) && tally->failures < 20)
            {
                fprintf(stderr, "  ran %d, results %u and %u, object %d\n", ran, shared->results[0], shared->results[1], shared->object);
            }
        }
        // The interpreters, the object's address unchecked: it is no memory they know.
        InterpreterOptions options = interpreter_default_options();
        options.check_memory = false;
        int32_t object = 5;
        const InterpreterValue arguments[2] = {
            { .low = second ? 0 : (uint64_t)(uintptr_t)&object },
            { .low = (uint64_t)(uintptr_t)&object },
        };
        const SymbolId symbol = 1;
        const InterpreterProgram typed = {
            .functions = &function,
            .function_symbols = &symbol,
            .function_count = 1,
        };
        bool same = true;
        for (uint32_t r = 0; r < 2; r += 1)
        {
            InterpreterResult result;
            const bool interpreted = interpreter_typed_run(&typed, symbol, arguments, f + 1, &execution, &options, &result);
            same = same && interpreted && result.status == INTERPRETER_OK && (uint32_t)result.value.low == expected[f][r];
        }
        ir_lower_test_expect(tally, same && object == 9, "the typed interpreter's compare-exchange reads its address from a parameter used only so", 0, f);
        IrFunction lowered = { 0 };
        if (ir_lower_test_lower(tally, &function, IR_I32, 1, &output, &twin_output, &arena, &lowered, 0, f))
        {
            object = 5;
            const IrInterpreterFunction code = { .symbol = symbol, .function = &lowered };
            const IrInterpreterProgram program = { .functions = &code, .function_count = 1 };
            same = true;
            for (uint32_t r = 0; r < 2; r += 1)
            {
                const InterpreterResult result = ir_interpret(&program, symbol, arguments, f + 1, &options, &execution);
                same = same && result.status == INTERPRETER_OK && (uint32_t)result.value.low == expected[f][r];
            }
            ir_lower_test_expect(tally, same && object == 9, "the IR interpreter's compare-exchange reads its address from a parameter used only so", 0, f);
        }
        arena_pop_to(&output, 0);
        arena_pop_to(&twin_output, 0);
        arena_pop_to(&execution, 0);
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(IrLowerTestCasShared));
    }
    arena_release(&execution);
    arena_release(&twin_output);
    arena_release(&output);
    arena_release(&arena);
}

// The static chain's sum with its argument (#148): r10, where a chained call passes it, plus rdi.
static __attribute__((naked, noinline)) int64_t ir_lower_test_chain_plus(int64_t x __attribute__((unused)))
{
    __asm__("leaq (%r10,%rdi), %rax\n\tret\n");
}

// The static chain's sum with its signed byte argument as the caller extended it (#1411's review): r10 plus edi's 32 bits
// sign-extended, -3 where the caller sign-extended the byte. The typed stream's lowering widens it to 32 bits by its class
// before the call (ir_typed_boundary_class); ir_interpreter_test.c's chained call of an i8 observes the record's flag.
static __attribute__((naked, noinline)) int64_t ir_lower_test_chain_byte(int8_t x __attribute__((unused)))
{
    __asm__("movslq %edi, %rax\n\taddq %r10, %rax\n\tret\n");
}

// A call with a static chain (#148): its argument 0 in r10, which the IR keeps as its ABI record (ir_typed_aggregate_call)
// on both lowerings, and which both interpreters pass to a native callee in r10: f() returns chain_plus(2) with the
// chain 40, 42. And (#1411's review) the IR interpreter keeps the other arguments' records, a signed byte's extension
// flag among them, with no extensions of the native's, and refuses an undefined chain where it checks definedness.
static void ir_lower_test_chain(IrLowerTestTally* tally)
{
    const uint8_t kinds[] = {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CALL,
        BUSTER_TYPED_RETURN,
    };
    const uint8_t classes[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
    };
    const uint64_t values[] = {
        40,
        2,
        0,
        0,
    };
    const uint8_t argument_classes[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I64,
    };
    const AbiLocation locations[] = {
        { .registers = { BUSTER_ABI_R10, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        { .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
    };
    const AbiCall call = { .integer_registers = 1, .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .result = { .kind = BUSTER_ABI_RESULT_REGISTERS, .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE } } };
    const AbiReturn result = { .kind = BUSTER_ABI_RESULT_REGISTERS, .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE } };
    const SymbolId native_symbol = 2;
    const uint32_t first = 0;
    const uint32_t count = 2;
    const uint8_t variadic = 0;
    const Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .result = result,
        .call_targets = &native_symbol,
        .call_first = &first,
        .call_argument_counts = &count,
        .call_variadic = &variadic,
        .calls = &call,
        .argument_locations = locations,
        .argument_classes = argument_classes,
        .call_count = 1,
        .argument_count = 2,
    };
    Arena output = arena_reserve(1ull << 20);
    Arena twin_output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    Arena execution = arena_reserve(1ull << 28);
    const InterpreterNativeBinding native = { .symbol = native_symbol, .address = (void*)ir_lower_test_chain_plus };
    const SymbolId symbol = 1;
    const InterpreterProgram typed = { .functions = &function, .function_symbols = &symbol, .function_count = 1, .natives = &native, .native_count = 1 };
    InterpreterResult typed_result;
    const bool interpreted = interpreter_typed_run(&typed, symbol, 0, 0, &execution, 0, &typed_result);
    ir_lower_test_expect(tally, interpreted && typed_result.value.low == 42, "the typed interpreter passes a static chain in r10", 0, 0);
    IrFunction lowered = { 0 };
    if (ir_lower_test_lower(tally, &function, IR_I64, 3, &output, &twin_output, &scratch, &lowered, 0, 0))
    {
        const IrCall* lowered_call = lowered.call_count == 1 ? &lowered.calls[0] : 0;
        const bool recorded = lowered_call != 0 && lowered_call->abi != 0 && lowered.passed[lowered.abi_calls[lowered_call->abi - 1].passed].location.registers[0] == BUSTER_ABI_R10;
        ir_lower_test_expect(tally, recorded, "a static chain's call keeps its r10 record in the IR", 0, 0);
        const IrInterpreterFunction code = { .symbol = symbol, .function = &lowered };
        const IrInterpreterProgram program = { .functions = &code, .function_count = 1, .natives = &native, .native_count = 1 };
        const InterpreterResult ir_result = ir_interpret(&program, symbol, 0, 0, 0, &execution);
        ir_lower_test_expect(tally, ir_result.status == INTERPRETER_OK && ir_result.value.low == 42, "the IR interpreter passes a static chain in r10", 0, 0);
        // An undefined chain (#1411's review): its constant made IR_UNDEF.
        const uint32_t chain = lowered_call != 0 ? lowered.extra[lowered_call->argument_first] : BUSTER_IR_NONE;
        if (chain < lowered.instruction_count)
        {
            lowered.op[chain] = IR_UNDEF;
            lowered.a[chain] = BUSTER_IR_NONE;
            InterpreterOptions checked = interpreter_default_options();
            checked.check_undefined = true;
            const InterpreterResult undefined = ir_interpret(&program, symbol, 0, 0, &checked, &execution);
            ir_lower_test_expect(tally, ir_verify(&lowered, &scratch, 0).error == IR_OK && undefined.status == INTERPRETER_UNDEFINED,
                                 "the IR interpreter refuses an undefined static chain where it checks definedness", 0, 0);
        }
    }
    // A signed byte's argument beside the chain: -3, sign-extended by its record's flag, 37.
    arena_pop_to(&output, 0);
    arena_pop_to(&twin_output, 0);
    const uint8_t byte_classes[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I8,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
    };
    const uint64_t byte_values[] = {
        40,
        (uint64_t)-3,
        0,
        0,
    };
    const uint8_t byte_arguments[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I8,
    };
    const AbiLocation byte_locations[] = {
        locations[0],
        { .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS, .flags = BUSTER_ABI_FLAG_SIGN },
    };
    Tier0Function bytes = function;
    bytes.classes = byte_classes;
    bytes.values = byte_values;
    bytes.argument_classes = byte_arguments;
    bytes.argument_locations = byte_locations;
    const InterpreterNativeBinding byte_native = { .symbol = native_symbol, .address = (void*)ir_lower_test_chain_byte };
    IrFunction byte_lowered = { 0 };
    if (ir_lower_test_lower(tally, &bytes, IR_I64, 3, &output, &twin_output, &scratch, &byte_lowered, 0, 1))
    {
        const IrInterpreterFunction code = { .symbol = symbol, .function = &byte_lowered };
        const IrInterpreterProgram program = { .functions = &code, .function_count = 1, .natives = &byte_native, .native_count = 1 };
        const InterpreterResult byte_result = ir_interpret(&program, symbol, 0, 0, 0, &execution);
        ir_lower_test_expect(tally, byte_result.status == INTERPRETER_OK && byte_result.value.low == 37,
                             "the IR interpreter keeps a chained call's other records: a signed byte's extension", 0, 1);
    }
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
    arena_release(&execution);
}

// An aggregate of no size passed on the stack (#1879): a record of a flexible array member alone is MEMORY, and clang
// passes it byval in an 8-byte slot, its value (its address, argument_sizes 0) in no register, before a long in rdi.
// A call without records would place the address by its class in rdi, so the IR keeps the call's records
// (ir_typed_aggregate_call), the slot's and rdi's. A seventh long on the stack, all six general registers taken before
// it, is where its class puts it, and that call keeps none.
static void ir_lower_test_empty_stack(IrLowerTestTally* tally)
{
    const uint8_t kinds[] = {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CALL,
        BUSTER_TYPED_RETURN,
    };
    const uint8_t classes[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64,
    };
    const uint64_t values[] = {
        4096,
        7,
        0,
        0,
    };
    const uint8_t argument_classes[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I64,
    };
    const uint32_t argument_sizes[] = {
        0,
        0,
    };
    const AbiLocation locations[] = {
        { .stack_offset = 0, .registers = { BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_STACK },
        { .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
    };
    const AbiCall call = { .stack_bytes = 8, .integer_registers = 1, .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .result = { .kind = BUSTER_ABI_RESULT_REGISTERS, .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE } } };
    const AbiReturn result = { .kind = BUSTER_ABI_RESULT_REGISTERS, .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE } };
    const SymbolId target = 2;
    const uint32_t first = 0;
    const uint32_t count = 2;
    const uint8_t variadic = 0;
    const Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .result = result,
        .call_targets = &target,
        .call_first = &first,
        .call_argument_counts = &count,
        .call_variadic = &variadic,
        .calls = &call,
        .argument_locations = locations,
        .argument_classes = argument_classes,
        .argument_sizes = argument_sizes,
        .call_count = 1,
        .argument_count = 2,
    };
    Arena output = arena_reserve(1ull << 20);
    Arena twin_output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    IrFunction lowered = { 0 };
    if (ir_lower_test_lower(tally, &function, IR_I64, 3, &output, &twin_output, &scratch, &lowered, 0, 0))
    {
        const IrCall* lowered_call = lowered.call_count == 1 ? &lowered.calls[0] : 0;
        const IrPassed* passed = lowered_call != 0 && lowered_call->abi != 0 ? &lowered.passed[lowered.abi_calls[lowered_call->abi - 1].passed] : 0;
        const bool recorded = passed != 0 && passed[0].location.kind == BUSTER_ABI_LOCATION_STACK && passed[0].size == 0 &&
                              passed[1].location.kind == BUSTER_ABI_LOCATION_REGISTERS && passed[1].location.registers[0] == BUSTER_ABI_RDI;
        ir_lower_test_expect(tally, recorded, "a call of an aggregate of no size on the stack keeps its records in the IR", 0, 0);
    }
    // Seven longs: rdi to r9, then the stack.
    arena_pop_to(&output, 0);
    arena_pop_to(&twin_output, 0);
    uint8_t long_kinds[9];
    uint8_t long_classes[9];
    uint64_t long_values[9];
    uint8_t long_arguments[7];
    uint32_t long_sizes[7];
    AbiLocation long_locations[7];
    for (uint32_t a = 0; a < 7; a += 1)
    {
        long_kinds[a] = BUSTER_TYPED_CONST;
        long_classes[a] = BUSTER_TYPED_CLASS_I64;
        long_values[a] = a;
        long_arguments[a] = BUSTER_TYPED_CLASS_I64;
        long_sizes[a] = 0;
        long_locations[a] = a < BUSTER_ABI_INTEGER_ARGUMENTS
                                ? (AbiLocation){ .registers = { abi_integer_arguments[a], BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS }
                                : (AbiLocation){ .stack_offset = 0, .registers = { BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_STACK };
    }
    long_kinds[7] = BUSTER_TYPED_CALL;
    long_classes[7] = BUSTER_TYPED_CLASS_I64;
    long_values[7] = 0;
    long_kinds[8] = BUSTER_TYPED_RETURN;
    long_classes[8] = BUSTER_TYPED_CLASS_I64;
    long_values[8] = 0;
    const uint32_t long_count = 7;
    const AbiCall long_call = { .stack_bytes = 8, .integer_registers = 6, .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2, .result = call.result };
    Tier0Function longs = function;
    longs.kinds = long_kinds;
    longs.classes = long_classes;
    longs.values = long_values;
    longs.count = 9;
    longs.call_argument_counts = &long_count;
    longs.calls = &long_call;
    longs.argument_locations = long_locations;
    longs.argument_classes = long_arguments;
    longs.argument_sizes = long_sizes;
    longs.argument_count = 7;
    IrFunction long_lowered = { 0 };
    if (ir_lower_test_lower(tally, &longs, IR_I64, 3, &output, &twin_output, &scratch, &long_lowered, 0, 1))
    {
        ir_lower_test_expect(tally, long_lowered.call_count == 1 && long_lowered.calls[0].abi == 0, "a seventh long on the stack keeps no records", 0, 1);
    }
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
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

// A variadic definition's frame (#148): long f(long n, ...), whose va_list's overflow_arg_area takes the overflow area's
// address and whose result is the register save area's second general slot, its first unnamed argument. Both paths
// give the IR its frame and its three anchors as their slots, which promotion keeps and an all-integer call fills; each
// break of the roles' rules is tier0_validate's, refused before allocation.
static void ir_lower_test_local_roles(IrLowerTestTally* tally)
{
    Arena output = arena_reserve(1ull << 20);
    Arena twin_output = arena_reserve(1ull << 20);
    Arena promoted = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    Arena execution = arena_reserve(1ull << 20);
    static const uint8_t kinds[] =
    {
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_PTR_ADD,
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_SET,
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_PTR_ADD,
        BUSTER_TYPED_LOAD,
        BUSTER_TYPED_RETURN
    };
    static const uint8_t classes[] =
    {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I64
    };
    static const uint64_t values[] =
    {
        1,
        1,
        8,
        3,
        0,
        2,
        1,
        8,
        0,
        0
    };
    uint8_t local_classes[4] =
    {
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64
    };
    uint8_t local_flags[4] =
    {
        0
    };
    const AbiLocation parameter =
    {
        .kind = BUSTER_ABI_LOCATION_REGISTERS,
        .registers =
        {
            X86_REGISTER_RDI,
            BUSTER_ABI_REGISTER_NONE
        },
    };
    const AbiCall incoming =
    {
        .stack_alignment_log2 = 4,
        .integer_registers = 1,
        .result =
        {
            .kind = BUSTER_ABI_RESULT_REGISTERS,
            .part_bytes = 8,
            .registers =
            {
                BUSTER_ABI_RAX,
                BUSTER_ABI_REGISTER_NONE,
                BUSTER_ABI_REGISTER_NONE,
                BUSTER_ABI_REGISTER_NONE
            },
        },
    };
    Tier0Function source =
    {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 4,
        .parameter_count = 1,
        .parameters = &parameter,
        .incoming_variadic = &incoming,
    };
    const IrFunction preserved =
    {
        .instruction_count = 123,
        .block_count = 17,
    };
    const bool ready = output.base != 0 && twin_output.base != 0 && promoted.base != 0 && scratch.base != 0 && execution.base != 0;
    ir_lower_test_expect(tally, ready, "variadic frame arenas", 0, 0);
    const uint8_t roles[3] =
    {
        BUSTER_TYPED_LOCAL_VA_LIST,
        BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA,
        BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA
    };
    for (uint32_t r = 0; r < 3; r += 1)
    {
        local_flags[1 + r] = roles[r] | BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
    }
    IrFunction function =
    {
        0
    };
    if (ready && ir_lower_test_lower(tally, &source, IR_I64, 1, &output, &twin_output, &scratch, &function, 0, 0))
    {
        const IrSlot list = function.slots[1];
        const IrSlot save = function.slots[2];
        const IrSlot overflow = function.slots[3];
        ir_lower_test_expect(tally, function.variadic && memcmp(&function.incoming, &incoming, sizeof(incoming)) == 0, "the incoming variadic frame is the stream's", 0, 0);
        ir_lower_test_expect(tally, list.size == sizeof(AbiVaList) && list.alignment == 8 && list.type == IR_VOID && list.flags == BUSTER_IR_SLOT_ADDRESS_TAKEN &&
                                        save.size == BUSTER_ABI_SAVE_AREA_BYTES && save.alignment == 16 && save.type == IR_VOID &&
                                        save.flags == (BUSTER_IR_SLOT_ADDRESS_TAKEN | BUSTER_IR_SLOT_REGISTER_SAVE_AREA) && overflow.size == 0 && overflow.alignment == 16 &&
                                        overflow.type == IR_VOID && overflow.flags == (BUSTER_IR_SLOT_ADDRESS_TAKEN | BUSTER_IR_SLOT_OVERFLOW_AREA),
                             "a va_list is its 24 bytes, and the anchors their slots", 0, 0);
        const uint64_t arguments[2] =
        {
            5,
            42
        };
        bool fault = false;
        const Tier0TestData data =
        {
            0
        };
        uint8_t globals[1] =
        {
            0
        };
        const uint64_t actual = ir_lower_test_interpret(&function, 1, 0, 0, &data, globals, arguments, 2, &execution, &fault);
        ir_lower_test_expect(tally, !fault && actual == 42, "the register save area holds the first unnamed argument", 0, 0);
        IrFunction promotion =
        {
            0
        };
        const IrCheck check = ssa_ir_promote(&function, &promoted, &scratch, &promotion);
        bool anchors = check.error == IR_OK && promotion.variadic;
        for (uint32_t slot = 0; anchors && slot < promotion.slot_count; slot += 1)
        {
            anchors = promotion.slots[slot].flags != (BUSTER_IR_SLOT_ADDRESS_TAKEN | BUSTER_IR_SLOT_REGISTER_SAVE_AREA) || promotion.slots[slot].size == BUSTER_ABI_SAVE_AREA_BYTES;
        }
        ir_lower_test_expect(tally, anchors && promotion.slot_count >= 3, "promotion keeps the frame and its anchors", 0, 0);
    }
    // Each break of the roles' rules: a role on a parameter, without ADDRESS_TAKEN, two roles at once, an anchor without
    // the incoming frame, a role on a local of another class.
    for (uint32_t vector = 0; ready && vector < 2; vector += 1)
    {
        for (uint32_t breach = 0; breach < 5; breach += 1)
        {
            for (uint32_t r = 0; r < 3; r += 1)
            {
                local_flags[1 + r] = roles[r] | BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
            }
            local_flags[0] = breach == 0 ? BUSTER_TYPED_LOCAL_VA_LIST | BUSTER_TYPED_LOCAL_ADDRESS_TAKEN : 0;
            local_flags[2] = breach == 1 ? BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA : breach == 2 ? local_flags[2] | BUSTER_TYPED_LOCAL_VA_LIST : local_flags[2];
            source.incoming_variadic = breach == 3 ? 0 : &incoming;
            local_classes[3] = breach == 4 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_U64;
            const uint64_t output_mark = output.length;
            const uint64_t scratch_mark = scratch.length;
            IrFunction result = preserved;
            const IrCheck check = ir_lower_typed_path(&source, IR_I64, 1, &output, &scratch, &result, vector != 0);
            ir_lower_test_expect(tally, check.error == IR_ERROR_STRUCTURE && output.length == output_mark && scratch.length == scratch_mark &&
                                            result.instruction_count == preserved.instruction_count && result.block_count == preserved.block_count,
                                 "a break of the frame roles' rules refused without allocation", breach, vector);
        }
        source.incoming_variadic = &incoming;
        local_classes[3] = BUSTER_TYPED_CLASS_U64;
        local_flags[0] = 0;
    }
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&promoted);
    arena_release(&scratch);
    arena_release(&execution);
}

// Structs and unions by value (#137): struct S f(struct S s, long x) { struct S t = g(s); return t; }, S of 16 bytes in
// rdi and rsi, returned in rax and rdx. Both paths give the IR the function's records (its parameters' locations and
// sizes, its result's ABI and size) and the call's (its totals, its argument's, its result object's size), s a ptr whose
// bytes the preheader copies into s's object, the call's destination t's slot address, and a ptr result. A null input
// is a structural error, and so are aggregates without their calls' columns and each break of the aggregates' rules,
// tier0_validate's, refused before allocation.
static void ir_lower_test_aggregates(IrLowerTestTally* tally)
{
    Arena output = arena_reserve(1ull << 20);
    Arena twin_output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    static const uint8_t kinds[] =
    {
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_CALL,
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_RETURN
    };
    static const uint8_t classes[] =
    {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_VOID,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64
    };
    static const uint64_t values[] =
    {
        0,
        0,
        2,
        0
    };
    static const uint8_t local_classes[3] =
    {
        BUSTER_TYPED_CLASS_VOID,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_VOID
    };
    static const uint8_t local_flags[3] =
    {
        BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
        0,
        BUSTER_TYPED_LOCAL_ADDRESS_TAKEN
    };
    static const uint32_t local_sizes[3] =
    {
        16,
        0,
        16
    };
    static const uint8_t local_alignments[3] =
    {
        3,
        0,
        3
    };
    const AbiLocation pair =
    {
        .kind = BUSTER_ABI_LOCATION_REGISTERS,
        .registers =
        {
            X86_REGISTER_RDI,
            X86_REGISTER_RSI
        },
    };
    const AbiLocation parameters[2] =
    {
        pair,
        {
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
            .registers =
            {
                X86_REGISTER_RDX,
                BUSTER_ABI_REGISTER_NONE
            },
        },
    };
    const AbiReturn returned =
    {
        .kind = BUSTER_ABI_RESULT_REGISTERS,
        .part_bytes = 8,
        .registers =
        {
            BUSTER_ABI_RAX,
            BUSTER_ABI_RDX,
            BUSTER_ABI_REGISTER_NONE,
            BUSTER_ABI_REGISTER_NONE
        },
    };
    AbiCall calls[1] =
    {
        {
            .stack_alignment_log2 = 4,
            .integer_registers = 2,
            .result = returned,
        },
    };
    static const SymbolId call_targets[1] =
    {
        1
    };
    static const uint32_t call_first[1] =
    {
        0
    };
    static const uint32_t call_argument_counts[1] =
    {
        1
    };
    static const uint8_t call_variadic[1] =
    {
        0
    };
    static const uint8_t argument_classes[1] =
    {
        BUSTER_TYPED_CLASS_U64
    };
    AbiLocation argument_locations[1] =
    {
        pair
    };
    static const uint32_t argument_sizes[1] =
    {
        16
    };
    uint32_t call_result_objects[1] =
    {
        3
    };
    Tier0Function source =
    {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 3,
        .parameter_count = 2,
        .parameters = parameters,
        .result = returned,
        .call_targets = call_targets,
        .call_first = call_first,
        .call_argument_counts = call_argument_counts,
        .call_variadic = call_variadic,
        .calls = calls,
        .argument_locations = argument_locations,
        .argument_classes = argument_classes,
        .call_count = 1,
        .argument_count = 1,
        .local_sizes = local_sizes,
        .local_alignments = local_alignments,
        .argument_sizes = argument_sizes,
        .call_result_objects = call_result_objects,
        .result_size = 16,
    };
    const IrFunction preserved =
    {
        .instruction_count = 123,
        .block_count = 17,
    };
    const bool ready = output.base != 0 && twin_output.base != 0 && scratch.base != 0;
    ir_lower_test_expect(tally, ready, "aggregate arenas", 0, 0);
    IrFunction function =
    {
        0
    };
    if (ready && ir_lower_test_lower(tally, &source, IR_I64, 2, &output, &twin_output, &scratch, &function, 0, 0))
    {
        const IrPassed* own = function.parameter_passed != 0 ? &function.passed[function.parameter_passed - 1] : 0;
        ir_lower_test_expect(tally, own != 0 && function.parameter_types[0] == IR_PTR && function.parameter_types[1] == IR_I64 && own[0].size == 16 &&
                                        memcmp(&own[0].location, &pair, sizeof(pair)) == 0 && own[1].size == 0 && memcmp(&own[1].location, &parameters[1], sizeof(pair)) == 0 &&
                                        function.result_type == IR_PTR && function.result_size == 16 && memcmp(&function.result_abi, &returned, sizeof(returned)) == 0,
                             "the function's own records are the stream's parameters and result", 0, 0);
        uint32_t call = BUSTER_IR_NONE;
        uint32_t copies = 0;
        for (uint32_t i = 0; i < function.instruction_count; i += 1)
        {
            call = function.op[i] == IR_CALL ? i : call;
            copies += function.op[i] == IR_STORE && i < function.block_offsets[1];
        }
        const IrCall* descriptor = call != BUSTER_IR_NONE ? &function.calls[function.b[call]] : 0;
        const IrAbiCall* abi = descriptor != 0 && descriptor->abi != 0 ? &function.abi_calls[descriptor->abi - 1] : 0;
        ir_lower_test_expect(tally, abi != 0 && memcmp(&abi->call, &calls[0], sizeof(AbiCall)) == 0 && abi->result_size == 16 && function.passed[abi->passed].size == 16 &&
                                        memcmp(&function.passed[abi->passed].location, &pair, sizeof(pair)) == 0 && function.type[call] == IR_VOID && function.c[call] == 2 &&
                                        function.op[function.c[call]] == IR_STACK_ADDR && function.a[function.c[call]] == 2 &&
                                        function.type[function.extra[descriptor->argument_first]] == IR_PTR,
                             "the call's records, its aggregate argument a ptr and its result object's slot its destination", 0, 0);
        // Two pieces of s's bytes, and x's value.
        ir_lower_test_expect(tally, copies == 3 && function.slots[0].size == 16 && function.slots[2].size == 16, "the parameter's 16 bytes copied in two pieces into its object", 0, 0);
    }
    for (uint32_t vector = 0; ready && vector < 2; vector += 1)
    {
        const uint64_t output_mark = output.length;
        const uint64_t scratch_mark = scratch.length;
        IrFunction result = preserved;
        IrCheck check = ir_lower_typed_path(0, IR_I64, 1, &output, &scratch, &result, vector != 0);
        ir_lower_test_expect(tally, check.error == IR_ERROR_STRUCTURE && output.length == output_mark && scratch.length == scratch_mark &&
                                        result.instruction_count == preserved.instruction_count,
                             "null input refused as a structural error", 0, vector);
        // A result object that isn't an object, an argument past its call's stack area, a result in registers without
        // their result, and aggregates without the calls' totals.
        for (uint32_t breach = 0; breach < 4; breach += 1)
        {
            call_result_objects[0] = breach == 0 ? 2 : 3;
            argument_locations[0] = breach == 1 ? (AbiLocation){ .kind = BUSTER_ABI_LOCATION_STACK, .registers = { BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE } } : pair;
            source.result.kind = breach == 2 ? BUSTER_ABI_RESULT_MEMORY : BUSTER_ABI_RESULT_REGISTERS;
            source.calls = breach == 3 ? 0 : calls;
            result = preserved;
            check = ir_lower_typed_path(&source, IR_I64, 2, &output, &scratch, &result, vector != 0);
            ir_lower_test_expect(tally, check.error == IR_ERROR_STRUCTURE && output.length == output_mark && scratch.length == scratch_mark &&
                                            result.instruction_count == preserved.instruction_count && result.block_count == preserved.block_count,
                                 "a break of the aggregates' rules refused without allocation", breach, vector);
        }
        call_result_objects[0] = 3;
        argument_locations[0] = pair;
        source.result.kind = BUSTER_ABI_RESULT_REGISTERS;
        source.calls = calls;
    }
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
}

// #136's frame objects and COPY. Two objects, of 23 bytes (aligned to 8) and 24 (aligned to 16), the first's bytes
// stored in pieces of every width and copied into the second, whose last byte the copy mustn't reach, then the second
// copied over itself (the same range); the result folds the second's three words and its address's remainder by 16.
// The executor faults on any piece past the first object's 23 bytes. The slots must keep the objects' sizes and
// alignments, address-taken.
typedef struct IrLowerTestStream IrLowerTestStream;
struct IrLowerTestStream
{
    uint8_t kinds[64];
    uint8_t classes[64];
    uint64_t values[64];
    uint32_t count;
};

// Narrow integers at a call's boundary (#141): f(signed char a, unsigned short b) returns g(a, b, (unsigned char)200,
// (short)-3). Each narrow parameter arrives as an i32, truncated to its type before its slot's store, and each narrow
// argument crosses its call as an i32, sign- or zero-extended by its class, with i32 call types, by both lowering paths.
// ir_typed_ignored_arguments' twins (#141): random call argument locations of every kind, none, one or several of them
// IGNORE, at every count from 0 to 40 and so at each of the vector path's 8 lanes and its partial tails, must agree.
static void ir_lower_test_ignored_scan(IrLowerTestTally* tally)
{
    AbiLocation locations[40];
    uint64_t random = 141;
    uint32_t agreed = 0;
    for (uint32_t trial = 0; trial < 2000; trial += 1)
    {
        const uint32_t count = trial % 41;
        for (uint32_t a = 0; a < count; a += 1)
        {
            random = random * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
            const uint32_t bits = (uint32_t)(random >> 33);
            locations[a] = (AbiLocation){
                .stack_offset = bits & 0xff,
                .registers = { (uint8_t)(bits >> 8), (uint8_t)(bits >> 16) },
                .kind = (uint8_t)(1 + (bits >> 24) % 3),
                .flags = (uint8_t)(bits >> 28),
            };
        }
        // None, one at a random place, or one in each eighth of the trials' further ones.
        random = random * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        const uint32_t place = count != 0 ? (uint32_t)(random >> 33) % count : 0;
        const uint32_t shape = trial % 3;
        for (uint32_t a = 0; count != 0 && shape != 0 && a < count; a += 1)
        {
            locations[a].kind = (shape == 1 && a == place) || (shape == 2 && a % 8 == place % 8) ? BUSTER_ABI_LOCATION_IGNORE : locations[a].kind;
        }
        // A static chain's location (#148), in r10, which the random registers sometimes give, is found as an ignored one is.
        bool chained = false;
        for (uint32_t a = 0; a < count; a += 1)
        {
            chained |= locations[a].kind == BUSTER_ABI_LOCATION_REGISTERS && locations[a].registers[0] == BUSTER_ABI_R10;
        }
        const Tier0Function function = { .argument_locations = count != 0 ? locations : NULL, .argument_count = count };
        const bool expected = (count != 0 && shape != 0) || chained;
        agreed += ir_typed_ignored_arguments(&function, true) == expected && ir_typed_ignored_arguments(&function, false) == expected;
    }
    ir_lower_test_expect(tally, agreed == 2000, "ir_typed_ignored_arguments' paths find an ignored argument or a static chain where there is one", 141, 0);
}

static void ir_lower_test_narrow(IrLowerTestTally* tally)
{
    Arena output = arena_reserve(1ull << 20);
    Arena twin_output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    static const uint8_t kinds[] =
    {
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CALL,
        BUSTER_TYPED_RETURN
    };
    static const uint8_t classes[] =
    {
        BUSTER_TYPED_CLASS_I8,
        BUSTER_TYPED_CLASS_U16,
        BUSTER_TYPED_CLASS_U8,
        BUSTER_TYPED_CLASS_I16,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32
    };
    static const uint64_t values[] =
    {
        0,
        1,
        200,
        0xfffd,
        0,
        0
    };
    static const uint8_t local_classes[2] =
    {
        BUSTER_TYPED_CLASS_I8,
        BUSTER_TYPED_CLASS_U16
    };
    static const uint8_t local_flags[2] =
    {
        0,
        0
    };
    const AbiLocation parameters[2] =
    {
        { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { X86_REGISTER_RDI, BUSTER_ABI_REGISTER_NONE }, .flags = BUSTER_ABI_FLAG_SIGN },
        { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { X86_REGISTER_RSI, BUSTER_ABI_REGISTER_NONE }, .flags = BUSTER_ABI_FLAG_ZERO },
    };
    const AbiReturn returned =
    {
        .kind = BUSTER_ABI_RESULT_REGISTERS,
        .part_bytes = 8,
        .registers =
        {
            BUSTER_ABI_RAX,
            BUSTER_ABI_REGISTER_NONE,
            BUSTER_ABI_REGISTER_NONE,
            BUSTER_ABI_REGISTER_NONE
        },
    };
    AbiCall calls[1] =
    {
        {
            .stack_alignment_log2 = 4,
            .integer_registers = 4,
            .result = returned,
        },
    };
    static const SymbolId call_targets[1] =
    {
        1
    };
    static const uint32_t call_first[1] =
    {
        0
    };
    static const uint32_t call_argument_counts[1] =
    {
        4
    };
    static const uint8_t call_variadic[1] =
    {
        0
    };
    static const uint8_t argument_classes[4] =
    {
        BUSTER_TYPED_CLASS_I8,
        BUSTER_TYPED_CLASS_U16,
        BUSTER_TYPED_CLASS_U8,
        BUSTER_TYPED_CLASS_I16
    };
    AbiLocation argument_locations[4] =
    {
        { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { X86_REGISTER_RDI, BUSTER_ABI_REGISTER_NONE }, .flags = BUSTER_ABI_FLAG_SIGN },
        { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { X86_REGISTER_RSI, BUSTER_ABI_REGISTER_NONE }, .flags = BUSTER_ABI_FLAG_ZERO },
        { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { X86_REGISTER_RDX, BUSTER_ABI_REGISTER_NONE }, .flags = BUSTER_ABI_FLAG_ZERO },
        { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { X86_REGISTER_RCX, BUSTER_ABI_REGISTER_NONE }, .flags = BUSTER_ABI_FLAG_SIGN },
    };
    Tier0Function source =
    {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 2,
        .parameter_count = 2,
        .parameters = parameters,
        .result = returned,
        .call_targets = call_targets,
        .call_first = call_first,
        .call_argument_counts = call_argument_counts,
        .call_variadic = call_variadic,
        .calls = calls,
        .argument_locations = argument_locations,
        .argument_classes = argument_classes,
        .call_count = 1,
        .argument_count = 4,
    };
    const bool ready = output.base != 0 && twin_output.base != 0 && scratch.base != 0;
    ir_lower_test_expect(tally, ready, "narrow boundary arenas", 0, 0);
    IrFunction function =
    {
        0
    };
    if (ready && ir_lower_test_lower(tally, &source, IR_I32, 2, &output, &twin_output, &scratch, &function, 0, 0))
    {
        // Each parameter's i32, the truncation its store reads, and that store.
        uint32_t truncated = 0;
        uint32_t truncations[2] = { BUSTER_IR_NONE, BUSTER_IR_NONE };
        for (uint32_t i = 0; i < function.instruction_count; i += 1)
        {
            const uint32_t a = function.a[i];
            const bool parameter = function.op[i] == IR_TRUNC && a < function.instruction_count && function.op[a] == IR_PARAM && function.type[a] == IR_I32;
            if (parameter && function.a[a] == 0 && function.type[i] == IR_I8)
            {
                truncated |= 1;
                truncations[0] = i;
            }
            if (parameter && function.a[a] == 1 && function.type[i] == IR_I16)
            {
                truncated |= 2;
                truncations[1] = i;
            }
        }
        uint32_t stored = 0;
        for (uint32_t i = 0; i < function.instruction_count; i += 1)
        {
            stored |= function.op[i] == IR_STORE && function.a[i] == 0 && function.b[i] == truncations[0] ? 1u : 0;
            stored |= function.op[i] == IR_STORE && function.a[i] == 1 && function.b[i] == truncations[1] ? 2u : 0;
        }
        ir_lower_test_expect(tally, function.parameter_count == 2 && function.parameter_types[0] == IR_I32 && function.parameter_types[1] == IR_I32 && truncated == 3 && stored == 3,
                             "a narrow parameter arrives as an i32 its callee truncates into its slot", 0, 0);
        uint32_t call = BUSTER_IR_NONE;
        for (uint32_t i = 0; i < function.instruction_count; i += 1)
        {
            call = function.op[i] == IR_CALL ? i : call;
        }
        bool extended = call != BUSTER_IR_NONE && function.calls[function.b[call]].argument_count == 4;
        static const uint8_t extensions[4] =
        {
            IR_SEXT,
            IR_ZEXT,
            IR_ZEXT,
            IR_SEXT
        };
        static const uint8_t narrow_types[4] =
        {
            IR_I8,
            IR_I16,
            IR_I8,
            IR_I16
        };
        for (uint32_t argument = 0; extended && argument < 4; argument += 1)
        {
            const IrCall descriptor = function.calls[function.b[call]];
            const uint32_t value = function.extra[descriptor.argument_first + argument];
            extended = function.call_types[descriptor.type_first + argument] == IR_I32 && value < function.instruction_count && function.op[value] == extensions[argument] &&
                       function.type[value] == IR_I32 && function.type[function.a[value]] == narrow_types[argument];
        }
        ir_lower_test_expect(tally, extended, "a narrow argument crosses its call as an i32 extended by its class", 0, 0);
    }
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
}

static void ir_lower_test_entry(IrLowerTestStream* stream, uint8_t kind, uint8_t class, uint64_t value)
{
    if (stream->count < 64)
    {
        stream->kinds[stream->count] = kind;
        stream->classes[stream->count] = class;
        stream->values[stream->count] = value;
    }
    stream->count += 1;
}

static void ir_lower_test_sources(IrLowerTestTally* tally)
{
    IrLowerTestStream stream = { 0 };
    const uint8_t local_classes[] = {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I8,
        BUSTER_TYPED_CLASS_F64,
        BUSTER_TYPED_CLASS_I32,
    };
    const uint8_t local_flags[] = { 0, 0, 0, 0, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, 0, 0, 0, 0 };
    const AbiLocation parameters[] = {
        { .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        { .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
    };
    const uint32_t assigned[] = { 2, 3, 6, 7, 4, 1, 3 };
    const uint64_t literals[] = { 29, 11, 0xfb, UINT64_C(0x400c000000000000), 3, 17, 19 };
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(assigned); k += 1)
    {
        const uint32_t local = assigned[k];
        if (k + 1 == BUSTER_ARRAY_LENGTH(assigned))
        {
            ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
            ir_lower_test_entry(&stream, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
        }
        else
        {
            ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, local_classes[local], literals[k]);
        }
        ir_lower_test_entry(&stream, BUSTER_TYPED_SET_LOCAL, local_classes[local], local);
    }
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
    const uint32_t branch = stream.count;
    ir_lower_test_entry(&stream, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_U64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 23);
    ir_lower_test_entry(&stream, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, 5);
    for (uint32_t read = 0; read < 2; read += 1)
    {
        ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, 5);
        ir_lower_test_entry(&stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0);
    }
    const uint32_t loop = stream.count;
    stream.values[branch] = loop;
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 5);
    ir_lower_test_entry(&stream, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I32, 8);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_BRANCH_TRUE, BUSTER_TYPED_CLASS_U64, loop);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
    const Tier0Function source = {
        .kinds = stream.kinds,
        .classes = stream.classes,
        .values = stream.values,
        .count = stream.count,
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = BUSTER_ARRAY_LENGTH(local_classes),
        .parameters = parameters,
        .parameter_count = BUSTER_ARRAY_LENGTH(parameters),
        .result = { .kind = BUSTER_ABI_RESULT_REGISTERS, .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE } },
    };
    Arena output = arena_reserve(1ull << 20);
    Arena plain_output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        IrFunction plain = { 0 };
        IrFunction described = { 0 };
        const IrCheck plain_check = ir_lower_typed_target_pieces(&source, IR_I64, 1, 0, &plain_output, &scratch, &plain, scalar != 0);
        const uint64_t poisoned_bytes = 1ull << 18;
        const uint64_t reused = scratch.length;
        uint8_t* poison = arena_push(&scratch, poisoned_bytes, 1);
        if (poison != NULL)
        {
            memset(poison, 0xa5, poisoned_bytes);
        }
        arena_pop_to(&scratch, reused);
        const IrCheck debug_check = ir_lower_typed_target_pieces_sources(&source, IR_I64, 1, 0, &output, &scratch, &described, scalar != 0);
        bool valid = ir_lower_test_expect(tally, poison != NULL && plain_check.error == IR_OK && debug_check.error == IR_OK && described.debug != NULL,
                                          "source-aware lowering accepts the scalar admission fixture with poisoned reused scratch", 0, scalar);
        if (valid)
        {
            valid = ir_lower_test_equal(&plain, &described) && plain.debug == NULL && described.debug->scalar_count == 4;
            const IrDebugScalar expected[] = {
                { .local = 0, .entry = BUSTER_IR_NONE, .kind = IR_DEBUG_PARAMETER, .bytes = 8 },
                { .value = 29, .local = 2, .entry = 1, .kind = IR_DEBUG_CONSTANT, .bytes = 4 },
                { .value = 0xfb, .local = 6, .entry = 5, .kind = IR_DEBUG_CONSTANT, .bytes = 1 },
                { .value = UINT64_C(0x400c000000000000), .local = 7, .entry = 7, .kind = IR_DEBUG_CONSTANT, .bytes = 8 },
            };
            for (uint32_t k = 0; valid && k < BUSTER_ARRAY_LENGTH(expected); k += 1)
            {
                const IrDebugScalar actual = described.debug->scalars[k];
                valid = actual.local == expected[k].local && actual.entry == expected[k].entry && actual.kind == expected[k].kind &&
                        actual.bytes == expected[k].bytes && actual.value == expected[k].value;
            }
            for (uint32_t local = 0; valid && local < source.local_count; local += 1)
            {
                valid = described.debug->locals[local] == local;
            }
            ir_lower_test_expect(tally, valid, "debug admission changed IR, lost exact literal bits, or admitted a reassigned/addressed/conditional local", 0, scalar);
            const IrDebugInfo* debug = described.debug;
            bool bindings = debug->binding_count == 11 && debug->first_block_end == branch + 1 &&
                            debug->bindings[0].local == 0 && debug->bindings[0].entry == BUSTER_IR_NONE && debug->bindings[0].kind == IR_DEBUG_BIND_VALUE &&
                            described.op[debug->bindings[0].value] == IR_PARAM && described.a[debug->bindings[0].value] == 0 &&
                            debug->bindings[3].local == 3 && debug->bindings[3].entry == 3 && debug->bindings[3].kind == IR_DEBUG_BIND_CONSTANT && debug->bindings[3].literal == 11 &&
                            debug->bindings[8].local == 3 && debug->bindings[8].entry == 14 && debug->bindings[8].kind == IR_DEBUG_BIND_VALUE &&
                            described.op[debug->bindings[8].value] == IR_TRUNC;
            for (uint32_t local = 0; bindings && local < source.local_count; local += 1)
            {
                const uint8_t admitted = local == 4 ? 0 : local == 5 || local == 8 ? BUSTER_IR_DEBUG_BIND_REGIONS : BUSTER_IR_DEBUG_BIND_ENTRY;
                bindings = debug->binding_flags[local] == admitted;
            }
            ir_lower_test_expect(tally, bindings, "assignment events lost an initial parameter, a later unknown write, or the branch/loop admission distinction", 0, scalar);
            const bool regions = debug->region_count == 2 && debug->regions[0].local == 5 && debug->regions[0].block == 2 &&
                                 debug->regions[0].begin == branch + 3 && debug->regions[0].end == loop &&
                                 debug->regions[0].kind == IR_DEBUG_BIND_CONSTANT && debug->regions[0].literal == 23 &&
                                 debug->regions[1].local == 8 && debug->regions[1].block == 3 && debug->regions[1].begin == loop + 2 &&
                                 debug->regions[1].end == loop + 4 && debug->regions[1].kind == IR_DEBUG_BIND_CONSTANT && debug->regions[1].literal == 5;
            ir_lower_test_expect(tally, regions, "branch/loop assignments have disjoint block windows and repeated real reads share one witness", 0, scalar);
            const uint64_t required = plain_output.length;
            arena_pop_to(&output, 0);
            Arena bounded = output;
            bounded.capacity = required;
            IrFunction sentinel = { .instruction_count = 123, .block_count = 17 };
            const IrFunction before = sentinel;
            const uint64_t scratch_mark = scratch.length;
            const IrCheck refused = ir_lower_typed_target_pieces_sources(&source, IR_I64, 1, 0, &bounded, &scratch, &sentinel, scalar != 0);
            ir_lower_test_expect(tally, refused.error == IR_ERROR_MEMORY && bounded.length == 0 && scratch.length == scratch_mark &&
                                       memcmp(&before, &sentinel, sizeof(sentinel)) == 0,
                                 "source sidecar exhaustion did not roll back the whole lowering", 0, scalar);
        }
        arena_pop_to(&output, 0);
        arena_pop_to(&plain_output, 0);
        // The first typed block can itself be a loop header. Its assignments are not one-time definitions merely
        // because their entry numbers precede the first control transfer; an unchanged incoming parameter still is.
        stream.values[loop + 3] = 0;
        IrFunction reentered = { 0 };
        const IrCheck reentered_check = ir_lower_typed_target_pieces_sources(&source, IR_I64, 1, 0, &output, &scratch, &reentered, scalar != 0);
        bool excluded = reentered_check.error == IR_OK && reentered.debug != NULL && reentered.debug->binding_count == 11;
        for (uint32_t local = 0; excluded && local < source.local_count; local += 1)
        {
            excluded = reentered.debug->binding_flags[local] == (local == 0 ? BUSTER_IR_DEBUG_BIND_ENTRY : local == 4 ? 0 : BUSTER_IR_DEBUG_BIND_REGIONS);
        }
        ir_lower_test_expect(tally, excluded, "a backedge into the first source block restricts written locals to witnessed regions", 0, scalar);
        stream.values[loop + 3] = loop;
        arena_pop_to(&output, 0);
    }
    arena_release(&scratch);
    arena_release(&plain_output);
    arena_release(&output);
}

static void ir_lower_test_region_lifetimes(IrLowerTestTally* tally)
{
    IrLowerTestStream stream = { 0 };
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 11);
    ir_lower_test_entry(&stream, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I64, 18);
    for (uint32_t read = 0; read < 2; read += 1)
    {
        ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, 1);
        ir_lower_test_entry(&stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
    }
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 22);
    ir_lower_test_entry(&stream, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 33);
    ir_lower_test_entry(&stream, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_I64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_I64, 0);
    const uint8_t classes[] = { BUSTER_TYPED_CLASS_I64, BUSTER_TYPED_CLASS_I64 };
    const uint8_t flags[] = { 0, 0 };
    const AbiLocation parameter = { .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS };
    const uint32_t lifetime_entries[] = { 0, 12, 14, 18 };
    const uint32_t lifetime_locals[] = { 1, 1 | BUSTER_TIER0_LIFETIME_END, 1, 1 | BUSTER_TIER0_LIFETIME_END };
    const Tier0Function source = {
        .kinds = stream.kinds, .classes = stream.classes, .values = stream.values, .count = stream.count,
        .local_classes = classes, .local_flags = flags, .local_count = BUSTER_ARRAY_LENGTH(classes),
        .parameters = &parameter, .parameter_count = 1,
        .result = { .kind = BUSTER_ABI_RESULT_REGISTERS, .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE } },
        .lifetime_entries = lifetime_entries, .lifetime_locals = lifetime_locals, .lifetime_count = BUSTER_ARRAY_LENGTH(lifetime_entries),
    };
    Arena output = arena_reserve(1ull << 20);
    Arena plain_output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        IrFunction function = { 0 };
        IrFunction plain = { 0 };
        const IrCheck described = ir_lower_typed_target_pieces_sources(&source, IR_I64, 1, 0, &output, &scratch, &function, scalar != 0);
        const IrCheck control = ir_lower_typed_target_pieces(&source, IR_I64, 1, 0, &plain_output, &scratch, &plain, scalar != 0);
        bool exact = described.error == IR_OK && control.error == IR_OK && ir_lower_test_equal(&function, &plain) && function.debug != NULL &&
                     function.debug->region_count == 4 && function.debug->binding_flags[0] == BUSTER_IR_DEBUG_BIND_ENTRY &&
                     function.debug->binding_flags[1] == BUSTER_IR_DEBUG_BIND_REGIONS;
        const uint32_t begins[] = { 2, 4, 10, 16 };
        const uint32_t ends[] = { 4, 9, 12, 18 };
        const uint64_t literals[] = { 11, 0, 22, 33 };
        for (uint32_t k = 0; exact && k < BUSTER_ARRAY_LENGTH(begins); k += 1)
        {
            const IrDebugRegion region = function.debug->regions[k];
            exact = region.local == 1 && region.block == (k == 0 ? 1u : 2u) && region.begin == begins[k] && region.end == ends[k] &&
                    region.bytes == 8 && region.type == IR_I64 && region.next == (k == 1 ? 2u : k == 2 ? 3u : BUSTER_IR_NONE);
            exact = exact && (k == 1 ? region.kind == IR_DEBUG_BIND_VALUE && region.value < function.instruction_count &&
                             function.op[region.value] == IR_LOAD && function.debug->origins[region.value] == 4 :
                             region.kind == IR_DEBUG_BIND_CONSTANT && region.literal == literals[k]);
        }
        ir_lower_test_expect(tally, exact, "source regions preserve read witnesses, exact next-assignment kills and lifetime gaps without changing IR", 0, scalar);
        arena_pop_to(&plain_output, 0);
        arena_pop_to(&output, 0);
    }
    arena_release(&scratch);
    arena_release(&plain_output);
    arena_release(&output);
}

static void ir_lower_test_objects(IrLowerTestTally* tally)
{
    IrLowerTestStream stream =
    {
        0
    };
    // The first object's 23 bytes, by pieces of every width.
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 0x1122334455667788ull);
    ir_lower_test_entry(&stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 8);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 0x99aabbccddeeff00ull);
    ir_lower_test_entry(&stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 16);
    ir_lower_test_entry(&stream, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 0x01020304);
    ir_lower_test_entry(&stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I32, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 20);
    ir_lower_test_entry(&stream, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I16, 0x0506);
    ir_lower_test_entry(&stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I16, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 22);
    ir_lower_test_entry(&stream, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I8, 0x07);
    ir_lower_test_entry(&stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I8, 0);
    // Past the copy's end, the second object's last byte.
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 23);
    ir_lower_test_entry(&stream, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I8, 0x5a);
    ir_lower_test_entry(&stream, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I8, 0);
    // The copy, then the second object over itself.
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, 23);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, 24);
    // The second object's three words and its address's remainder by 16.
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 8);
    ir_lower_test_entry(&stream, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 16);
    ir_lower_test_entry(&stream, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 15);
    ir_lower_test_entry(&stream, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_I64, 0);
    ir_lower_test_entry(&stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_I64, 0);
    const uint8_t local_classes[2] =
    {
        BUSTER_TYPED_CLASS_VOID,
        BUSTER_TYPED_CLASS_VOID
    };
    const uint8_t local_flags[2] =
    {
        BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
        BUSTER_TYPED_LOCAL_ADDRESS_TAKEN
    };
    const uint32_t local_sizes[2] =
    {
        23,
        24
    };
    const uint8_t local_alignments[2] =
    {
        3,
        4
    };
    const Tier0Function source =
    {
        .kinds = stream.kinds,
        .classes = stream.classes,
        .values = stream.values,
        .count = stream.count,
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 2,
        .local_sizes = local_sizes,
        .local_alignments = local_alignments,
    };
    const uint64_t expected = 0x1122334455667788ull ^ 0x99aabbccddeeff00ull ^ 0x5a07050601020304ull;
    const bool valid = ir_lower_test_expect(tally, stream.count <= 64 && tier0_validate(&source).error == TIER0_OK, "the object fixture is a valid typed stream", 0, 0);
    const uint64_t arguments[1] =
    {
        0
    };
    if (valid)
    {
        ir_lower_test_fixture(tally, "objects' bytes copied in pieces, the destination's tail kept", &source, IR_I64, arguments, expected);
    }
    Arena output = arena_reserve(1ull << 20);
    Arena twin_output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    IrFunction function =
    {
        0
    };
    if (valid && ir_lower_test_lower(tally, &source, IR_I64, 1, &output, &twin_output, &scratch, &function, 0, 0))
    {
        bool slots = function.slot_count >= 2;
        for (uint32_t l = 0; slots && l < 2; l += 1)
        {
            const IrSlot slot = function.slots[l];
            slots = slot.size == local_sizes[l] && slot.alignment == 1u << local_alignments[l] && slot.type == IR_VOID && slot.flags == BUSTER_IR_SLOT_ADDRESS_TAKEN;
        }
        ir_lower_test_expect(tally, slots, "objects' slots keep their sizes and alignments, address-taken", 0, 0);
    }
    // Objects' metadata tier0_validate refuses, before anything reads it: sizes without alignments, and an object with
    // a class. Both paths must refuse them as structure errors without allocating or touching the caller's result.
    const uint8_t classed[2] =
    {
        BUSTER_TYPED_CLASS_I64,
        BUSTER_TYPED_CLASS_VOID
    };
    for (uint32_t broken = 0; valid && broken < 2; broken += 1)
    {
        Tier0Function malformed = source;
        if (broken == 0)
        {
            malformed.local_alignments = 0;
        }
        else
        {
            malformed.local_classes = classed;
        }
        for (uint32_t vector = 0; vector < 2; vector += 1)
        {
            const uint64_t output_mark = output.length;
            const uint64_t scratch_mark = scratch.length;
            IrFunction result =
            {
                .instruction_count = 123,
            };
            const IrCheck check = ir_lower_typed_path(&malformed, IR_I64, 1, &output, &scratch, &result, vector != 0);
            ir_lower_test_expect(tally, check.error == IR_ERROR_STRUCTURE && output.length == output_mark && scratch.length == scratch_mark && result.instruction_count == 123,
                                 "malformed objects refused without allocation", broken, vector);
        }
    }
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
}

// COPY of every size from 1 to BUSTER_TIER0_MAX_COPY_BYTES between two globals, and from one global onto itself, by
// both paths: the destination's first `size` bytes must be the source's, its other bytes and the source unchanged, with
// one load and one store per piece of the widest integer that fits what remains.
#define BUSTER_IR_LOWER_TEST_COPY_SPAN (BUSTER_TIER0_MAX_COPY_BYTES + 8)

static void ir_lower_test_copies(IrLowerTestTally* tally)
{
    const uint32_t span = BUSTER_IR_LOWER_TEST_COPY_SPAN;
    const uint8_t kinds[] =
    {
        BUSTER_TYPED_GLOBAL_ADDRESS,
        BUSTER_TYPED_GLOBAL_ADDRESS,
        BUSTER_TYPED_COPY,
        BUSTER_TYPED_RETURN
    };
    const uint8_t classes[] =
    {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_VOID,
        BUSTER_TYPED_CLASS_VOID
    };
    Arena output = arena_reserve(1ull << 20);
    Arena twin_output = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    Arena execution = arena_reserve(1ull << 20);
    static uint8_t globals[2 * BUSTER_IR_LOWER_TEST_COPY_SPAN];
    static uint8_t initial[2 * BUSTER_IR_LOWER_TEST_COPY_SPAN];
    static uint8_t expected[2 * BUSTER_IR_LOWER_TEST_COPY_SPAN];
    const uint64_t arguments[1] =
    {
        0
    };
    Tier0TestData data =
    {
        0
    };
    data.offsets[0] = 0;
    data.offsets[1] = span;
    data.size = 2 * span;
    uint64_t state = 0x136c09ull;
    for (uint32_t b = 0; b < 2 * span; b += 1)
    {
        initial[b] = (uint8_t)tier0_generate_random(&state);
    }
    uint64_t failed_sizes = 0;
    for (uint32_t same = 0; same < 2; same += 1)
    {
        for (uint32_t size = 1; size <= BUSTER_TIER0_MAX_COPY_BYTES; size += 1)
        {
            // Global 1 (the destination) and global 2 (the source), or global 2 onto itself.
            const uint64_t values[] =
            {
                same != 0 ? 2 : 1,
                2,
                size,
                0
            };
            const Tier0Function source =
            {
                .kinds = kinds,
                .classes = classes,
                .values = values,
                .count = 4,
            };
            IrFunction function =
            {
                0
            };
            bool ok = ir_lower_test_lower(tally, &source, IR_VOID, 3, &output, &twin_output, &scratch, &function, size, same);
            if (ok)
            {
                uint32_t loads = 0;
                uint32_t stores = 0;
                for (uint32_t i = 0; i < function.instruction_count; i += 1)
                {
                    loads += function.op[i] == IR_LOAD;
                    stores += function.op[i] == IR_STORE;
                }
                const uint32_t pieces = size / 8 + (uint32_t)__builtin_popcount(size % 8);
                ok = ir_lower_test_expect(tally, loads == pieces && stores == pieces, "a copy is one load and store per widest piece", size, same);
                // The globals as the copy must leave them.
                memcpy(expected, initial, sizeof(expected));
                memmove(expected + (same != 0 ? span : 0), initial + span, size);
                memcpy(globals, initial, sizeof(globals));
                bool fault = false;
                ir_lower_test_interpret(&function, 1, 0, 2, &data, globals, arguments, 0, &execution, &fault);
                ok = ir_lower_test_expect(tally, !fault && memcmp(globals, expected, sizeof(expected)) == 0, "a copy's bytes, and nothing past them", size, same) && ok;
            }
            failed_sizes += !ok;
            arena_pop_to(&output, 0);
            arena_pop_to(&twin_output, 0);
        }
    }
    printf("IR lowering copies: %u sizes twice, %" PRIu64 " failed\n", BUSTER_TIER0_MAX_COPY_BYTES, failed_sizes);
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
    arena_release(&execution);
}

// #133's labels and indirect jumps: GNU C's labels as values and the switches' jump tables. Each label is a block, and
// every JUMP_INDIRECT a branch to the function's dispatch block, whose br_indirect's successors are the labels: a stream
// whose jump goes to an address the labels don't have still lowers (its jump has no meaning), a computed goto through a
// ?: of two labels' addresses executes as it should, by both paths and promoted, and so do two computed gotos, which
// share the one dispatch block, whose address promotion makes a phi of the two.
static void ir_lower_test_indirect_jump(IrLowerTestTally* tally)
{
    static const uint8_t kinds[3] =
    {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_JUMP_INDIRECT,
        BUSTER_TYPED_RETURN
    };
    static const uint8_t classes[3] =
    {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_VOID
    };
    static const uint64_t values[3] =
    {
        7,
        0,
        0
    };
    // The label the indirect jump goes to.
    static const uint32_t labels[1] =
    {
        2
    };
    // void* t = p ? &&a : &&b; goto *t; a: return 10; b: return 20; with label a's symbol 2 and b's 3, as the executor
    // numbers them for its one function.
    static const uint8_t goto_kinds[] =
    {
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_GLOBAL_ADDRESS,
        BUSTER_TYPED_JUMP_VALUE,
        BUSTER_TYPED_GLOBAL_ADDRESS,
        BUSTER_TYPED_JUMP_INDIRECT,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_RETURN,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_RETURN
    };
    static const uint8_t goto_classes[] =
    {
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32
    };
    static const uint64_t goto_values[] =
    {
        0,
        4,
        2,
        5,
        3,
        0,
        10,
        0,
        20,
        0
    };
    static const uint32_t goto_labels[2] =
    {
        6,
        8
    };
    // if (p) goto *&&a; else goto *&&b; a: return 10; b: return 20; the same symbols.
    static const uint8_t twice_kinds[] =
    {
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_GLOBAL_ADDRESS,
        BUSTER_TYPED_JUMP_INDIRECT,
        BUSTER_TYPED_GLOBAL_ADDRESS,
        BUSTER_TYPED_JUMP_INDIRECT,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_RETURN,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_RETURN
    };
    static const uint8_t twice_classes[] =
    {
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32
    };
    static const uint64_t twice_values[] =
    {
        0,
        4,
        2,
        0,
        3,
        0,
        10,
        0,
        20,
        0
    };
    static const uint8_t local_classes[1] =
    {
        BUSTER_TYPED_CLASS_I32
    };
    static const uint8_t local_flags[1] =
    {
        0
    };
    const AbiLocation parameter =
    {
        .kind = BUSTER_ABI_LOCATION_REGISTERS,
        .registers =
        {
            X86_REGISTER_RDI
        },
    };
    Arena output = arena_reserve(1ull << 20);
    Arena twin_output = arena_reserve(1ull << 20);
    Arena promoted = arena_reserve(1ull << 20);
    Arena twin_promoted = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    // Both executors' frames, the shared IR interpreter's as the wide programs' check reserves them.
    Arena execution = arena_reserve(1ull << 26);
    const bool ready = output.base != 0 && twin_output.base != 0 && promoted.base != 0 && twin_promoted.base != 0 && scratch.base != 0 && execution.base != 0;
    ir_lower_test_expect(tally, ready, "indirect jump arenas", 0, 0);
    const Tier0Function source =
    {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = 3,
        .label_entries = labels,
        .label_count = 1,
    };
    const Tier0Function computed =
    {
        .kinds = goto_kinds,
        .classes = goto_classes,
        .values = goto_values,
        .count = BUSTER_ARRAY_LENGTH(goto_kinds),
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 1,
        .parameter_count = 1,
        .parameters = &parameter,
        .label_entries = goto_labels,
        .label_count = 2,
    };
    const Tier0Function twice =
    {
        .kinds = twice_kinds,
        .classes = twice_classes,
        .values = twice_values,
        .count = BUSTER_ARRAY_LENGTH(twice_kinds),
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 1,
        .parameter_count = 1,
        .parameters = &parameter,
        .label_entries = goto_labels,
        .label_count = 2,
    };
    const Tier0TestData data =
    {
        0
    };
    uint8_t globals[1] =
    {
        0
    };
    IrFunction function =
    {
        0
    };
    if (ready && ir_lower_test_lower(tally, &source, IR_VOID, 1, &output, &twin_output, &scratch, &function, 0, 0))
    {
        uint32_t jumps = 0;
        for (uint32_t i = 0; i < function.instruction_count; i += 1)
        {
            jumps += function.op[i] == IR_BR_INDIRECT;
        }
        ir_lower_test_expect(tally, jumps == 1 && function.label_count == 1 && function.labels[0] < function.block_count &&
                                        function.block_offsets[function.labels[0]] < function.instruction_count && function.op[function.block_offsets[function.labels[0]]] == IR_RET,
                             "an indirect jump lowers to a br_indirect, its label to the block of its entry", 0, 0);
    }
    arena_pop_to(&output, 0);
    arena_pop_to(&twin_output, 0);
    if (ready && ir_lower_test_lower(tally, &computed, IR_I32, 4, &output, &twin_output, &scratch, &function, 0, 1))
    {
        for (uint64_t p = 0; p < 2; p += 1)
        {
            bool fault = false;
            const uint64_t actual = ir_lower_test_interpret(&function, 1, 0, 0, &data, globals, &p, 1, &execution, &fault);
            ir_lower_test_expect(tally, !fault && actual == (p != 0 ? 10 : 20), "a computed goto goes to the label whose address it takes", 0, (uint32_t)p);
        }
        ir_lower_test_promote(tally, &function, &promoted, &twin_promoted, &scratch, 0, 1);
        IrFunction promoted_function =
        {
            0
        };
        const IrCheck check = ssa_ir_promote(&function, &promoted, &scratch, &promoted_function);
        ir_lower_test_expect(tally, check.error == IR_OK && promoted_function.label_count == 2 && promoted_function.labels[0] != BUSTER_IR_NONE && promoted_function.labels[1] != BUSTER_IR_NONE,
                             "promotion keeps both labels' blocks", 0, 1);
        // The promoted join's address is a phi of the arms' (a depth slot #983 promotes), which this executor has no
        // phis for: the shared IR's interpreter runs it, its labels symbols 2 and 3.
        static const SymbolId computed_label_symbols[2] =
        {
            2,
            3
        };
        const IrInterpreterFunction computed_code = { .symbol = 1, .function = &promoted_function, .labels = computed_label_symbols };
        const IrInterpreterProgram computed_program = { .functions = &computed_code, .function_count = 1 };
        for (uint64_t p = 0; check.error == IR_OK && p < 2; p += 1)
        {
            const InterpreterValue argument = { .low = p };
            const InterpreterResult result = ir_interpret(&computed_program, 1, &argument, 1, 0, &execution);
            ir_lower_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == (p != 0 ? 10 : 20), "the promoted computed goto goes to the same labels", 0, (uint32_t)p);
        }
    }
    arena_pop_to(&output, 0);
    arena_pop_to(&twin_output, 0);
    arena_pop_to(&promoted, 0);
    arena_pop_to(&twin_promoted, 0);
    if (ready && ir_lower_test_lower(tally, &twice, IR_I32, 4, &output, &twin_output, &scratch, &function, 0, 2))
    {
        // The dispatch block is the last: each jump's block branches to it, and its br_indirect goes to the two labels.
        const uint32_t last = function.block_count - 1;
        const uint32_t end = function.block_offsets[function.block_count] - 1;
        uint32_t jumps = 0;
        uint32_t dispatches = 0;
        for (uint32_t i = 0; i < function.instruction_count; i += 1)
        {
            jumps += function.op[i] == IR_BR_INDIRECT;
            dispatches += function.op[i] == IR_BR && function.a[i] == last;
        }
        ir_lower_test_expect(tally, jumps == 1 && dispatches == 2 && function.op[end] == IR_BR_INDIRECT && ir_successor(&function, end, 0) == function.labels[0] &&
                                        ir_successor(&function, end, 1) == function.labels[1] && function.labels[0] != function.labels[1],
                             "two indirect jumps branch to the one dispatch block, whose br_indirect goes to the labels", 0, 2);
        for (uint64_t p = 0; p < 2; p += 1)
        {
            bool fault = false;
            const uint64_t actual = ir_lower_test_interpret(&function, 1, 0, 0, &data, globals, &p, 1, &execution, &fault);
            ir_lower_test_expect(tally, !fault && actual == (p != 0 ? 10 : 20), "two computed gotos go to the labels whose addresses they take", 0, (uint32_t)p);
        }
        ir_lower_test_promote(tally, &function, &promoted, &twin_promoted, &scratch, 0, 2);
        IrFunction promoted_function =
        {
            0
        };
        const IrCheck check = ssa_ir_promote(&function, &promoted, &scratch, &promoted_function);
        // Promoted, the dispatch block's address is a phi of the two jumps' (the block that ends in the br_indirect).
        uint32_t phi = BUSTER_IR_NONE;
        for (uint32_t b = 0; check.error == IR_OK && b < promoted_function.block_count; b += 1)
        {
            const uint32_t terminator = promoted_function.block_offsets[b + 1] - 1;
            phi = promoted_function.op[terminator] == IR_BR_INDIRECT ? promoted_function.block_offsets[b] : phi;
        }
        ir_lower_test_expect(tally, check.error == IR_OK && phi != BUSTER_IR_NONE && promoted_function.op[phi] == IR_PHI && promoted_function.b[phi] == 2,
                             "promotion makes the dispatch block's address a phi of the two jumps'", 0, 2);
        // This executor has no phis: the shared IR's interpreter runs the promoted function, its labels symbols 2 and 3.
        static const SymbolId label_symbols[2] =
        {
            2,
            3
        };
        const IrInterpreterFunction code = { .symbol = 1, .function = &promoted_function, .labels = label_symbols };
        const IrInterpreterProgram program = { .functions = &code, .function_count = 1 };
        for (uint64_t p = 0; check.error == IR_OK && p < 2; p += 1)
        {
            const InterpreterValue argument = { .low = p };
            const InterpreterResult result = ir_interpret(&program, 1, &argument, 1, 0, &execution);
            ir_lower_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == (p != 0 ? 10 : 20), "the promoted computed gotos go to the same labels", 0, (uint32_t)p);
        }
    }
    arena_pop_to(&output, 0);
    arena_pop_to(&twin_output, 0);
    arena_pop_to(&promoted, 0);
    arena_pop_to(&twin_promoted, 0);
    // The same two jumps as jump tables' (#785): each a br_indirect of its own table's label, without a dispatch block;
    // and the first a goto * with the second a table's, the dispatch block's br_indirect listing only the label of no
    // table. Each runs as before, and promoted needs no phi for the jumps' addresses.
    static const uint32_t table_jumps[2] =
    {
        3,
        5
    };
    static const uint32_t two_tables[2] =
    {
        1,
        2
    };
    static const uint32_t one_table[2] =
    {
        0,
        1
    };
    static const uint32_t table_addresses[2] = { 2, 4 };
    static const uint8_t direct_access[4] = { 0 };
    for (uint32_t variant = 0; ready && variant < 4; variant += 1)
    {
        const uint32_t mixed = variant & 1;
        const bool hoist = variant >= 2;
        Tier0Function tabled = twice;
        tabled.label_tables = mixed != 0 ? one_table : two_tables;
        tabled.table_jumps = mixed != 0 ? table_jumps + 1 : table_jumps;
        tabled.table_count = mixed != 0 ? 1 : 2;
        tabled.table_addresses = hoist ? table_addresses + mixed : 0;
        tabled.symbol_access = hoist ? direct_access : 0;
        tabled.symbol_count = 4;
        ir_lower_test_expect(tally, tier0_validate(&tabled).error == TIER0_OK && tier0_validate_scalar(&tabled).error == TIER0_OK,
                             "Tier 0 takes the jump tables' columns", 0, 3 + variant);
        if (ir_lower_test_lower(tally, &tabled, IR_I32, 4, &output, &twin_output, &scratch, &function, 0, 3 + variant))
        {
            // Each br_indirect lists one label, its one successor: label 0 the goto *'s (mixed) or table 0's, label 1
            // the last table's.
            uint32_t jumps = 0;
            uint32_t listed = 0;
            uint32_t dispatches = 0;
            uint32_t addresses = 0;
            uint32_t preheader = 0;
            bool pairs = true;
            for (uint32_t i = 0; i < function.instruction_count; i += 1)
            {
                if (function.op[i] == IR_BR_INDIRECT && function.b[i] != BUSTER_IR_NONE && function.c[i] == 1 && function.extra[function.b[i]] < 2)
                {
                    const uint32_t label = function.extra[function.b[i]];
                    listed |= ir_successor_count(&function, i) == 1 && ir_successor(&function, i, 0) == function.labels[label] ? 1u << label : 0;
                }
                jumps += function.op[i] == IR_BR_INDIRECT;
                dispatches += function.op[i] == IR_BR && function.a[i] == function.block_count - 1;
                if (function.op[i] == IR_SYMBOL_ADDR)
                {
                    addresses += 1;
                    preheader += i < function.block_offsets[1];
                    pairs &= i + 1 < function.instruction_count && function.op[i + 1] == IR_PTRTOINT && function.a[i + 1] == i;
                }
            }
            ir_lower_test_expect(tally, jumps == 2 && listed == 3 && dispatches == mixed, "a jump table's jump is a br_indirect of its own table's labels", 0, 3 + variant);
            ir_lower_test_expect(tally, pairs && addresses == 2 && preheader == (hoist ? tabled.table_count : 0),
                                 "only explicitly listed table addresses are materialized once in the preheader", 0, 3 + variant);
            for (uint64_t p = 0; p < 2; p += 1)
            {
                bool fault = false;
                const uint64_t actual = ir_lower_test_interpret(&function, 1, 0, 0, &data, globals, &p, 1, &execution, &fault);
                ir_lower_test_expect(tally, !fault && actual == (p != 0 ? 10 : 20), "jump tables' jumps go to their labels", 0, 3 + variant);
            }
            ir_lower_test_promote(tally, &function, &promoted, &twin_promoted, &scratch, 0, 3 + variant);
            IrFunction promoted_function =
            {
                0
            };
            const IrCheck check = ssa_ir_promote(&function, &promoted, &scratch, &promoted_function);
            uint32_t phis = 0;
            for (uint32_t i = 0; check.error == IR_OK && i < promoted_function.instruction_count; i += 1)
            {
                phis += promoted_function.op[i] == IR_PHI;
            }
            ir_lower_test_expect(tally, check.error == IR_OK && phis == 0, "jump tables' jumps need no phi of their addresses", 0, 3 + variant);
        }
        arena_pop_to(&output, 0);
        arena_pop_to(&twin_output, 0);
        arena_pop_to(&promoted, 0);
        arena_pop_to(&twin_promoted, 0);
    }
    // #1217: the first table is unreachable, the second jumps back to source entry zero. The preheader must run once,
    // retain both tables' distinct symbols, and dominate the loop without reinitializing its parameter on a backedge.
    static const uint8_t loop_kinds[] = {
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONST, BUSTER_TYPED_SUB, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_JUMP,
        BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_JUMP_INDIRECT,
        BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_JUMP_INDIRECT,
        BUSTER_TYPED_CONST, BUSTER_TYPED_RETURN,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_RETURN,
    };
    static const uint8_t loop_classes[] = {
        BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_VOID,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32,
    };
    static const uint64_t loop_values[] = { 0, 13, 0, 1, 0, 0, 9, 2, 0, 3, 0, 77, 0, 0, 0 };
    static const uint32_t loop_labels[] = { 11, 0 };
    static const uint32_t loop_jumps[] = { 8, 10 };
    static const uint32_t loop_addresses[] = { 7, 9 };
    Tier0Function loop = twice;
    loop.kinds = loop_kinds;
    loop.classes = loop_classes;
    loop.values = loop_values;
    loop.count = BUSTER_ARRAY_LENGTH(loop_kinds);
    loop.label_entries = loop_labels;
    loop.label_tables = two_tables;
    loop.table_jumps = loop_jumps;
    loop.table_addresses = loop_addresses;
    loop.table_count = 2;
    if (ready && ir_lower_test_lower(tally, &loop, IR_I32, 4, &output, &twin_output, &scratch, &function, 0, 7))
    {
        uint32_t addresses = 0;
        uint32_t symbols = 0;
        bool preheader = true;
        for (uint32_t i = 0; i < function.instruction_count; i += 1)
        {
            if (function.op[i] == IR_SYMBOL_ADDR)
            {
                addresses += 1;
                symbols |= function.a[i] == 2 ? 1u : function.a[i] == 3 ? 2u : 0;
                preheader &= i < function.block_offsets[1];
            }
        }
        ir_lower_test_expect(tally, addresses == 2 && symbols == 3 && preheader,
                             "an unreachable table does not confuse the later table's preheader value", 0, 7);
        for (uint64_t p = 0; p < 3; p += 1)
        {
            bool fault = false;
            const uint64_t actual = ir_lower_test_interpret(&function, 1, 0, 0, &data, globals, &p, 1, &execution, &fault);
            ir_lower_test_expect(tally, !fault && actual == 0, "a table backedge to source entry zero preserves the decremented parameter", 0, (uint32_t)p);
        }
        ir_lower_test_promote(tally, &function, &promoted, &twin_promoted, &scratch, 0, 7);
    }
    arena_pop_to(&output, 0);
    arena_pop_to(&twin_output, 0);
    arena_pop_to(&promoted, 0);
    arena_pop_to(&twin_promoted, 0);
    // #148's frame address, which both paths lower to a save of BUSTER_IR_STACK_SAVE_FRAME (#1847), its function's frame
    // pointer, of the class's U64, which the verifier takes.
    static const uint8_t frame_kinds[3] =
    {
        BUSTER_TYPED_FRAME_ADDRESS,
        BUSTER_TYPED_DISCARD,
        BUSTER_TYPED_RETURN
    };
    static const uint8_t frame_classes[3] =
    {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_VOID
    };
    static const uint64_t frame_values[3] = { 0 };
    const Tier0Function frame =
    {
        .kinds = frame_kinds,
        .classes = frame_classes,
        .values = frame_values,
        .count = 3,
    };
    const IrFunction preserved =
    {
        .instruction_count = 123,
        .block_count = 17,
    };
    const bool marked = ready && BUSTER_ARENA_PUSH_ARRAY(&output, uint8_t, 24) != 0 && BUSTER_ARENA_PUSH_ARRAY(&scratch, uint8_t, 40) != 0;
    // #1217's optional table-address column is checked before hoisting: no out-of-range read, wrong stack value,
    // load, GOT or TLS access can enter the preheader. Both validators and both lowerings refuse the same input.
    typedef struct IrLowerTableAddressBreak IrLowerTableAddressBreak;
    struct IrLowerTableAddressBreak
    {
        const char* name;
        uint32_t addresses[2];
        uint8_t kind;
        uint8_t class;
        uint64_t symbol;
        uint8_t access;
        uint32_t symbols;
    };
    static const IrLowerTableAddressBreak address_breaks[] = {
        { "a table address past the entries", { 10, 4 }, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2, BUSTER_TIER0_ACCESS_DIRECT, 4 },
        { "a table address at its jump", { 3, 4 }, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2, BUSTER_TIER0_ACCESS_DIRECT, 4 },
        { "table addresses out of order", { 2, 1 }, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2, BUSTER_TIER0_ACCESS_DIRECT, 4 },
        { "duplicate table addresses", { 2, 2 }, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2, BUSTER_TIER0_ACCESS_DIRECT, 4 },
        { "a table address naming a load", { 2, 4 }, BUSTER_TYPED_GLOBAL, BUSTER_TYPED_CLASS_U64, 2, BUSTER_TIER0_ACCESS_DIRECT, 4 },
        { "a table address with the wrong class", { 2, 4 }, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_I64, 2, BUSTER_TIER0_ACCESS_DIRECT, 4 },
        { "a table address with symbol zero", { 2, 4 }, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0, BUSTER_TIER0_ACCESS_DIRECT, 4 },
        { "a table address past SymbolId", { 2, 4 }, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, UINT64_C(1) << 32, BUSTER_TIER0_ACCESS_DIRECT, 4 },
        { "a table address through the GOT", { 2, 4 }, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2, BUSTER_TIER0_ACCESS_GOT, 4 },
        { "a local-exec TLS table address", { 2, 4 }, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2, BUSTER_TIER0_ACCESS_LOCAL_EXEC, 4 },
        { "an initial-exec TLS table address", { 2, 4 }, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2, BUSTER_TIER0_ACCESS_INITIAL_EXEC, 4 },
        { "a table address past the access column", { 2, 4 }, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2, BUSTER_TIER0_ACCESS_DIRECT, 2 },
    };
    for (uint32_t b = 0; marked && b < BUSTER_ARRAY_LENGTH(address_breaks); b += 1)
    {
        uint8_t broken_kinds[BUSTER_ARRAY_LENGTH(twice_kinds)];
        uint8_t broken_classes[BUSTER_ARRAY_LENGTH(twice_classes)];
        uint64_t broken_values[BUSTER_ARRAY_LENGTH(twice_values)];
        memcpy(broken_kinds, twice_kinds, sizeof(broken_kinds));
        memcpy(broken_classes, twice_classes, sizeof(broken_classes));
        memcpy(broken_values, twice_values, sizeof(broken_values));
        broken_kinds[2] = address_breaks[b].kind;
        broken_classes[2] = address_breaks[b].class;
        broken_values[2] = address_breaks[b].symbol;
        const uint8_t accesses[4] = { 0, 0, address_breaks[b].access, 0 };
        Tier0Function broken = twice;
        broken.kinds = broken_kinds;
        broken.classes = broken_classes;
        broken.values = broken_values;
        broken.label_tables = two_tables;
        broken.table_jumps = table_jumps;
        broken.table_addresses = address_breaks[b].addresses;
        broken.table_count = 2;
        broken.symbol_access = accesses;
        broken.symbol_count = address_breaks[b].symbols;
        const Tier0Check vector = tier0_validate(&broken);
        const Tier0Check scalar = tier0_validate_scalar(&broken);
        ir_lower_test_expect(tally, vector.error == TIER0_ERROR_INVALID && scalar.error == TIER0_ERROR_INVALID && vector.node == scalar.node,
                             address_breaks[b].name, 0, b);
        for (uint32_t path = 0; path < 2; path += 1)
        {
            const uint64_t output_mark = output.length;
            const uint64_t scratch_mark = scratch.length;
            IrFunction result = preserved;
            const IrCheck check = ir_lower_typed_path(&broken, IR_I32, 4, &output, &scratch, &result, path != 0);
            ir_lower_test_expect(tally, check.error == IR_ERROR_STRUCTURE && output.length == output_mark && scratch.length == scratch_mark &&
                                            result.instruction_count == preserved.instruction_count && result.block_count == preserved.block_count,
                                 address_breaks[b].name, path, b);
        }
    }
    for (uint32_t vector = 0; marked && vector < 2; vector += 1)
    {
        const uint64_t output_mark = output.length;
        const uint64_t scratch_mark = scratch.length;
        IrFunction result = preserved;
        const IrCheck check = ir_lower_typed_path(&frame, IR_VOID, 1, &output, &scratch, &result, vector != 0);
        uint32_t saves = 0;
        for (uint32_t i = 0; check.error == IR_OK && i < result.instruction_count; i += 1)
        {
            saves += result.op[i] == IR_STACK_SAVE && result.type[i] == IR_I64 && result.flags[i] == BUSTER_IR_STACK_SAVE_FRAME;
        }
        ir_lower_test_expect(tally, tier0_validate(&frame).error == TIER0_OK && check.error == IR_OK && saves == 1 &&
                                        ir_verify(&result, &scratch, 0).error == IR_OK && ir_verify_scalar(&result, &scratch, 0).error == IR_OK,
                             "the frame address lowered to a frame's stack save", 0, vector);
        arena_pop_to(&output, output_mark);
        arena_pop_to(&scratch, scratch_mark);
    }
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&promoted);
    arena_release(&twin_promoted);
    arena_release(&scratch);
    arena_release(&execution);
}

// #2574: a direct symbol's address the function takes at least twice is one value, which every entry that takes it reads,
// defined at the start of the straight line's one block (#2593: its takes' nearest common dominator, which takes it);
// one taken once, and one reached through its GOT slot, stay where they are. Both lowering paths give the same IR, and
// the function's result is the same: (s1 - s1) + (s2 - s1), the globals 8 bytes apart.
static void ir_lower_test_hoisted_symbols(IrLowerTestTally* tally)
{
    static const uint8_t kinds[] = {
        BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_SUB,
        BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_SUB,
        BUSTER_TYPED_ADD, BUSTER_TYPED_RETURN,
    };
    static const uint8_t classes[] = {
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
    };
    static const uint64_t values[] = { 1, 1, 0, 2, 1, 0, 0, 0 };
    // Without an access column, every symbol direct; with one, symbol 1 direct and then through its GOT slot.
    static const uint8_t direct[3] = { 0, BUSTER_TIER0_ACCESS_DIRECT, BUSTER_TIER0_ACCESS_DIRECT };
    static const uint8_t got[3] = { 0, BUSTER_TIER0_ACCESS_GOT, BUSTER_TIER0_ACCESS_DIRECT };
    const uint8_t* accesses[3] = { 0, direct, got };
    const char* names[3] = {
        "a symbol taken three times is one value, defined in its block; one taken once stays its own",
        "a direct symbol by its access column is one value, defined in its block",
        "a symbol reached through its GOT slot stays in its block",
    };
    Arena output = arena_reserve(1ull << 24);
    Arena twin_output = arena_reserve(1ull << 24);
    Arena scratch = arena_reserve(1ull << 24);
    Arena execution = arena_reserve(1ull << 24);
    const bool ready = output.base != 0 && twin_output.base != 0 && scratch.base != 0 && execution.base != 0;
    Tier0TestData data = { 0 };
    data.offsets[1] = 8;
    uint8_t globals[16] = { 0 };
    for (uint32_t variant = 0; ready && variant < 3; variant += 1)
    {
        const Tier0Function source = {
            .kinds = kinds,
            .classes = classes,
            .values = values,
            .count = BUSTER_ARRAY_LENGTH(kinds),
            .symbol_access = accesses[variant],
            .symbol_count = accesses[variant] != 0 ? 3 : 0,
        };
        IrFunction function = { 0 };
        ir_lower_test_expect(tally, tier0_validate(&source).error == TIER0_OK && tier0_validate_scalar(&source).error == TIER0_OK, "Tier 0 takes the symbols' function", 0, variant);
        if (ir_lower_test_lower(tally, &source, IR_I64, 4, &output, &twin_output, &scratch, &function, 0, variant))
        {
            uint32_t addresses = 0;
            uint32_t preheader = 0;
            uint32_t first = 0;
            bool pairs = true;
            for (uint32_t i = 0; i < function.instruction_count; i += 1)
            {
                if (function.op[i] == IR_SYMBOL_ADDR)
                {
                    // The first address the block computes is symbol 1's, at its start.
                    first += addresses == 0 && function.a[i] == 1;
                    addresses += 1;
                    preheader += i < function.block_offsets[1];
                    pairs &= i + 1 < function.instruction_count && function.op[i + 1] == IR_PTRTOINT && function.a[i + 1] == i;
                }
            }
            const bool hoisted = variant != 2;
            ir_lower_test_expect(tally, pairs && addresses == (hoisted ? 2u : 4u) && preheader == 0 && first == 1, names[variant], 0, variant);
            bool fault = false;
            const uint64_t actual = ir_lower_test_interpret(&function, 1, 0, 2, &data, globals, 0, 0, &execution, &fault);
            ir_lower_test_expect(tally, !fault && actual == 8, "the hoisted symbols' function computes the same difference", 0, variant);
        }
        arena_pop_to(&output, 0);
        arena_pop_to(&twin_output, 0);
        arena_pop_to(&scratch, 0);
        arena_pop_to(&execution, 0);
    }
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&scratch);
    arena_release(&execution);
}

// The symbol_addr instructions of `symbol` in `function`: how many, and in a bit per block (blocks below 32).
static uint32_t ir_lower_test_symbol_blocks(const IrFunction* function, uint32_t symbol, uint32_t* count)
{
    uint32_t blocks = 0;
    uint32_t block = 0;
    *count = 0;
    for (uint32_t i = 0; i < function->instruction_count; i += 1)
    {
        while (block + 1 < function->block_count && function->block_offsets[block + 1] <= i)
        {
            block += 1;
        }
        if (function->op[i] == IR_SYMBOL_ADDR && function->a[i] == symbol)
        {
            *count += 1;
            blocks |= block < 32 ? 1u << block : 0;
        }
    }
    return blocks;
}

// #2593: where a hoisted address is defined. f's blocks (preheader 0, then each source block's node, b + 1):
//     1: if (p == 0) goto 8;          a path that takes nothing
//     2: x = &s1; if (p != 1) goto 4;
//     3: r = &s2 - &s1 + &s3 - x; goto 7;
//     4: if (p != 2) goto 6;
//     5: r = &s2 - x; goto 7;
//     6: r = 5;
//     7: return r + (&s3 - &s1);
//     8: return 7;
// s1's takes' nearest common dominator is 2, which takes it: one definition, at 2's start, not in 0 or 1. s2's is 2
// too, but the path through 6 takes it nowhere: its two takes stay their own. s3's is 2, from which every path takes it:
// one definition, at 2's start after s1's. g's (locals p and r):
//     1: if (p != 0) goto 3;
//     2: return 9;
//     3: r = 0;
//     4: r = r + (&s4 - &s4) + 1; if (p - 5 != 0) goto 6;      the loop's header
//     5: &s5; return r + 100;
//     6: p = p - 1; if (p != 0) goto 4;
//     7: &s5; return r;
// s4's takes are in the loop, whose header's immediate dominator, 3, defines it, outside the loop. s5's, on
// two of its exits, have their nearest common dominator in the loop and none inside it: they stay their own. Both
// lowering paths give the same IR, which the interpreter runs and promotion takes.
static void ir_lower_test_hoisted_places(IrLowerTestTally* tally)
{
    static const uint8_t f_kinds[] = {
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONST, BUSTER_TYPED_SUB, BUSTER_TYPED_BRANCH_TRUE,
        BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_SUB, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_ADD, BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_SUB, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_JUMP,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONST, BUSTER_TYPED_SUB, BUSTER_TYPED_BRANCH_TRUE,
        BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_LOCAL, BUSTER_TYPED_SUB, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_JUMP,
        BUSTER_TYPED_CONST, BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_SUB, BUSTER_TYPED_ADD, BUSTER_TYPED_RETURN,
        BUSTER_TYPED_CONST, BUSTER_TYPED_RETURN,
    };
    static const uint8_t f_classes[] = {
        BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID,
        BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
    };
    static const uint64_t f_values[] = {
        0, 34,
        1, 1, 0, 1, 0, 17,
        2, 1, 0, 3, 0, 1, 0, 2, 28,
        0, 2, 0, 26,
        2, 1, 0, 2, 28,
        5, 2,
        2, 3, 1, 0, 0, 0,
        7, 0,
    };
    static const uint8_t g_kinds[] = {
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_BRANCH_TRUE,
        BUSTER_TYPED_CONST, BUSTER_TYPED_RETURN,
        BUSTER_TYPED_CONST, BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_SUB, BUSTER_TYPED_ADD, BUSTER_TYPED_CONST, BUSTER_TYPED_ADD,
        BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONST, BUSTER_TYPED_SUB, BUSTER_TYPED_BRANCH_TRUE,
        BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_DISCARD, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONST, BUSTER_TYPED_ADD, BUSTER_TYPED_RETURN,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONST, BUSTER_TYPED_SUB, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_LOCAL, BUSTER_TYPED_BRANCH_TRUE,
        BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_DISCARD, BUSTER_TYPED_LOCAL, BUSTER_TYPED_RETURN,
    };
    static const uint8_t g_classes[] = {
        BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
    };
    static const uint64_t g_values[] = {
        0, 4,
        9, 0,
        0, 1,
        1, 4, 4, 0, 0, 1, 0, 1, 0, 5, 0, 24,
        5, 0, 1, 100, 0, 0,
        0, 1, 0, 0, 0, 6,
        5, 0, 1, 0,
    };
    static const uint8_t local_classes[3] = { BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
    static const uint8_t local_flags[3] = { 0 };
    const AbiLocation parameter = { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { X86_REGISTER_RDI } };
    _Static_assert(BUSTER_ARRAY_LENGTH(f_kinds) == BUSTER_ARRAY_LENGTH(f_classes) && BUSTER_ARRAY_LENGTH(f_kinds) == BUSTER_ARRAY_LENGTH(f_values), "f's columns");
    _Static_assert(BUSTER_ARRAY_LENGTH(g_kinds) == BUSTER_ARRAY_LENGTH(g_classes) && BUSTER_ARRAY_LENGTH(g_kinds) == BUSTER_ARRAY_LENGTH(g_values), "g's columns");
    const Tier0Function sources[2] = {
        {
            .kinds = f_kinds,
            .classes = f_classes,
            .values = f_values,
            .count = BUSTER_ARRAY_LENGTH(f_kinds),
            .local_classes = local_classes,
            .local_flags = local_flags,
            .local_count = 3,
            .parameter_count = 1,
            .parameters = &parameter,
        },
        {
            .kinds = g_kinds,
            .classes = g_classes,
            .values = g_values,
            .count = BUSTER_ARRAY_LENGTH(g_kinds),
            .local_classes = local_classes,
            .local_flags = local_flags,
            .local_count = 2,
            .parameter_count = 1,
            .parameters = &parameter,
        },
    };
    // f's results for p = 0 to 3, with s1, s2 and s3 at 0, 8 and 16; g's for p = 0, 3 and 7.
    static const uint64_t f_expected[4] = { 7, 40, 24, 21 };
    static const uint64_t g_arguments[3] = { 0, 3, 7 };
    static const uint64_t g_expected[3] = { 9, 3, 103 };
    Arena output = arena_reserve(1ull << 24);
    Arena twin_output = arena_reserve(1ull << 24);
    Arena promoted = arena_reserve(1ull << 24);
    Arena twin_promoted = arena_reserve(1ull << 24);
    Arena scratch = arena_reserve(1ull << 24);
    Arena execution = arena_reserve(1ull << 24);
    const bool ready = output.base != 0 && twin_output.base != 0 && promoted.base != 0 && twin_promoted.base != 0 && scratch.base != 0 && execution.base != 0;
    ir_lower_test_expect(tally, ready, "hoisted places' arenas", 0, 0);
    // Symbol k's global is data.offsets[k - 1]'s.
    Tier0TestData data = { 0 };
    data.offsets[1] = 8;
    data.offsets[2] = 16;
    data.offsets[3] = 24;
    data.offsets[4] = 32;
    uint8_t globals[48] = { 0 };
    for (uint32_t which = 0; ready && which < 2; which += 1)
    {
        const Tier0Function* source = &sources[which];
        IrFunction function = { 0 };
        ir_lower_test_expect(tally, tier0_validate(source).error == TIER0_OK && tier0_validate_scalar(source).error == TIER0_OK, "Tier 0 takes the hoisted places' functions", 0, which);
        if (ir_lower_test_lower(tally, source, IR_I64, 6, &output, &twin_output, &scratch, &function, 0, which))
        {
            uint32_t counts[6] = { 0 };
            uint32_t blocks[6] = { 0 };
            for (uint32_t symbol = 1; symbol < 6; symbol += 1)
            {
                blocks[symbol] = ir_lower_test_symbol_blocks(&function, symbol, &counts[symbol]);
            }
            if (which == 0)
            {
                // Block 2 defines s1 and then s3, in their first takes' order.
                uint32_t order = 0;
                for (uint32_t i = function.block_offsets[2]; i < function.block_offsets[3]; i += 1)
                {
                    order = function.op[i] == IR_SYMBOL_ADDR ? order * 4 + function.a[i] : order;
                }
                ir_lower_test_expect(tally, counts[1] == 1 && blocks[1] == 1u << 2, "a symbol taken past an early return is defined in its takes' block, past the return", 0, 0);
                ir_lower_test_expect(tally, counts[2] == 2 && blocks[2] == ((1u << 3) | (1u << 5)), "takes on two of three paths stay their own", 0, 0);
                ir_lower_test_expect(tally, counts[3] == 1 && blocks[3] == 1u << 2 && order == 1 * 4 + 3, "a symbol every path takes is defined in its takes' common dominator", 0, 0);
                for (uint64_t p = 0; p < 4; p += 1)
                {
                    bool fault = false;
                    const uint64_t actual = ir_lower_test_interpret(&function, 1, 0, 3, &data, globals, &p, 1, &execution, &fault);
                    ir_lower_test_expect(tally, !fault && actual == f_expected[p], "f's placed addresses compute its results", p, 0);
                }
            }
            else
            {
                ir_lower_test_expect(tally, counts[4] == 1 && blocks[4] == 1u << 3, "a symbol taken in a loop is defined before it, in its header's dominator", 0, 1);
                ir_lower_test_expect(tally, counts[5] == 2 && blocks[5] == ((1u << 5) | (1u << 7)), "takes on a loop's exits stay their own", 0, 1);
                for (uint32_t k = 0; k < 3; k += 1)
                {
                    bool fault = false;
                    const uint64_t actual = ir_lower_test_interpret(&function, 1, 0, 5, &data, globals, &g_arguments[k], 1, &execution, &fault);
                    ir_lower_test_expect(tally, !fault && actual == g_expected[k], "g's placed addresses compute its results", g_arguments[k], 1);
                }
            }
            ir_lower_test_promote(tally, &function, &promoted, &twin_promoted, &scratch, 0, which);
        }
        arena_pop_to(&output, 0);
        arena_pop_to(&twin_output, 0);
        arena_pop_to(&promoted, 0);
        arena_pop_to(&twin_promoted, 0);
        arena_pop_to(&scratch, 0);
        arena_pop_to(&execution, 0);
    }
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&promoted);
    arena_release(&twin_promoted);
    arena_release(&scratch);
    arena_release(&execution);
}

// The expected results of ir_lower_test_depth_types' functions, by C: f, and g = 100 + f.
static uint64_t ir_lower_test_depth_types_expected(uint64_t p, bool pending)
{
    uint32_t s = 0;
    for (uint32_t n = (uint32_t)p; n != 0; n -= 1)
    {
        const uint64_t a = (n & 1) != 0 ? 7 : 9;
        s = s + ((n & 2) != 0 ? 3u : 4u) + (uint32_t)a;
    }
    return pending ? 100 + (uint64_t)s : s;
}

// One of ir_lower_test_depth_types' functions lowered by both paths: its temporary (depth) slots, `temporaries` of them,
// each reached by one type's loads and stores alone, `wide` of them i64's and the others i32's; its results at six
// arguments C's; and promoted by both paths to no slot, load or store, its results the same by the shared IR's
// interpreter.
static void ir_lower_test_depth_check(IrLowerTestTally* tally, const Tier0Function* source, IrType result_type, bool pending, uint32_t temporaries, uint32_t wide, uint32_t index)
{
    static const uint64_t arguments[] =
    {
        0,
        1,
        2,
        3,
        6,
        11
    };
    Arena output = arena_reserve(1ull << 20);
    Arena twin_output = arena_reserve(1ull << 20);
    Arena promoted = arena_reserve(1ull << 20);
    Arena twin_promoted = arena_reserve(1ull << 20);
    Arena scratch = arena_reserve(1ull << 20);
    Arena execution = arena_reserve(1ull << 26);
    const bool ready = output.base != 0 && twin_output.base != 0 && promoted.base != 0 && twin_promoted.base != 0 && scratch.base != 0 && execution.base != 0;
    ir_lower_test_expect(tally, ready, "depth-type arenas", 0, index);
    IrFunction function =
    {
        0
    };
    const Tier0TestData data =
    {
        0
    };
    uint8_t globals[1] =
    {
        0
    };
    if (ready && ir_lower_test_lower(tally, source, result_type, 1, &output, &twin_output, &scratch, &function, 0, index))
    {
        // Each temporary slot's access types, a bit per IR type, by the loads' types and the stored values'.
        uint32_t types[16] =
        {
            0
        };
        const bool bounded = function.slot_count <= BUSTER_ARRAY_LENGTH(types);
        for (uint32_t i = 0; bounded && i < function.instruction_count; i += 1)
        {
            const bool access = function.op[i] == IR_LOAD || function.op[i] == IR_STORE;
            const uint32_t address = access ? function.a[i] : BUSTER_IR_NONE;
            const bool slot = address < function.instruction_count && function.op[address] == IR_STACK_ADDR;
            const uint32_t s = slot ? function.a[address] : 0;
            const uint32_t type = function.op[i] == IR_LOAD ? function.type[i] : access ? function.type[function.b[i]] : 0;
            if (slot && s < function.slot_count && (function.slots[s].flags & BUSTER_IR_SLOT_TEMPORARY) != 0)
            {
                types[s] |= 1u << type;
            }
        }
        uint32_t found = 0;
        uint32_t wides = 0;
        uint32_t narrows = 0;
        bool single = bounded;
        for (uint32_t s = 0; bounded && s < function.slot_count; s += 1)
        {
            const bool temporary = (function.slots[s].flags & BUSTER_IR_SLOT_TEMPORARY) != 0;
            found += temporary;
            single = single && (!temporary || __builtin_popcount(types[s]) == 1);
            wides += temporary && types[s] == 1u << IR_I64;
            narrows += temporary && types[s] == 1u << IR_I32;
        }
        ir_lower_test_expect(tally, single && found == temporaries && wides == wide && narrows == temporaries - wide,
                             "a depth slot per depth and type, each reached by one type alone", 0, index);
        for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(arguments); k += 1)
        {
            bool fault = false;
            const uint64_t actual = ir_lower_test_interpret(&function, 1, 0, 0, &data, globals, &arguments[k], 1, &execution, &fault);
            ir_lower_test_expect(tally, !fault && actual == ir_lower_test_depth_types_expected(arguments[k], pending), "the depth slots' values reach their joins across the loop", 0, index);
        }
        ir_lower_test_promote(tally, &function, &promoted, &twin_promoted, &scratch, 0, index);
        IrFunction promoted_function =
        {
            0
        };
        const IrCheck check = ssa_ir_promote(&function, &promoted, &scratch, &promoted_function);
        uint32_t memory = 0;
        for (uint32_t i = 0; check.error == IR_OK && i < promoted_function.instruction_count; i += 1)
        {
            memory += promoted_function.op[i] == IR_LOAD || promoted_function.op[i] == IR_STORE || promoted_function.op[i] == IR_STACK_ADDR;
        }
        ir_lower_test_expect(tally, check.error == IR_OK && memory == 0 && promoted_function.slot_count == 0, "promotion takes every depth slot and local: no slot, load or store is left", 0, index);
        // The promoted function has phis, which this executor doesn't take: the shared IR's interpreter runs it.
        const IrInterpreterFunction code = { .symbol = 1, .function = &promoted_function };
        const IrInterpreterProgram program = { .functions = &code, .function_count = 1 };
        for (uint32_t k = 0; check.error == IR_OK && k < BUSTER_ARRAY_LENGTH(arguments); k += 1)
        {
            const InterpreterValue argument = { .low = arguments[k] };
            const InterpreterResult result = ir_interpret(&program, 1, &argument, 1, 0, &execution);
            const uint64_t value = result_type == IR_I32 ? (uint32_t)result.value.low : result.value.low;
            ir_lower_test_expect(tally, result.status == INTERPRETER_OK && value == ir_lower_test_depth_types_expected(arguments[k], pending), "the promoted function computes the same", 0, index);
        }
    }
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&promoted);
    arena_release(&twin_promoted);
    arena_release(&scratch);
    arena_release(&execution);
}

// #1189: one depth of a function's expression stack carries values of different types into different blocks, so the
// lowering gives it a slot per type. A plan that let a block's stores and its successor's loads pick different slots, or
// gave two types one slot again (whose slot then stays in memory), fails ir_lower_test_depth_check.
//
// f's depth 0 carries a U64 into one `?:` join and an I32 into another, and its depth 1 an I32, the joins in a loop:
//     int f(int p) { int s = 0; for (int n = p; n != 0; n = n - 1) { long a = n & 1 ? 7 : 9;
//                                                                     s = s + (n & 2 ? 3 : 4) + (int)a; } return s; }
// g is f's loop in a GNU statement expression under a pending 100L, so depth 0's U64 crosses the loop's backedge in its
// slot and the joins' values are a depth higher, depth 1 a U64's and an I32's:
//     long g(int p) { return 100L + ({ int s = 0; for (...) { ... } s; }); }
static void ir_lower_test_depth_types(IrLowerTestTally* tally)
{
    // Locals: p (the parameter), s, n and a. f's 4 and 32 are its loop's head and exit, 13 and 22 its joins; g's are 5
    // and 33, 14 and 23.
    static const uint8_t f_kinds[] =
    {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_AND,
        BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_JUMP_VALUE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_AND,
        BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_JUMP_VALUE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONVERT,
        BUSTER_TYPED_ADD,
        BUSTER_TYPED_ADD,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_SUB,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_JUMP,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_RETURN
    };
    static const uint8_t f_classes[] =
    {
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32
    };
    static const uint64_t f_values[] =
    {
        0,
        1,
        0,
        2,
        2,
        32,
        2,
        1,
        0,
        12,
        7,
        13,
        9,
        3,
        1,
        2,
        2,
        0,
        21,
        3,
        22,
        4,
        3,
        0,
        0,
        0,
        1,
        2,
        1,
        0,
        2,
        4,
        1,
        0
    };
    static const uint8_t g_kinds[] =
    {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_AND,
        BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_JUMP_VALUE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_AND,
        BUSTER_TYPED_BRANCH_FALSE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_JUMP_VALUE,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONVERT,
        BUSTER_TYPED_ADD,
        BUSTER_TYPED_ADD,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_SUB,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_JUMP,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_CONVERT,
        BUSTER_TYPED_ADD,
        BUSTER_TYPED_RETURN
    };
    static const uint8_t g_classes[] =
    {
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64
    };
    static const uint64_t g_values[] =
    {
        100,
        0,
        1,
        0,
        2,
        2,
        33,
        2,
        1,
        0,
        13,
        7,
        14,
        9,
        3,
        1,
        2,
        2,
        0,
        22,
        3,
        23,
        4,
        3,
        0,
        0,
        0,
        1,
        2,
        1,
        0,
        2,
        5,
        1,
        0,
        0,
        0
    };
    _Static_assert(BUSTER_ARRAY_LENGTH(f_kinds) == BUSTER_ARRAY_LENGTH(f_classes) && BUSTER_ARRAY_LENGTH(f_kinds) == BUSTER_ARRAY_LENGTH(f_values), "one class and value per entry");
    _Static_assert(BUSTER_ARRAY_LENGTH(g_kinds) == BUSTER_ARRAY_LENGTH(g_classes) && BUSTER_ARRAY_LENGTH(g_kinds) == BUSTER_ARRAY_LENGTH(g_values), "one class and value per entry");
    static const uint8_t local_classes[4] =
    {
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_I32,
        BUSTER_TYPED_CLASS_U64
    };
    static const uint8_t local_flags[4] =
    {
        0,
        0,
        0,
        0
    };
    const AbiLocation parameter =
    {
        .kind = BUSTER_ABI_LOCATION_REGISTERS,
        .registers =
        {
            X86_REGISTER_RDI
        },
    };
    const Tier0Function f =
    {
        .kinds = f_kinds,
        .classes = f_classes,
        .values = f_values,
        .count = BUSTER_ARRAY_LENGTH(f_kinds),
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 4,
        .parameter_count = 1,
        .parameters = &parameter,
    };
    const Tier0Function g =
    {
        .kinds = g_kinds,
        .classes = g_classes,
        .values = g_values,
        .count = BUSTER_ARRAY_LENGTH(g_kinds),
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 4,
        .parameter_count = 1,
        .parameters = &parameter,
    };
    // f: depth 0's i64 and i32, depth 1's i32. g: depth 0's i64, depth 1's i64 and i32, depth 2's i32.
    ir_lower_test_depth_check(tally, &f, IR_I32, false, 3, 1, 0);
    ir_lower_test_depth_check(tally, &g, IR_I64, true, 4, 2, 1);
}

// Defined after the production interpreter, optimizers and selector in ir_stack_test.c: exercise their real contracts.
static void ir_lower_test_stack(IrLowerTestTally* tally);

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

// A variadic definition's frame through the shared IR's interpreter (#788): test-tier0-variadic-frame's original callees
// (va_start, va_copy and va_arg's recipes over general, vector and overflow arguments, and kind 4's lists without an
// incoming frame), lowered by both paths and run natively from clang's caller in a child, onto a shared page, and by the
// interpreter from an IR caller of the same arguments. Every output that isn't an address must be the native run's: the
// named parameters, each va_arg's and its copy's, the lists' offsets and how far their overflow cursors moved, the save
// area's argument slots, the canaries, the object and the result. Kind 3 passes vectors, which the IR has no type for.
// The stream reads the save area whole, whose other bytes the IR's frame leaves undefined: the interpreter runs without
// undefined-byte checks.
static InterpreterResult ir_lower_test_variadic_typed(const Tier0Function* callee, const uint16_t* arguments, uint32_t count, Tier0VfOutput* output, Arena* execution)
{
    uint8_t kinds[28];
    uint8_t classes[28];
    uint64_t values[28];
    uint8_t argument_classes[26];
    AbiLocation places[26];
    uint32_t integers = 0;
    uint32_t vectors = 0;
    uint64_t stack_bytes = 0;
    uint8_t alignment = BUSTER_ABI_STACK_ALIGNMENT_LOG2;
    for (uint32_t a = 0; a < count; a += 1)
    {
        const bool floating = arguments[a] >= 0x200;
        argument_classes[a] = a < callee->parameter_count ? callee->local_classes[a] : floating ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_I64;
        kinds[a] = a == 0 ? BUSTER_TYPED_GLOBAL_ADDRESS : BUSTER_TYPED_CONST;
        classes[a] = argument_classes[a];
        values[a] = a == 0 ? 4 : floating ? tier0_vf_fp_bits(arguments[a] - 0x200) : tier0_vf_gp(arguments[a] - 0x100);
        places[a] = interpreter_native_place(floating ? IR_F64 : IR_I64, &integers, &vectors, &stack_bytes, &alignment);
    }
    kinds[count] = BUSTER_TYPED_CALL;
    kinds[count + 1] = BUSTER_TYPED_RETURN;
    classes[count] = classes[count + 1] = BUSTER_TYPED_CLASS_U64;
    values[count] = values[count + 1] = 0;
    const SymbolId target = 3;
    const uint32_t first = 0;
    const uint8_t variadic = callee->incoming_variadic != 0;
    const AbiCall call = { .stack_bytes = (uint32_t)((stack_bytes + 7) & ~UINT64_C(7)), .stack_alignment_log2 = alignment,
                          .integer_registers = (uint8_t)integers, .vector_registers = (uint8_t)vectors, .result = callee->result };
    const Tier0Function functions[] = {
        *callee,
        { .kinds = kinds, .classes = classes, .values = values, .count = count + 2, .call_targets = &target, .call_first = &first,
          .call_argument_counts = &count, .call_variadic = &variadic, .calls = &call, .argument_classes = argument_classes, .argument_locations = places,
          .call_count = 1, .argument_count = count },
    };
    const SymbolId symbols[] = { 3, 5 };
    const InterpreterBinding binding = { .symbol = 4, .bytes = (uint8_t*)output, .size = sizeof(*output), .writable = true };
    const InterpreterNativeBinding clobber = { .symbol = 1, .address = (void*)&tier0_vf_clobber };
    const InterpreterProgram program = { .functions = functions, .function_symbols = symbols, .function_count = 2, .bindings = &binding, .binding_count = 1,
                                         .natives = &clobber, .native_count = 1 };
    InterpreterOptions options = interpreter_default_options();
    options.check_undefined = false;
    InterpreterResult result;
    interpreter_typed_run(&program, 5, 0, 0, execution, &options, &result);
    return result;
}

static void ir_lower_test_variadic_frames(IrLowerTestTally* tally)
{
    static const uint8_t kinds[] = { 0, 1, 2, 4, 5 };
    static const uint8_t named[] = { 1, 3, 18, 1, 7 };
    static const uint8_t counts[] = { 11, 21, 26, 1, 15 };
    // Each kind's arguments as tier0_vf_invoke passes them: 0 the output, 0x100 + i tier0_vf_gp(i), 0x200 + i tier0_vf_fp(i).
    static const uint16_t arguments[][26] = {
        { 0, 0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108, 0x109 },
        { 0, 0x11e, 0x209, 0x100, 0x200, 0x101, 0x201, 0x102, 0x202, 0x103, 0x203, 0x104, 0x204, 0x105, 0x205, 0x106, 0x206, 0x107, 0x207, 0x208, 0x209 },
        { 0, 0x11e, 0x11f, 0x120, 0x121, 0x122, 0x123, 0x124, 0x200, 0x201, 0x202, 0x203, 0x204, 0x205, 0x206, 0x207, 0x208, 0x209, 0x100, 0x200, 0x101, 0x201, 0x102, 0x202, 0x103, 0x203 },
        { 0 },
        { 0, 0x11e, 0x11f, 0x120, 0x121, 0x122, 0x123, 0x100, 0x200, 0x101, 0x201, 0x102, 0x202, 0x103, 0x203 },
    };
    Tier0Tables tables =
    {
        0
    };
    Arena arena = arena_reserve(UINT64_C(1) << 26);
    Arena output = arena_reserve(UINT64_C(1) << 24);
    Arena twin_output = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 26);
    Arena execution = arena_reserve(UINT64_C(1) << 26);
    Tier0VfStream* stream = calloc(1, sizeof(*stream));
    Tier0VfOutput* interpreted = calloc(1, sizeof(*interpreted));
    uint8_t* page = mmap(NULL, BUSTER_TIER0_VF_PAGE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    uint32_t compared = 0;
    bool ready = arena.base != 0 && output.base != 0 && twin_output.base != 0 && scratch.base != 0 && execution.base != 0 && stream != 0 && interpreted != 0 && page != MAP_FAILED && tier0_tables_open(&tables);
    ready = ir_lower_test_expect(tally, ready, "variadic frames' arenas and ABI tables", 0, 0);
    for (uint32_t k = 0; ready && k < BUSTER_ARRAY_LENGTH(kinds); k += 1)
    {
        const uint32_t kind = kinds[k];
        Tier0VfTally vf =
        {
            0
        };
        Tier0VfNative native =
        {
            0
        };
        IrFunction callee =
        {
            0
        };
        bool ok = tier0_vf_fixture(&vf, stream, &tables, kind, false) && tier0_vf_build(&vf, &tables, &arena, kind, &native);
        ok = ir_lower_test_expect(tally, ok && vf.failures == 0, "Tier 0's variadic callee and its native code", kind, 0);
        ok = ok && ir_lower_test_lower(tally, &stream->function, IR_I64, 2, &output, &twin_output, &scratch, &callee, kind, 0);
        // Natively, from clang's caller, in a child: the output and then the result on the shared page.
        Tier0VfOutput* native_output = (Tier0VfOutput*)(void*)page;
        uint64_t native_result = 0;
        if (ok)
        {
            memset(page, 0xa5, BUSTER_TIER0_VF_PAGE);
            fflush(NULL);
            const pid_t child = fork();
            if (child == 0)
            {
                alarm(15);
                const uint64_t returned = tier0_vf_invoke(&native.images[0][0], native_output, kind);
                memcpy(page + sizeof(Tier0VfOutput), &returned, sizeof(returned));
                _exit(0);
            }
            int status = 0;
            pid_t waited;
            do
            {
                waited = child > 0 ? waitpid(child, &status, 0) : -1;
            } while (child > 0 && waited < 0 && errno == EINTR);
            ok = ir_lower_test_expect(tally, child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0, "the native variadic callee returns", kind, 0);
            memcpy(&native_result, page + sizeof(Tier0VfOutput), sizeof(native_result));
        }
        // The IR caller: the output's address and the constants, in a call of the callee as variadic as it is.
        IrFunction caller =
        {
            0
        };
        const uint32_t count = counts[k];
        const IrSizes sizes =
        {
            .instruction_count = (uint64_t)count + 3,
            .block_count = 1,
            .extra_count = count,
            .constant_count = count,
            .call_count = 1,
            .call_type_count = named[k],
        };
        ok = ok && ir_create(&output, &sizes, &caller) == IR_OK;
        if (ok)
        {
            caller.result_type = IR_I64;
            caller.symbol_count = 8;
            caller.block_offsets[1] = count + 3;
            caller.op[0] = IR_SYMBOL_ADDR;
            caller.type[0] = IR_PTR;
            caller.a[0] = 4;
            caller.b[0] = BUSTER_IR_NONE;
            caller.c[0] = BUSTER_IR_NONE;
            caller.op[1] = IR_PTRTOINT;
            caller.type[1] = IR_I64;
            caller.a[1] = 0;
            caller.b[1] = BUSTER_IR_NONE;
            caller.c[1] = BUSTER_IR_NONE;
            caller.extra[0] = 1;
            for (uint32_t p = 1; p < count; p += 1)
            {
                const uint16_t argument = arguments[k][p];
                const uint32_t id = 1 + p;
                const bool floating = argument >= 0x200;
                caller.constants[p] = (IrConstant){ .low = floating ? tier0_vf_fp_bits(argument - 0x200) : tier0_vf_gp(argument - 0x100) };
                caller.op[id] = IR_CONST;
                caller.type[id] = floating ? IR_F64 : IR_I64;
                caller.a[id] = p;
                caller.b[id] = BUSTER_IR_NONE;
                caller.c[id] = BUSTER_IR_NONE;
                caller.extra[p] = id;
            }
            for (uint32_t p = 0; p < named[k]; p += 1)
            {
                caller.call_types[p] = caller.type[caller.extra[p]];
            }
            caller.calls[0] = (IrCall){
                .argument_count = count,
                .parameter_count = named[k],
                .result_type = IR_I64,
                .variadic = kind != 4,
            };
            caller.op[count + 1] = IR_CALL;
            caller.type[count + 1] = IR_I64;
            caller.a[count + 1] = 3;
            caller.b[count + 1] = 0;
            caller.c[count + 1] = BUSTER_IR_NONE;
            caller.op[count + 2] = IR_RET;
            caller.type[count + 2] = IR_VOID;
            caller.a[count + 2] = count + 1;
            caller.b[count + 2] = BUSTER_IR_NONE;
            caller.c[count + 2] = BUSTER_IR_NONE;
            memset(interpreted, 0xa5, sizeof(*interpreted));
            const IrInterpreterFunction functions[] =
            {
                { .symbol = 3, .function = &callee },
                { .symbol = 5, .function = &caller },
            };
            const InterpreterBinding binding = { .symbol = 4, .bytes = (uint8_t*)interpreted, .size = sizeof(*interpreted), .writable = true };
            const InterpreterNativeBinding clobber = { .symbol = 1, .address = (void*)&tier0_vf_clobber };
            const IrInterpreterProgram program = { .functions = functions, .function_count = 2, .bindings = &binding, .binding_count = 1, .natives = &clobber, .native_count = 1 };
            InterpreterOptions options = interpreter_default_options();
            options.check_undefined = false;
            const InterpreterResult result = ir_interpret(&program, 5, 0, 0, &options, &execution);
            ok = ir_lower_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == native_result && native_result == BUSTER_TIER0_VF_RETURN, "the interpreted variadic callee returns the native result", kind, 0);
            if (result.status != INTERPRETER_OK)
            {
                fprintf(stderr, "  shared IR status %u, function %u, instruction %u\n", result.status, result.function, result.entry);
            }
        }
        if (ok)
        {
            const Tier0VfOutput* o = interpreted;
            const Tier0VfOutput* n = native_output;
            bool same = o->named[0] == (uintptr_t)o && n->named[0] == (uintptr_t)n;
            for (uint32_t p = 1; p < BUSTER_TIER0_VF_PARAMETERS; p += 1)
            {
                same = same && o->named[p] == n->named[p];
            }
            ir_lower_test_expect(tally, same, "the named parameters are the native run's", kind, 0);
            ir_lower_test_expect(tally, memcmp(o->values, n->values, sizeof(o->values)) == 0 && memcmp(o->copy_values, n->copy_values, sizeof(o->copy_values)) == 0,
                                 "each va_arg and its copy's are the native run's", kind, 0);
            ir_lower_test_expect(tally, memcmp(o->canaries, n->canaries, sizeof(o->canaries)) == 0 && memcmp(o->object, n->object, sizeof(o->object)) == 0,
                                 "the canaries and the object are the native run's", kind, 0);
            if (kind == 4)
            {
                ir_lower_test_expect(tally, memcmp(o->initial, n->initial, sizeof(o->initial)) == 0 && memcmp(o->copied, n->copied, sizeof(o->copied)) == 0 &&
                                                memcmp(o->final, n->final, sizeof(o->final)) == 0 && memcmp(o->copy_final, n->copy_final, sizeof(o->copy_final)) == 0,
                                     "lists without an incoming frame are the native run's", kind, 0);
            }
            else
            {
                AbiVaList lists[4];
                memcpy(&lists[0], o->initial, sizeof(AbiVaList));
                memcpy(&lists[1], o->final, sizeof(AbiVaList));
                memcpy(&lists[2], n->initial, sizeof(AbiVaList));
                memcpy(&lists[3], n->final, sizeof(AbiVaList));
                ir_lower_test_expect(tally, lists[0].gp_offset == lists[2].gp_offset && lists[0].fp_offset == lists[2].fp_offset && lists[1].gp_offset == lists[3].gp_offset &&
                                                lists[1].fp_offset == lists[3].fp_offset && lists[1].overflow_arg_area - lists[0].overflow_arg_area == lists[3].overflow_arg_area - lists[2].overflow_arg_area &&
                                                lists[0].reg_save_area == o->addresses[2] && lists[1].reg_save_area == o->addresses[2] && lists[0].overflow_arg_area == o->addresses[3] &&
                                                memcmp(o->initial, o->copied, sizeof(o->initial)) == 0 && memcmp(o->final, o->copy_final, sizeof(o->final)) == 0,
                                     "va_start's offsets and the overflow cursor's moves are the native run's, at the frame's anchors", kind, 0);
                const uint32_t vectors = kind == 1 || kind == 2 ? 8 : kind == 5 ? 4 : 0;
                bool saved = o->save_before[0] == (uintptr_t)o;
                for (uint32_t slot = 1; slot < BUSTER_ABI_INTEGER_ARGUMENTS; slot += 1)
                {
                    saved = saved && o->save_before[slot] == n->save_before[slot] && o->save_after[slot] == o->save_before[slot];
                }
                for (uint32_t slot = 0; slot < vectors; slot += 1)
                {
                    const uint32_t word = BUSTER_ABI_INTEGER_ARGUMENTS + 2 * slot;
                    saved = saved && o->save_before[word] == n->save_before[word] && o->save_after[word] == o->save_before[word];
                }
                ir_lower_test_expect(tally, saved, "the register save area's argument slots are the native run's", kind, 0);
            }
            compared += 1;
        }
        if (ok)
        {
            memset(interpreted, 0xa5, sizeof(*interpreted));
            const InterpreterResult typed = ir_lower_test_variadic_typed(&stream->function, arguments[k], count, interpreted, &execution);
            const Tier0VfOutput* o = interpreted;
            const Tier0VfOutput* n = native_output;
            bool same = typed.status == INTERPRETER_OK && typed.value.low == native_result && o->named[0] == (uintptr_t)o;
            for (uint32_t p = 1; p < BUSTER_TIER0_VF_PARAMETERS; p += 1)
            {
                same = same && o->named[p] == n->named[p];
            }
            same = same && memcmp(o->values, n->values, sizeof(o->values)) == 0 && memcmp(o->copy_values, n->copy_values, sizeof(o->copy_values)) == 0 &&
                   memcmp(o->canaries, n->canaries, sizeof(o->canaries)) == 0 && memcmp(o->object, n->object, sizeof(o->object)) == 0;
            if (kind != 4)
            {
                AbiVaList lists[4];
                memcpy(&lists[0], o->initial, sizeof(AbiVaList));
                memcpy(&lists[1], o->final, sizeof(AbiVaList));
                memcpy(&lists[2], n->initial, sizeof(AbiVaList));
                memcpy(&lists[3], n->final, sizeof(AbiVaList));
                same = same && lists[0].gp_offset == lists[2].gp_offset && lists[0].fp_offset == lists[2].fp_offset &&
                       lists[1].gp_offset == lists[3].gp_offset && lists[1].fp_offset == lists[3].fp_offset &&
                       lists[1].overflow_arg_area - lists[0].overflow_arg_area == lists[3].overflow_arg_area - lists[2].overflow_arg_area &&
                       lists[0].reg_save_area == o->addresses[2] && lists[0].overflow_arg_area == o->addresses[3] &&
                       memcmp(o->initial, o->copied, sizeof(o->initial)) == 0 && memcmp(o->final, o->copy_final, sizeof(o->final)) == 0;
            }
            ir_lower_test_expect(tally, same, "production typed variadic values, copies, named parameters, canaries and cursors match native execution", kind, 0);
            if (typed.status != INTERPRETER_OK)
            {
                fprintf(stderr, "  production typed variadic status %u, function %u, entry %u\n", typed.status, typed.function, typed.entry);
            }
        }
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t encoder = 0; encoder < 2; encoder += 1)
            {
                Tier0VfImage* image = &native.images[path][encoder];
                if (image->bytes != NULL && image->bytes != MAP_FAILED)
                {
                    munmap(image->bytes, image->mapping_bytes);
                }
            }
        }
        arena_pop_to(&arena, 0);
        arena_pop_to(&output, 0);
        arena_pop_to(&twin_output, 0);
    }
    ir_lower_test_expect(tally, compared == BUSTER_ARRAY_LENGTH(kinds), "every variadic callee runs natively and interpreted", compared, 0);
    printf("IR lowering variadic frames: %u of Tier 0's variadic callees, native and shared IR alike\n", compared);
    if (page != MAP_FAILED)
    {
        munmap(page, BUSTER_TIER0_VF_PAGE);
    }
    tier0_tables_close(&tables);
    free(interpreted);
    free(stream);
    arena_release(&execution);
    arena_release(&scratch);
    arena_release(&twin_output);
    arena_release(&output);
    arena_release(&arena);
}

#include "ir_simd_lower_test.c"

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
    else if (!(x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize()))
    {
        // Tier 0's tables, for the generated programs' native code.
        fprintf(stderr, "test-ir-lowering: the tables can't be built\n");
    }
    else
    {
        IrLowerTestTally tally =
        {
            0
        };
        ir_simd_lower_tests(&tally);
        ir_lower_test_fixtures(&tally);
        ir_lower_test_sources(&tally);
        ir_lower_test_region_lifetimes(&tally);
        ir_lower_test_cas_parameter(&tally);
        ir_lower_test_failures(&tally);
        ir_lower_test_local_roles(&tally);
        ir_lower_test_aggregates(&tally);
        ir_lower_test_ignored_scan(&tally);
        ir_lower_test_chain(&tally);
        ir_lower_test_empty_stack(&tally);
        ir_lower_test_narrow(&tally);
        ir_lower_test_objects(&tally);
        ir_lower_test_copies(&tally);
        ir_lower_test_stack(&tally);
        ir_lower_test_indirect_jump(&tally);
        ir_lower_test_hoisted_symbols(&tally);
        ir_lower_test_hoisted_places(&tally);
        ir_lower_test_depth_types(&tally);
        ir_lower_test_guarded(&tally);
        const Tier0ProgramOptions values =
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
        Tier0ProgramOptions objects = values;
        objects.objects = true;
        // Calls through pointers to functions: locals that hold a function's address, and the address itself.
        Tier0ProgramOptions indirect = values;
        indirect.indirect = true;
        ir_lower_test_programs(&tally, seed, count, values, false, "values");
        ir_lower_test_programs(&tally, seed, count, objects, false, "objects");
        ir_lower_test_programs(&tally, seed, count, indirect, false, "indirect calls");
        // Structs and unions passed and returned by value: the IR's ABI records, its aggregate parameters' copies and
        // its results' destinations, run against the typed interpreter and Tier 0's native code.
        ir_lower_test_programs(&tally, seed, count, objects, true, "records");
        Tier0ProgramOptions plain = objects;
        plain.floating = false;
        ir_lower_test_programs(&tally, seed, count, plain, true, "records (shared IR / Tier 0 native)");
        ir_lower_test_variadic_frames(&tally);
        // Strict execution keeps undefined-byte checking enabled. Full representation COPY can include uninitialized
        // struct padding, and the older generator's FP->integer cases include the documented CVTT oracle boundary.
        // These are independent acceptance gaps, not grounds to weaken the dynamic storage checks.
        Tier0ProgramOptions dynamic = values;
        dynamic.vlas = true;
        dynamic.floating = false;
        ir_lower_test_programs(&tally, seed, count, dynamic, false, "dynamic stack (strict shared IR / Tier 0 native)");
        ir_lower_test_wide(&tally, seed, 4 * count);
        printf("test-ir-lowering: %" PRIu64 " checks, %" PRIu64 " failed\n", tally.checks, tally.failures);
        result = tally.failures != 0;
    }
    return result;
}
