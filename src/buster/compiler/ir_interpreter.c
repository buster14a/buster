// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Scalar shared-IR execution oracle. Include after ir_validate.c, interpreter.c and interpreter_native.c. Values preserve
// exact two-word payloads; frames, slots, phi snapshots and arguments live in the caller's arena. Calls never use
// the host call stack. All phis read the predecessor state before any destination phi is written. Native calls are
// the explicit host boundary. No execution-speed claim is made for this deliberately scalar reference consumer.
// Execution verifies whole-function structure and reachable SSA dominance, then diagnoses undefined dynamic uses.
// Unreachable uses impose no cross-block dominance requirement. Checked memory honors promised alignment; volatile transfers are ordered
// byte accesses, including exactly ten bytes for f80. Input/program views and native extension slices outlive execution.
// Layout: integer/float payload helpers; symbol lookup; frame preparation; scalar operations; ir_interpret.


typedef struct IrInterpreterFunction IrInterpreterFunction;
struct IrInterpreterFunction
{
    SymbolId symbol;
    const IrFunction* function;
};

typedef struct IrInterpreterProgram IrInterpreterProgram;
struct IrInterpreterProgram
{
    const IrInterpreterFunction* functions;
    uint32_t function_count;
    const InterpreterBinding* bindings;
    uint32_t binding_count;
    const InterpreterNativeBinding* natives;
    uint32_t native_count;
    // Optional ABI extension slices, one per native binding. Signless IR i8/i16 types cannot encode a C
    // prototype's signedness. Each supplied slice covers the actual arguments and uses AbiExtend values.
    const uint8_t* const* native_extensions;
};

typedef struct IrInterpreterFrame IrInterpreterFrame;
struct IrInterpreterFrame
{
    const IrFunction* function;
    InterpreterValue* values;
    uint8_t* defined;
    InterpreterValue* arguments;
    uint64_t* slots;
    InterpreterValue* phi_values;
    uint8_t* phi_defined;
    uint64_t arena_mark;
    uint32_t instruction;
    uint32_t block;
    uint32_t destination;
    uint32_t function_index;
    uint32_t value_charge;
};

static unsigned __int128 ir_interpreter_integer(InterpreterValue value)
{
    return ((unsigned __int128)value.high << 64) | value.low;
}

static unsigned __int128 ir_interpreter_mask(uint8_t type)
{
    const uint32_t bits = ir_type_bits[type];
    return bits == 128 ? ~(unsigned __int128)0 : (((unsigned __int128)1 << bits) - 1);
}

static InterpreterValue ir_interpreter_value(unsigned __int128 bits, uint8_t type)
{
    bits &= ir_interpreter_mask(type);
    const InterpreterValue result = {
        .low = (uint64_t)bits,
        .high = (uint64_t)(bits >> 64),
    };
    return result;
}

static __int128 ir_interpreter_signed(InterpreterValue value, uint8_t type)
{
    const uint32_t bits = ir_type_bits[type];
    unsigned __int128 raw = ir_interpreter_integer(value) & ir_interpreter_mask(type);
    if (bits < 128 && (raw & ((unsigned __int128)1 << (bits - 1))) != 0)
    {
        raw |= ~ir_interpreter_mask(type);
    }
    __int128 result = 0;
    memcpy(&result, &raw, sizeof(result));
    return result;
}

static long double ir_interpreter_float(InterpreterValue value, uint8_t type)
{
    long double result = 0;
    if (type == IR_F32)
    {
        const uint32_t raw = (uint32_t)value.low;
        float number = 0;
        memcpy(&number, &raw, sizeof(number));
        result = number;
    }
    else if (type == IR_F64)
    {
        double number = 0;
        memcpy(&number, &value.low, sizeof(number));
        result = number;
    }
    else
    {
        // x86-64 long double is the x87 80-bit format plus six padding bytes.
        uint8_t raw[sizeof(long double)] = { 0 };
        memcpy(raw, &value.low, 8);
        memcpy(raw + 8, &value.high, 2);
        memcpy(&result, raw, sizeof(result));
    }
    return result;
}

