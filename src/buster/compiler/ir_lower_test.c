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
    bool equal = a->instruction_count == b->instruction_count && a->block_count == b->block_count && a->slot_count == b->slot_count && a->constant_count == b->constant_count &&
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
        for (uint64_t p = 0; check.error == IR_OK && p < 2; p += 1)
        {
            bool fault = false;
            const uint64_t actual = ir_lower_test_interpret(&promoted_function, 1, 0, 0, &data, globals, &p, 1, &execution, &fault);
            ir_lower_test_expect(tally, !fault && actual == (p != 0 ? 10 : 20), "the promoted computed goto goes to the same labels", 0, (uint32_t)p);
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
    for (uint32_t mixed = 0; ready && mixed < 2; mixed += 1)
    {
        Tier0Function tabled = twice;
        tabled.label_tables = mixed != 0 ? one_table : two_tables;
        tabled.table_jumps = mixed != 0 ? table_jumps + 1 : table_jumps;
        tabled.table_count = mixed != 0 ? 1 : 2;
        ir_lower_test_expect(tally, tier0_validate(&tabled).error == TIER0_OK, "Tier 0 takes the jump tables' columns", 0, 3 + mixed);
        if (ir_lower_test_lower(tally, &tabled, IR_I32, 4, &output, &twin_output, &scratch, &function, 0, 3 + mixed))
        {
            // Each br_indirect lists one label, its one successor: label 0 the goto *'s (mixed) or table 0's, label 1
            // the last table's.
            uint32_t jumps = 0;
            uint32_t listed = 0;
            uint32_t dispatches = 0;
            for (uint32_t i = 0; i < function.instruction_count; i += 1)
            {
                if (function.op[i] == IR_BR_INDIRECT && function.b[i] != BUSTER_IR_NONE && function.c[i] == 1 && function.extra[function.b[i]] < 2)
                {
                    const uint32_t label = function.extra[function.b[i]];
                    listed |= ir_successor_count(&function, i) == 1 && ir_successor(&function, i, 0) == function.labels[label] ? 1u << label : 0;
                }
                jumps += function.op[i] == IR_BR_INDIRECT;
                dispatches += function.op[i] == IR_BR && function.a[i] == function.block_count - 1;
            }
            ir_lower_test_expect(tally, jumps == 2 && listed == 3 && dispatches == mixed, "a jump table's jump is a br_indirect of its own table's labels", 0, 3 + mixed);
            for (uint64_t p = 0; p < 2; p += 1)
            {
                bool fault = false;
                const uint64_t actual = ir_lower_test_interpret(&function, 1, 0, 0, &data, globals, &p, 1, &execution, &fault);
                ir_lower_test_expect(tally, !fault && actual == (p != 0 ? 10 : 20), "jump tables' jumps go to their labels", 0, 3 + mixed);
            }
            ir_lower_test_promote(tally, &function, &promoted, &twin_promoted, &scratch, 0, 3 + mixed);
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
            ir_lower_test_expect(tally, check.error == IR_OK && phis == 0, "jump tables' jumps need no phi of their addresses", 0, 3 + mixed);
        }
        arena_pop_to(&output, 0);
        arena_pop_to(&twin_output, 0);
        arena_pop_to(&promoted, 0);
        arena_pop_to(&twin_promoted, 0);
    }
    // #148's frame address, which the adapter doesn't lower: refused at its entry, as X86 rows are, before anything is
    // allocated (from nonzero marks, so that a rollback to zero would show).
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
    for (uint32_t vector = 0; marked && vector < 2; vector += 1)
    {
        const uint64_t output_mark = output.length;
        const uint64_t scratch_mark = scratch.length;
        IrFunction result = preserved;
        const IrCheck check = ir_lower_typed_path(&frame, IR_VOID, 1, &output, &scratch, &result, vector != 0);
        ir_lower_test_expect(tally, tier0_validate(&frame).error == TIER0_OK && check.error == IR_ERROR_OPCODE && check.instruction == 0 &&
                                        output.length == output_mark && scratch.length == scratch_mark &&
                                        result.instruction_count == preserved.instruction_count && result.block_count == preserved.block_count,
                             "the frame address refused at its entry without allocation", 0, vector);
    }
    arena_release(&output);
    arena_release(&twin_output);
    arena_release(&promoted);
    arena_release(&twin_promoted);
    arena_release(&scratch);
    arena_release(&execution);
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
        ir_lower_test_fixtures(&tally);
        ir_lower_test_failures(&tally);
        ir_lower_test_local_roles(&tally);
        ir_lower_test_aggregates(&tally);
        ir_lower_test_objects(&tally);
        ir_lower_test_copies(&tally);
        ir_lower_test_stack(&tally);
        ir_lower_test_indirect_jump(&tally);
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