static InterpreterValue ir_interpreter_float_value(long double number, uint8_t type)
{
    InterpreterValue result = { 0 };
    if (type == IR_F32)
    {
        const float narrowed = (float)number;
        uint32_t raw = 0;
        memcpy(&raw, &narrowed, sizeof(raw));
        result.low = raw;
    }
    else if (type == IR_F64)
    {
        const double narrowed = (double)number;
        memcpy(&result.low, &narrowed, sizeof(narrowed));
    }
    else
    {
        uint8_t raw[sizeof(long double)];
        memcpy(raw, &number, sizeof(raw));
        memcpy(&result.low, raw, 8);
        memcpy(&result.high, raw + 8, 2);
    }
    return result;
}

// Decode a finite x87 value after exact F32/F64 extension. Its explicit significand permits truncation and
// range checks entirely in unsigned integers, including i128 boundaries, without a host out-of-range cast.
static InterpreterStatus ir_interpreter_float_integer(InterpreterValue value, uint8_t source_type, uint8_t result_type, bool is_signed, unsigned __int128* result)
{
    const InterpreterValue extended = ir_interpreter_float_value(ir_interpreter_float(value, source_type), IR_F80);
    const uint32_t encoded_exponent = (uint32_t)extended.high & 0x7fffu;
    const bool negative = (extended.high & 0x8000u) != 0;
    const int32_t exponent = (int32_t)encoded_exponent - 16383;
    const uint32_t bits = ir_type_bits[result_type];
    InterpreterStatus status = INTERPRETER_OK;
    unsigned __int128 magnitude = 0;
    if (encoded_exponent == 0x7fff || exponent >= 128)
    {
        status = INTERPRETER_ARITHMETIC;
    }
    else if (exponent >= 63)
    {
        magnitude = (unsigned __int128)extended.low << (uint32_t)(exponent - 63);
    }
    else if (exponent >= 0)
    {
        magnitude = extended.low >> (uint32_t)(63 - exponent);
    }
    if (status == INTERPRETER_OK)
    {
        const unsigned __int128 signed_limit = (unsigned __int128)1 << (bits - 1);
        if (is_signed ? (negative ? magnitude > signed_limit : magnitude >= signed_limit) : (negative && magnitude != 0) || (bits < 128 && magnitude > ir_interpreter_mask(result_type)))
        {
            status = INTERPRETER_ARITHMETIC;
        }
        else
        {
            *result = negative ? 0 - magnitude : magnitude;
        }
    }
    return status;
}

static uint32_t ir_interpreter_find_function(const IrInterpreterProgram* program, SymbolId symbol, uint64_t address, bool indirect)
{
    uint32_t result = UINT32_MAX;
    for (uint32_t f = 0; f < program->function_count && result == UINT32_MAX; f += 1)
    {
        const IrInterpreterFunction* binding = &program->functions[f];
        if (indirect ? address == (uintptr_t)binding->function : symbol == binding->symbol)
        {
            result = f;
        }
    }
    return result;
}

static const InterpreterNativeBinding* ir_interpreter_find_native(const IrInterpreterProgram* program, SymbolId symbol, uint64_t address, bool indirect)
{
    const InterpreterNativeBinding* result = 0;
    for (uint32_t n = 0; n < program->native_count && result == 0; n += 1)
    {
        const InterpreterNativeBinding* binding = &program->natives[n];
        if (indirect ? address == (uintptr_t)binding->address : symbol == binding->symbol)
        {
            result = binding;
        }
    }
    return result;
}

static bool ir_interpreter_symbol_address(const IrInterpreterProgram* program, SymbolId symbol, InterpreterValue* value)
{
    const uint32_t function = ir_interpreter_find_function(program, symbol, 0, false);
    const InterpreterNativeBinding* native = ir_interpreter_find_native(program, symbol, 0, false);
    bool found = false;
    if (function != UINT32_MAX)
    {
        *value = (InterpreterValue){ .low = (uintptr_t)program->functions[function].function };
        found = true;
    }
    else if (native != 0)
    {
        *value = (InterpreterValue){ .low = (uintptr_t)native->address };
        found = true;
    }
    else
    {
        for (uint32_t b = 0; b < program->binding_count && !found; b += 1)
        {
            if (program->bindings[b].symbol == symbol)
            {
                *value = (InterpreterValue){ .low = (uintptr_t)program->bindings[b].bytes };
                found = true;
            }
        }
    }
    return found;
}

static InterpreterStatus ir_interpreter_frame(IrInterpreterFrame* frame, const IrInterpreterProgram* program, uint32_t function_index, const InterpreterValue* arguments, uint32_t count, uint32_t destination, uint32_t owner, const InterpreterOptions* options, uint64_t active_values, uint64_t active_locals, InterpreterRuntime* runtime, Arena* arena)
{
    const IrFunction* function = program->functions[function_index].function;
    const uint64_t mark = arena->length;
    InterpreterStatus status = INTERPRETER_OK;
    IrCheck verified = { .error = IR_ERROR_STRUCTURE };
    if (function != 0)
    {
        verified = ir_verify_complete(function, arena, false);
    }
    if (verified.error == IR_ERROR_MEMORY)
    {
        status = INTERPRETER_RESOURCE;
    }
    else if (verified.error != IR_OK || function->parameter_count != count || (count != 0 && arguments == 0))
    {
        status = INTERPRETER_INVALID;
    }
    else if ((uint64_t)function->instruction_count + count > UINT32_MAX || active_values + function->instruction_count + count > options->max_values)
    {
        status = INTERPRETER_VALUE_LIMIT;
    }
    else if (active_locals + function->slot_count > options->max_locals)
    {
        status = INTERPRETER_RESOURCE;
    }
    if (status == INTERPRETER_OK)
    {
        *frame = (IrInterpreterFrame){
            .function = function,
            .arena_mark = mark,
            .destination = destination,
            .function_index = function_index,
            .value_charge = function->instruction_count + count,
        };
        frame->values = BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterValue, (uint64_t)function->instruction_count + 1);
        frame->defined = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)function->instruction_count + 1);
        frame->arguments = BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterValue, (uint64_t)count + 1);
        frame->slots = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)function->slot_count + 1);
        frame->phi_values = BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterValue, (uint64_t)function->instruction_count + 1);
        frame->phi_defined = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)function->instruction_count + 1);
        if (frame->values == 0 || frame->defined == 0 || frame->arguments == 0 || frame->slots == 0 || frame->phi_values == 0 || frame->phi_defined == 0)
        {
            status = INTERPRETER_RESOURCE;
        }
        else
        {
            memset(frame->values, 0, ((uint64_t)function->instruction_count + 1) * sizeof(InterpreterValue));
            memset(frame->defined, 0, (uint64_t)function->instruction_count + 1);
            for (uint32_t p = 0; p < count; p += 1)
            {
                frame->arguments[p] = ir_interpreter_value(ir_interpreter_integer(arguments[p]), function->parameter_types[p]);
            }
        }
        for (uint32_t s = 0; s < function->slot_count && status == INTERPRETER_OK; s += 1)
        {
            const IrSlot slot = function->slots[s];
            uint8_t* bytes = arena_push(arena, slot.size, slot.alignment);
            uint8_t* defined = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, slot.size);
            if (bytes == 0 || defined == 0)
            {
                status = INTERPRETER_RESOURCE;
            }
            else
            {
                memset(bytes, 0, slot.size);
                memset(defined, 0, slot.size);
                frame->slots[s] = (uintptr_t)bytes;
                const bool added = interpreter_memory_add(runtime, bytes, slot.size, defined, true, owner);
                if (!added)
                {
                    status = INTERPRETER_RESOURCE;
                }
            }
        }
    }
    if (status != INTERPRETER_OK)
    {
        interpreter_memory_remove_owner(runtime, owner);
        arena_pop_to(arena, mark);
    }
    return status;
}

static InterpreterStatus ir_interpreter_enter_block(IrInterpreterFrame* frame, uint32_t block, uint32_t predecessor, const InterpreterOptions* options, uint64_t* steps)
{
    const IrFunction* function = frame->function;
    const uint32_t begin = function->block_offsets[block];
    const uint32_t end = function->block_offsets[block + 1];
    uint32_t phi_end = begin;
    InterpreterStatus status = INTERPRETER_OK;
    while (phi_end < end && function->op[phi_end] == IR_PHI && status == INTERPRETER_OK)
    {
        if (*steps == options->max_steps)
        {
            status = INTERPRETER_STEP_LIMIT;
        }
        else
        {
            *steps += 1;
            const uint32_t first = function->a[phi_end];
            const uint32_t count = function->b[phi_end];
            uint32_t source = BUSTER_IR_NONE;
            for (uint32_t p = 0; p < count; p += 1)
            {
                if (function->extra[first + p * 2] == predecessor)
                {
                    source = function->extra[first + p * 2 + 1];
                }
            }
            if (source == BUSTER_IR_NONE)
            {
                status = INTERPRETER_INVALID;
            }
            else
            {
                frame->phi_values[phi_end] = frame->values[source];
                frame->phi_defined[phi_end] = frame->defined[source];
            }
            phi_end += 1;
        }
    }
    if (status == INTERPRETER_OK)
    {
        for (uint32_t p = begin; p < phi_end; p += 1)
        {
            frame->values[p] = frame->phi_values[p];
            frame->defined[p] = frame->phi_defined[p];
        }
        frame->block = block;
        frame->instruction = phi_end;
    }
    return status;
}

static InterpreterStatus ir_interpreter_operation(const IrFunction* function, uint32_t instruction, InterpreterValue av, InterpreterValue bv, InterpreterValue* value)
{
    const uint8_t op = function->op[instruction];
    const uint8_t type = function->type[instruction];
    const uint8_t at = function->a[instruction] < function->instruction_count ? function->type[function->a[instruction]] : IR_VOID;
    const uint32_t bits = ir_type_bits[type];
    const unsigned __int128 left = ir_interpreter_integer(av);
    const unsigned __int128 right = ir_interpreter_integer(bv);
    unsigned __int128 result = 0;
    bool raw_result = true;
    InterpreterStatus status = INTERPRETER_OK;
    switch (op)
    {
    case IR_COPY:
    case IR_BITCAST:
    case IR_TRUNC:
    case IR_ZEXT:
    case IR_INTTOPTR:
    case IR_PTRTOINT:
        result = left;
        break;
    case IR_SEXT:
        result = (unsigned __int128)ir_interpreter_signed(av, at);
        break;
    case IR_ADD: result = left + right; break;
    case IR_SUB: result = left - right; break;
    case IR_MUL: result = left * right; break;
    case IR_AND: result = left & right; break;
    case IR_OR: result = left | right; break;
    case IR_XOR: result = left ^ right; break;
    case IR_NEG: result = 0 - left; break;
    case IR_NOT: result = ~left; break;
    case IR_UDIV:
    case IR_UREM:
        if (right == 0)
        {
            status = INTERPRETER_ARITHMETIC;
        }
        else
        {
            result = op == IR_UDIV ? left / right : left % right;
        }
        break;
    case IR_SDIV:
    case IR_SREM:
    {
        const __int128 signed_left = ir_interpreter_signed(av, type);
        const __int128 signed_right = ir_interpreter_signed(bv, type);
        const unsigned __int128 minimum = (unsigned __int128)1 << (bits - 1);
        if (signed_right == 0 || (left == minimum && signed_right == -1))
        {
            status = INTERPRETER_ARITHMETIC;
        }
        else
        {
            result = (unsigned __int128)(op == IR_SDIV ? signed_left / signed_right : signed_left % signed_right);
        }
        break;
    }
    case IR_SHL:
    case IR_LSHR:
    case IR_ASHR:
        if (right >= bits)
        {
            status = INTERPRETER_ARITHMETIC;
        }
        else if (op == IR_SHL)
        {
            result = left << (uint32_t)right;
        }
        else if (op == IR_LSHR)
        {
            result = left >> (uint32_t)right;
        }
        else
        {
            result = (unsigned __int128)(ir_interpreter_signed(av, type) >> (uint32_t)right);
        }
        break;
    case IR_ICMP:
    {
        const __int128 signed_left = ir_interpreter_signed(av, at);
        const __int128 signed_right = ir_interpreter_signed(bv, at);
        switch (function->c[instruction])
        {
        case IR_EQ: result = left == right; break;
        case IR_NE: result = left != right; break;
        case IR_LT: result = signed_left < signed_right; break;
        case IR_LE: result = signed_left <= signed_right; break;
        case IR_GT: result = signed_left > signed_right; break;
        case IR_GE: result = signed_left >= signed_right; break;
        case IR_ULT: result = left < right; break;
        case IR_ULE: result = left <= right; break;
        case IR_UGT: result = left > right; break;
        case IR_UGE: result = left >= right; break;
        default: status = INTERPRETER_INVALID; break;
        }
        break;
    }
    case IR_FNEG:
        result = left ^ ((unsigned __int128)1 << (bits - 1));
        break;
    case IR_FADD:
    case IR_FSUB:
    case IR_FMUL:
    case IR_FDIV:
    {
        const long double a = ir_interpreter_float(av, type);
        const long double b = ir_interpreter_float(bv, type);
        long double number = 0;
        if (type == IR_F32)
        {
            const float x = (float)a;
            const float y = (float)b;
            float z = 0;
            switch (op)
            {
            case IR_FADD: z = x + y; break;
            case IR_FSUB: z = x - y; break;
            case IR_FMUL: z = x * y; break;
            default: z = x / y; break;
            }
            number = z;
        }
        else if (type == IR_F64)
        {
            const double x = (double)a;
            const double y = (double)b;
            double z = 0;
            switch (op)
            {
            case IR_FADD: z = x + y; break;
            case IR_FSUB: z = x - y; break;
            case IR_FMUL: z = x * y; break;
            default: z = x / y; break;
            }
            number = z;
        }
        else
        {
            switch (op)
            {
            case IR_FADD: number = a + b; break;
            case IR_FSUB: number = a - b; break;
            case IR_FMUL: number = a * b; break;
            default: number = a / b; break;
            }
        }
        *value = ir_interpreter_float_value(number, type);
        raw_result = false;
        break;
    }
    case IR_FCMP:
    {
        const long double a = ir_interpreter_float(av, at);
        const long double b = ir_interpreter_float(bv, at);
        switch (function->c[instruction])
        {
        case IR_EQ: result = a == b; break;
        case IR_NE: result = a != b; break;
        case IR_LT: result = a < b; break;
        case IR_LE: result = a <= b; break;
        case IR_GT: result = a > b; break;
        case IR_GE: result = a >= b; break;
        default: status = INTERPRETER_INVALID; break;
        }
        break;
    }
    case IR_FTRUNC:
    case IR_FEXT:
        *value = ir_interpreter_float_value(ir_interpreter_float(av, at), type);
        raw_result = false;
        break;
    case IR_SITOFP:
    case IR_UITOFP:
    {
        // Cast directly to the destination precision: routing i128 through x87 first can double-round.
        const __int128 signed_value = ir_interpreter_signed(av, at);
        long double number = 0;
        if (type == IR_F32)
        {
            number = op == IR_SITOFP ? (float)signed_value : (float)left;
        }
        else if (type == IR_F64)
        {
            number = op == IR_SITOFP ? (double)signed_value : (double)left;
        }
        else
        {
            number = op == IR_SITOFP ? (long double)signed_value : (long double)left;
        }
        *value = ir_interpreter_float_value(number, type);
        raw_result = false;
        break;
    }
    case IR_FPTOSI:
    case IR_FPTOUI:
    {
        status = ir_interpreter_float_integer(av, at, type, op == IR_FPTOSI, &result);
        break;
    }
    default:
        status = INTERPRETER_INVALID;
        break;
    }
    if (status == INTERPRETER_OK && raw_result)
    {
        *value = ir_interpreter_value(result, type);
    }
    return status;
}

static __attribute__((unused)) InterpreterResult ir_interpret(const IrInterpreterProgram* program, SymbolId entry, const InterpreterValue* arguments, uint32_t argument_count, const InterpreterOptions* requested_options, Arena* arena)
{
    InterpreterResult result = {
        .status = INTERPRETER_INVALID,
        .function = UINT32_MAX,
        .entry = BUSTER_IR_NONE,
    };
    const InterpreterOptions defaults = interpreter_default_options();
    const InterpreterOptions* options = requested_options == 0 ? &defaults : requested_options;
    const uint64_t mark = arena->length;
    IrInterpreterFrame* frames = 0;
    InterpreterRuntime runtime = {
        .memory_capacity = options->max_memories,
        .check_memory = options->check_memory,
        .check_undefined = options->check_undefined,
    };
    uint32_t depth = 0;
    uint64_t active_values = 0;
    uint64_t active_locals = 0;
    if (program != 0 && (program->function_count == 0 || program->functions != 0) && (program->binding_count == 0 || program->bindings != 0) && (program->native_count == 0 || program->natives != 0))
    {
        frames = BUSTER_ARENA_PUSH_ARRAY(arena, IrInterpreterFrame, options->max_frames);
        runtime.memories = BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterMemory, options->max_memories);
        result.status = options->max_frames == 0 || options->max_frames == UINT32_MAX ? INTERPRETER_FRAME_LIMIT : frames == 0 || runtime.memories == 0 ? INTERPRETER_RESOURCE : INTERPRETER_OK;
    }
    for (uint32_t b = 0; result.status == INTERPRETER_OK && b < program->binding_count; b += 1)
    {
        const InterpreterBinding binding = program->bindings[b];
        const bool added = interpreter_memory_add(&runtime, binding.bytes, binding.size, 0, binding.writable, 0);
        if (!added)
        {
            result.status = INTERPRETER_RESOURCE;
        }
    }
    if (result.status == INTERPRETER_OK)
    {
        const uint32_t first = ir_interpreter_find_function(program, entry, 0, false);
        if (first == UINT32_MAX)
        {
            result.status = INTERPRETER_SYMBOL;
        }
        else
        {
            result.function = first;
            result.status = ir_interpreter_frame(&frames[0], program, first, arguments, argument_count, BUSTER_IR_NONE, 1, options, 0, 0, &runtime, arena);
            if (result.status == INTERPRETER_OK)
            {
                depth = 1;
                active_values = frames[0].value_charge;
                active_locals = frames[0].function->slot_count;
            }
        }
    }
    while (result.status == INTERPRETER_OK && depth != 0)
    {
        IrInterpreterFrame* frame = &frames[depth - 1];
        const IrFunction* function = frame->function;
        const uint32_t i = frame->instruction;
        result.function = frame->function_index;
        result.entry = i;
        if (function->instruction_count == 0)
        {
            result.value = (InterpreterValue){ 0 };
            const uint32_t destination = frame->destination;
            const uint64_t frame_mark = frame->arena_mark;
            active_values -= frame->value_charge;
            active_locals -= function->slot_count;
            interpreter_memory_remove_owner(&runtime, depth);
            depth -= 1;
            arena_pop_to(arena, frame_mark);
            if (depth != 0)
            {
                frames[depth - 1].values[destination] = result.value;
                frames[depth - 1].defined[destination] = 1;
            }
        }
        else if (result.steps == options->max_steps)
        {
            result.status = INTERPRETER_STEP_LIMIT;
        }
        else if (i >= function->instruction_count)
        {
            result.status = INTERPRETER_INVALID;
        }
        else
        {
            result.steps += 1;
            frame->instruction += 1;
            const uint8_t op = function->op[i];
            const uint8_t type = function->type[i];
            const uint32_t a = function->a[i];
            const uint32_t b = function->b[i];
            const uint32_t c = function->c[i];
            const InterpreterValue av = a < function->instruction_count ? frame->values[a] : (InterpreterValue){ 0 };
            const InterpreterValue bv = b < function->instruction_count ? frame->values[b] : (InterpreterValue){ 0 };
            InterpreterValue value = { 0 };
            bool store_value = type != IR_VOID;
            bool defined = true;
            // The operand table describes value operands, not metadata. Select and phi consume only their chosen
            // incoming value; dead undef alternatives remain legal. COPY can carry undef until a semantic use.
            uint8_t operands = ir_operand_table[op];
            if (op == IR_RET && a != BUSTER_IR_NONE)
            {
                operands = 1;
            }
            else if (op == IR_SELECT)
            {
                operands = 1;
            }
            else if (op == IR_COPY || op == IR_BITCAST)
            {
                operands = 0;
            }
            for (uint32_t p = 0; result.status == INTERPRETER_OK && p < 3; p += 1)
            {
                const uint32_t id = p == 0 ? a : p == 1 ? b : c;
                if ((operands & (1u << p)) != 0 && frame->defined[id] == 0 && options->check_undefined)
                {
                    result.status = INTERPRETER_UNDEFINED;
                }
            }
            if (op == IR_ADDRESS && b != BUSTER_IR_NONE && frame->defined[b] == 0 && options->check_undefined)
            {
                result.status = INTERPRETER_UNDEFINED;
            }
            if (result.status == INTERPRETER_OK)
            {
                switch (op)
                {
                case IR_CONST:
                    value.low = function->constants[a].low;
                    value.high = function->constants[a].high;
                    break;
                case IR_UNDEF:
                    defined = false;
                    break;
                case IR_PARAM:
                    value = frame->arguments[a];
                    break;
                case IR_COPY:
                case IR_BITCAST:
                    value = av;
                    defined = frame->defined[a] != 0;
                    break;
                case IR_SELECT:
                {
                    const uint32_t selected = av.low != 0 ? b : c;
                    value = frame->values[selected];
                    defined = frame->defined[selected] != 0;
                    break;
                }
                case IR_STACK_ADDR:
                    value.low = frame->slots[a];
                    break;
                case IR_SYMBOL_ADDR:
                    if (!ir_interpreter_symbol_address(program, a, &value))
                    {
                        result.status = INTERPRETER_SYMBOL;
                    }
                    break;
                case IR_ADDRESS:
                    value.low = av.low + (b == BUSTER_IR_NONE ? 0 : bv.low * function->addresses[c].scale) + (uint64_t)function->addresses[c].offset;
                    break;
                case IR_LOAD:
                case IR_STORE:
                {
                    const uint8_t memory_type = op == IR_LOAD ? type : function->type[b];
                    const uint32_t bytes = (ir_type_bits[memory_type] + 7) / 8;
                    result.status = interpreter_memory_access(&runtime, av.low, bytes, op == IR_STORE);
                    const uint64_t alignment_mask = (1ull << function->alignment_log2[i]) - 1;
                    if (result.status == INTERPRETER_OK && options->check_memory && (av.low & alignment_mask) != 0)
                    {
                        result.status = INTERPRETER_MEMORY;
                    }
                    if (result.status == INTERPRETER_OK)
                    {
                        if (op == IR_LOAD)
                        {
                            if ((function->flags[i] & BUSTER_IR_VOLATILE) != 0)
                            {
                                uint8_t* destination = (uint8_t*)&value;
                                const volatile uint8_t* source = (const volatile uint8_t*)(uintptr_t)av.low;
                                for (uint32_t byte = 0; byte < bytes; byte += 1)
                                {
                                    destination[byte] = source[byte];
                                }
                            }
                            else
                            {
                                memcpy(&value, (const void*)(uintptr_t)av.low, bytes);
                            }
                            value = ir_interpreter_value(ir_interpreter_integer(value), type);
                        }
                        else
                        {
                            if ((function->flags[i] & BUSTER_IR_VOLATILE) != 0)
                            {
                                volatile uint8_t* destination = (volatile uint8_t*)(uintptr_t)av.low;
                                const uint8_t* source = (const uint8_t*)&bv;
                                for (uint32_t byte = 0; byte < bytes; byte += 1)
                                {
                                    destination[byte] = source[byte];
                                }
                            }
                            else
                            {
                                memcpy((void*)(uintptr_t)av.low, &bv, bytes);
                            }
                        }
                    }
                    break;
                }
                case IR_CALL:
                case IR_CALL_INDIRECT:
                {
                    const IrCall call = function->calls[b];
                    const uint32_t callee = ir_interpreter_find_function(program, a, av.low, op == IR_CALL_INDIRECT);
                    const InterpreterNativeBinding* native = callee == UINT32_MAX ? ir_interpreter_find_native(program, a, av.low, op == IR_CALL_INDIRECT) : 0;
                    const uint64_t call_mark = arena->length;
                    bool pushed_frame = false;
                    InterpreterValue* passed = BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterValue, (uint64_t)call.argument_count + 1);
                    uint16_t* types = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, (uint64_t)call.argument_count + 1);
                    if (passed == 0 || types == 0)
                    {
                        result.status = INTERPRETER_RESOURCE;
                    }
                    for (uint32_t p = 0; result.status == INTERPRETER_OK && p < call.argument_count; p += 1)
                    {
                        const uint32_t id = function->extra[call.argument_first + p];
                        passed[p] = frame->values[id];
                        types[p] = function->type[id];
                        if (frame->defined[id] == 0 && options->check_undefined)
                        {
                            result.status = INTERPRETER_UNDEFINED;
                        }
                    }
                    if (result.status == INTERPRETER_OK && callee != UINT32_MAX)
                    {
                        const IrFunction* target = program->functions[callee].function;
                        IrCheck target_check = { .error = IR_ERROR_STRUCTURE };
                        if (target != 0)
                        {
                            target_check = ir_verify_complete(target, arena, false);
                        }
                        if (target_check.error == IR_ERROR_MEMORY)
                        {
                            result.status = INTERPRETER_RESOURCE;
                        }
                        else if (target_check.error != IR_OK || call.variadic || call.result_type != target->result_type || call.argument_count != target->parameter_count)
                        {
                            result.status = INTERPRETER_INVALID;
                        }
                        for (uint32_t p = 0; result.status == INTERPRETER_OK && p < call.argument_count; p += 1)
                        {
                            if (types[p] != target->parameter_types[p])
                            {
                                result.status = INTERPRETER_INVALID;
                            }
                        }
                        if (result.status == INTERPRETER_OK && depth == options->max_frames)
                        {
                            result.status = INTERPRETER_FRAME_LIMIT;
                        }
                        if (result.status == INTERPRETER_OK)
                        {
                            result.status = ir_interpreter_frame(&frames[depth], program, callee, passed, call.argument_count, i, depth + 1, options, active_values, active_locals, &runtime, arena);
                            if (result.status == INTERPRETER_OK)
                            {
                                // The new frame owns its copied arguments and the staging storage preceding them.
                                frames[depth].arena_mark = call_mark;
                                active_values += frames[depth].value_charge;
                                active_locals += frames[depth].function->slot_count;
                                depth += 1;
                                pushed_frame = true;
                                store_value = false;
                            }
                        }
                    }
                    else if (result.status == INTERPRETER_OK && native != 0)
                    {
                        const InterpreterNativeRequest request = {
                            .binding = native,
                            .types = types,
                            .extensions = program->native_extensions == 0 ? 0 : program->native_extensions[native - program->natives],
                            .arguments = passed,
                            .argument_count = call.argument_count,
                            .named_count = call.parameter_count,
                            .result_type = call.result_type,
                            .variadic = call.variadic,
                            .runtime = &runtime,
                        };
                        const bool invoked = interpreter_native_invoke(&request, &value);
                        if (!invoked)
                        {
                            result.status = INTERPRETER_NATIVE;
                        }
                    }
                    else if (result.status == INTERPRETER_OK)
                    {
                        result.status = INTERPRETER_SYMBOL;
                    }
                    if (!pushed_frame)
                    {
                        arena_pop_to(arena, call_mark);
                    }
                    break;
                }
                case IR_BR:
                case IR_CBR:
                case IR_SWITCH:
                {
                    uint32_t target = a;
                    if (op == IR_CBR)
                    {
                        target = av.low != 0 ? b : c;
                    }
                    else if (op == IR_SWITCH)
                    {
                        target = function->extra[b];
                        bool matched = false;
                        for (uint32_t p = 0; p < c && !matched; p += 1)
                        {
                            const IrConstant candidate = function->constants[function->extra[b + p * 2 + 1]];
                            matched = av.low == candidate.low && av.high == candidate.high;
                            if (matched)
                            {
                                target = function->extra[b + p * 2 + 2];
                            }
                        }
                    }
                    result.status = ir_interpreter_enter_block(frame, target, frame->block, options, &result.steps);
                    break;
                }
                case IR_RET:
                {
                    value = a == BUSTER_IR_NONE ? (InterpreterValue){ 0 } : av;
                    const uint32_t destination = frame->destination;
                    const uint64_t frame_mark = frame->arena_mark;
                    active_values -= frame->value_charge;
                    active_locals -= function->slot_count;
                    interpreter_memory_remove_owner(&runtime, depth);
                    depth -= 1;
                    arena_pop_to(arena, frame_mark);
                    if (depth == 0)
                    {
                        result.value = value;
                    }
                    else
                    {
                        frames[depth - 1].values[destination] = value;
                        frames[depth - 1].defined[destination] = 1;
                    }
                    store_value = false;
                    break;
                }
                case IR_UNREACHABLE:
                    result.status = INTERPRETER_UNDEFINED;
                    break;
                default:
                    result.status = ir_interpreter_operation(function, i, av, bv, &value);
                    break;
                }
            }
            if (result.status == INTERPRETER_OK && store_value)
            {
                frame->values[i] = ir_interpreter_value(ir_interpreter_integer(value), type);
                frame->defined[i] = defined;
            }
        }
    }
    interpreter_native_release(&runtime);
    arena_pop_to(arena, mark);
    return result;
}
