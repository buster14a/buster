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


// A function and its symbol; `labels`, the producer's symbols of its labels in IrFunction.labels' order (label_count of
// them), or null for a function whose labels' addresses nothing takes. A label's address is its labels entry's, which
// only br_indirect reads. A call with by-value aggregates' ABI records (#131) passes each aggregate as a copy its callee
// owns, and a result through a hidden pointer or from the bytes its callee returns the address of; a variadic definition
// finds its anchors laid out from its call's arguments (#148). An entry with records of its own, or variadic, is refused.
typedef struct IrInterpreterFunction IrInterpreterFunction;
struct IrInterpreterFunction
{
    SymbolId symbol;
    const IrFunction* function;
    const SymbolId* labels;
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
    InterpreterStack dynamic;
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
        for (uint32_t f = 0; f < program->function_count && !found; f += 1)
        {
            const IrInterpreterFunction* binding = &program->functions[f];
            for (uint32_t k = 0; binding->labels != 0 && binding->function != 0 && k < binding->function->label_count && !found; k += 1)
            {
                if (binding->labels[k] == symbol)
                {
                    *value = (InterpreterValue){ .low = (uintptr_t)&binding->function->labels[k] };
                    found = true;
                }
            }
        }
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

// A variadic definition's incoming arguments (#148), named and unnamed, as its call passes them: each one's value (an
// aggregate's the address of its bytes, its place's size nonzero), IR type and place, the bytes of the call's argument
// area and its alignment, rsp's at the call (2^stack_alignment_log2, which a va_arg of an argument aligned past 16 rounds
// its address up to), and a result's hidden pointer, which rdi holds.
typedef struct IrInterpreterIncoming IrInterpreterIncoming;
struct IrInterpreterIncoming
{
    const InterpreterValue* values;
    const uint16_t* types;
    const IrPassed* places;
    uint32_t count;
    uint64_t stack_bytes;
    uint64_t hidden;
    uint8_t stack_alignment_log2;
    bool has_hidden;
};

// The most a call's argument area is aligned to (AbiCall.stack_alignment_log2), Tier 0's and Tier 1's bound: a call
// aligned past it is refused.
#define BUSTER_IR_INTERPRETER_MAX_STACK_ALIGNMENT_LOG2 30u

// An object representation's copy (#131's aggregates by value): `size` bytes from `source` to `destination` and each
// byte's definedness with them, both in registered memory where memory is checked, the destination writable.
static InterpreterStatus ir_interpreter_copy(InterpreterRuntime* runtime, uint64_t destination, uint64_t source, uint64_t size)
{
    InterpreterNativeRange from = {
        0
    };
    InterpreterNativeRange to = {
        0
    };
    const bool ranges = interpreter_native_range(runtime, source, size, false, &from) && interpreter_native_range(runtime, destination, size, true, &to);
    const InterpreterStatus status = ranges ? INTERPRETER_OK : INTERPRETER_MEMORY;
    if (ranges && size != 0)
    {
        memmove((void*)(uintptr_t)destination, (const void*)(uintptr_t)source, size);
        if (to.memory != 0 && to.memory->defined != 0)
        {
            if (from.memory != 0 && from.memory->defined != 0)
            {
                memmove(to.memory->defined + to.offset, from.memory->defined + from.offset, size);
            }
            else
            {
                memset(to.memory->defined + to.offset, 1, size);
            }
        }
    }
    return status;
}

// The anchors of a variadic definition's frame (#148) as its entry finds them: the register save area holding the
// argument registers (rdi to r9, then xmm0 to xmm7's low eightbytes), and the overflow area the call's argument area
// past the named arguments'; each argument's bytes where its place puts them (an aggregate's from its memory), and only
// those bytes defined. INTERPRETER_INVALID for a place outside them.
static InterpreterStatus ir_interpreter_anchors(const IrInterpreterIncoming* incoming, InterpreterRuntime* runtime, const InterpreterNativeArea* area)
{
    InterpreterStatus status = INTERPRETER_OK;
    if (incoming->has_hidden)
    {
        const uint8_t all[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
        interpreter_native_store(area->registers, area->registers_defined, (const uint8_t*)&incoming->hidden, all, sizeof(incoming->hidden));
    }
    for (uint32_t i = 0; status == INTERPRETER_OK && i < incoming->count; i += 1)
    {
        const IrPassed place = incoming->places[i];
        const uint16_t type = incoming->types[i];
        bool placed = false;
        if (place.size != 0)
        {
            InterpreterNativeRange range = {
                0
            };
            const uint64_t address = incoming->values[i].low;
            placed = interpreter_native_range(runtime, address, place.size, false, &range);
            const uint8_t* defined = range.memory != 0 && range.memory->defined != 0 ? range.memory->defined + range.offset : 0;
            placed = placed && interpreter_native_put(area, place.location, (const uint8_t*)(uintptr_t)address, defined, place.size);
            status = placed ? INTERPRETER_OK : INTERPRETER_MEMORY;
        }
        else
        {
            // A scalar's register or slot: its value's bytes defined, the rest of its 8 (16 for an i128 or a long double)
            // not, as a register's bits past a narrower value aren't.
            const uint32_t value_bytes = (ir_type_bits[type] + 7) / 8;
            uint8_t defined[16] = {
                0
            };
            memset(defined, 1, value_bytes);
            placed = interpreter_native_put(area, place.location, (const uint8_t*)&incoming->values[i], defined, type == IR_I128 || type == IR_F80 ? 16 : 8);
            status = placed ? INTERPRETER_OK : INTERPRETER_INVALID;
        }
    }
    return status;
}

static InterpreterStatus ir_interpreter_frame(IrInterpreterFrame* frame, const IrInterpreterProgram* program, uint32_t function_index, const InterpreterValue* arguments, uint32_t count, const IrInterpreterIncoming* incoming, uint32_t destination, uint32_t owner, const InterpreterOptions* options, uint64_t active_values, uint64_t active_locals, InterpreterRuntime* runtime, Arena* arena)
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
    else if (verified.error != IR_OK || function->parameter_count != count || (count != 0 && arguments == 0) || function->variadic != (incoming != 0) ||
             (incoming != 0 && incoming->stack_bytes < function->incoming.stack_bytes))
    {
        // A variadic definition's anchors take its call's arguments' places (#148); no other frame takes any.
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
        InterpreterNativeArea anchors = {
            0
        };
        for (uint32_t s = 0; s < function->slot_count && status == INTERPRETER_OK; s += 1)
        {
            // The overflow area: the call's argument area, aligned as rsp is at the call (an argument's offset there,
            // its alignment's multiple, is so its address's), its anchor past the named arguments' bytes, which the
            // frame doesn't hold.
            const IrSlot slot = function->slots[s];
            const bool save = (slot.flags & BUSTER_IR_SLOT_REGISTER_SAVE_AREA) != 0;
            const bool overflow = (slot.flags & BUSTER_IR_SLOT_OVERFLOW_AREA) != 0;
            const uint64_t size = overflow ? incoming->stack_bytes : slot.size;
            const uint64_t named = overflow ? function->incoming.stack_bytes : 0;
            uint8_t* bytes = arena_push(arena, size, overflow ? UINT64_C(1) << incoming->stack_alignment_log2 : slot.alignment);
            uint8_t* defined = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, size);
            if (bytes == 0 || defined == 0)
            {
                status = INTERPRETER_RESOURCE;
            }
            else
            {
                memset(bytes, 0, size);
                memset(defined, 0, size);
                frame->slots[s] = (uintptr_t)bytes + named;
                const bool added = interpreter_memory_add(runtime, bytes + named, size - named, defined + named, true, owner);
                if (!added)
                {
                    status = INTERPRETER_RESOURCE;
                }
                if (save)
                {
                    anchors.registers = bytes;
                    anchors.registers_defined = defined;
                }
                if (overflow)
                {
                    anchors.stack = bytes;
                    anchors.stack_defined = defined;
                    anchors.stack_capacity = size;
                }
            }
        }
        if (status == INTERPRETER_OK && incoming != 0)
        {
            // Anchors the function doesn't have take nothing: their bytes go to scratch the frame releases.
            uint8_t* scratch = arena_push(arena, (uint64_t)BUSTER_ABI_SAVE_AREA_BYTES * 2 + incoming->stack_bytes * 2, 16);
            status = scratch == 0 ? INTERPRETER_RESOURCE : INTERPRETER_OK;
            if (status == INTERPRETER_OK && anchors.registers == 0)
            {
                anchors.registers = scratch;
                anchors.registers_defined = scratch + BUSTER_ABI_SAVE_AREA_BYTES;
            }
            if (status == INTERPRETER_OK && anchors.stack == 0)
            {
                anchors.stack = scratch + BUSTER_ABI_SAVE_AREA_BYTES * 2;
                anchors.stack_defined = anchors.stack + incoming->stack_bytes;
                anchors.stack_capacity = incoming->stack_bytes;
            }
            if (status == INTERPRETER_OK)
            {
                status = ir_interpreter_anchors(incoming, runtime, &anchors);
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

// A call's arguments as its interpreted callee takes them (#131, #148), as both functions' records have them: a named
// aggregate's size alike in each (a callee's IGNOREd one, an empty record's, takes a fresh object of its own, whatever the
// call passes, as no bytes pass), and a result's kind; a hidden pointer, the call's destination, as the callee's parameter
// 0; a result in registers that the call has no room for dropped, as a call that doesn't use it drops it; each named
// aggregate as the address of a copy its frame owns (`owner`), aligned as its place in the call's argument area is; and
// for a variadic definition, which only takes the default argument promotions' types past its named parameters, every
// argument's place, its records' or worked out as LLVM's assignment places a scalar, whose register counts must be its
// incoming frame's, and the argument area's alignment. INTERPRETER_INVALID for a call its callee can't take, or whose
// argument area is aligned past BUSTER_IR_INTERPRETER_MAX_STACK_ALIGNMENT_LOG2.
static InterpreterStatus ir_interpreter_arguments(const IrFunction* function, IrCall call, const IrFunction* target, const InterpreterValue* passed, const uint16_t* types, uint64_t destination, uint32_t owner, InterpreterRuntime* runtime, Arena* arena, InterpreterValue* parameters, IrInterpreterIncoming* incoming)
{
    const IrAbiCall* records = call.abi != 0 ? &function->abi_calls[call.abi - 1] : 0;
    const uint32_t own = target->parameter_passed;
    const uint32_t result_size = records != 0 ? records->result_size : 0;
    const bool hidden = own != 0 && target->result_abi.kind == BUSTER_ABI_RESULT_MEMORY;
    const bool registers = own != 0 && target->result_size != 0;
    const uint32_t named = call.parameter_count;
    // The call's argument area's alignment, rsp's at the call: the records' (or 16 for a call without them, whose stack
    // arguments are scalars), or the callee's incoming frame's where that says more. A stack argument lies there at a
    // multiple of its alignment, which a copy of it must keep.
    const uint8_t call_alignment = records != 0 ? records->call.stack_alignment_log2 : (uint8_t)BUSTER_ABI_STACK_ALIGNMENT_LOG2;
    const uint8_t larger = call_alignment > target->incoming.stack_alignment_log2 ? call_alignment : target->incoming.stack_alignment_log2;
    const uint8_t alignment_log2 = larger > BUSTER_ABI_STACK_ALIGNMENT_LOG2 ? larger : (uint8_t)BUSTER_ABI_STACK_ALIGNMENT_LOG2;
    bool valid = call.variadic == target->variadic && target->parameter_count == named + hidden && !(hidden && registers) && alignment_log2 <= BUSTER_IR_INTERPRETER_MAX_STACK_ALIGNMENT_LOG2;
    valid = valid && (hidden || registers ? call.result_type == IR_VOID && (result_size != 0 || !hidden) && (result_size == 0 || records->call.result.kind == target->result_abi.kind) &&
                                                (!registers || target->result_type == IR_PTR)
                                          : result_size == 0 && call.result_type == target->result_type);
    valid = valid && (!hidden || target->parameter_types[0] == IR_I64 || target->parameter_types[0] == IR_PTR);
    for (uint32_t p = 0; valid && p < call.argument_count; p += 1)
    {
        const uint32_t size = records != 0 ? function->passed[records->passed + p].size : 0;
        const IrPassed parameter = p < named && own != 0 ? target->passed[own - 1 + hidden + p] : (IrPassed){ 0 };
        const uint16_t type = types[p];
        const bool ignored = parameter.size != 0 && parameter.location.kind == BUSTER_ABI_LOCATION_IGNORE;
        valid = p < named ? ignored || (type == target->parameter_types[hidden + p] && size == parameter.size) : size != 0 || (type != IR_F32 && (type < IR_I1 || type > IR_I16));
    }
    InterpreterStatus status = valid ? INTERPRETER_OK : INTERPRETER_INVALID;
    if (status == INTERPRETER_OK && hidden)
    {
        parameters[0] = (InterpreterValue){ .low = destination };
    }
    for (uint32_t p = 0; status == INTERPRETER_OK && p < named; p += 1)
    {
        const IrPassed parameter = own != 0 ? target->passed[own - 1 + hidden + p] : (IrPassed){ 0 };
        parameters[hidden + p] = passed[p];
        if (parameter.size != 0)
        {
            // The callee's copy of the call's bytes, aligned to 64 or as the call's argument area is (a register's
            // aggregate is 16 bytes at most, a stack argument's offset its alignment's multiple); an IGNOREd aggregate's
            // undefined.
            uint8_t* bytes = arena_push(arena, parameter.size, UINT64_C(1) << (alignment_log2 > 6 ? alignment_log2 : 6));
            uint8_t* defined = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, parameter.size);
            status = bytes == 0 || defined == 0 ? INTERPRETER_RESOURCE : INTERPRETER_OK;
            if (status == INTERPRETER_OK)
            {
                memset(bytes, 0, parameter.size);
                memset(defined, 0, parameter.size);
                status = interpreter_memory_add(runtime, bytes, parameter.size, defined, true, owner) ? INTERPRETER_OK : INTERPRETER_RESOURCE;
            }
            if (status == INTERPRETER_OK && parameter.location.kind != BUSTER_ABI_LOCATION_IGNORE)
            {
                status = ir_interpreter_copy(runtime, (uintptr_t)bytes, passed[p].low, parameter.size);
            }
            parameters[hidden + p] = (InterpreterValue){ .low = (uintptr_t)bytes };
        }
    }
    if (status == INTERPRETER_OK && target->variadic)
    {
        IrPassed* places = records != 0 ? 0 : BUSTER_ARENA_PUSH_ARRAY(arena, IrPassed, (uint64_t)call.argument_count + 1);
        uint32_t integers = 0;
        uint32_t vectors = 0;
        uint64_t stack_bytes = 0;
        uint8_t stack_alignment = BUSTER_ABI_STACK_ALIGNMENT_LOG2;
        status = records != 0 || places != 0 ? INTERPRETER_OK : INTERPRETER_RESOURCE;
        for (uint32_t p = 0; places != 0 && p < call.argument_count; p += 1)
        {
            places[p] = (IrPassed){ .location = interpreter_native_place(types[p], &integers, &vectors, &stack_bytes, &stack_alignment) };
        }
        *incoming = (IrInterpreterIncoming){
            .values = passed,
            .types = types,
            .places = records != 0 ? &function->passed[records->passed] : places,
            .count = call.argument_count,
            .stack_bytes = records != 0 ? records->call.stack_bytes : (stack_bytes + 7) & ~7ull,
            .hidden = destination,
            .stack_alignment_log2 = stack_alignment > alignment_log2 ? stack_alignment : alignment_log2,
            .has_hidden = hidden,
        };
        // The named arguments' registers are the incoming frame's, which va_start's offsets count, and every unnamed
        // argument on the stack lies past its stack bytes, in the overflow area.
        uint32_t named_integers = hidden;
        uint32_t named_vectors = 0;
        for (uint32_t p = 0; status == INTERPRETER_OK && p < call.argument_count; p += 1)
        {
            const AbiLocation location = incoming->places[p].location;
            const bool stacked = location.kind == BUSTER_ABI_LOCATION_STACK || location.kind == BUSTER_ABI_LOCATION_SPLIT;
            for (uint32_t r = 0; p < named && r < 2; r += 1)
            {
                const uint8_t reg = location.kind == BUSTER_ABI_LOCATION_REGISTERS || location.kind == BUSTER_ABI_LOCATION_SPLIT ? location.registers[r] : BUSTER_ABI_REGISTER_NONE;
                named_vectors += reg != BUSTER_ABI_REGISTER_NONE && reg >= BUSTER_ABI_REGISTER_VECTOR;
                named_integers += reg != BUSTER_ABI_REGISTER_NONE && reg < BUSTER_ABI_REGISTER_VECTOR;
            }
            status = p >= named && stacked && location.stack_offset < target->incoming.stack_bytes ? INTERPRETER_INVALID : INTERPRETER_OK;
        }
        status = status == INTERPRETER_OK && (named_integers != target->incoming.integer_registers || named_vectors != target->incoming.vector_registers || incoming->stack_bytes < target->incoming.stack_bytes) ? INTERPRETER_INVALID : status;
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
    if (program != 0 && interpreter_mappings_valid(program->functions, program->function_count, sizeof(IrInterpreterFunction), program->bindings, program->binding_count, program->natives, program->native_count))
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
            // An entry has no call whose records would place its aggregates (#131).
            const IrFunction* function = program->functions[first].function;
            result.status = function != 0 && function->parameter_passed != 0 ? INTERPRETER_INVALID
                                                                             : ir_interpreter_frame(&frames[0], program, first, arguments, argument_count, 0, BUSTER_IR_NONE, 1, options, 0, 0, &runtime, arena);
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
                case IR_ALLOCA:
                    result.status = interpreter_stack_allocate(&frame->dynamic, av.low, depth, &runtime, arena, &value.low);
                    break;
                case IR_STACK_SAVE:
                    result.status = interpreter_stack_save(&frame->dynamic, &runtime, arena, &value.low);
                    break;
                case IR_STACK_RESTORE:
                    result.status = interpreter_stack_restore(&frame->dynamic, av.low, &runtime, arena);
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
                    // A call with ABI records (#131): an aggregate result's destination is c.
                    const IrAbiCall* records = call.abi != 0 ? &function->abi_calls[call.abi - 1] : 0;
                    const uint32_t result_size = records != 0 ? records->result_size : 0;
                    const uint64_t destination = result_size != 0 ? frame->values[c].low : 0;
                    const uint64_t call_mark = arena->length;
                    bool pushed_frame = false;
                    InterpreterValue* passed = BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterValue, (uint64_t)call.argument_count + 1);
                    InterpreterValue* parameters = BUSTER_ARENA_PUSH_ARRAY(arena, InterpreterValue, (uint64_t)call.argument_count + 2);
                    uint16_t* types = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, (uint64_t)call.argument_count + 1);
                    if (passed == 0 || parameters == 0 || types == 0)
                    {
                        result.status = INTERPRETER_RESOURCE;
                    }
                    else if (result_size != 0 && frame->defined[c] == 0 && options->check_undefined)
                    {
                        result.status = INTERPRETER_UNDEFINED;
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
                        IrInterpreterIncoming incoming = {
                            0
                        };
                        if (target != 0)
                        {
                            target_check = ir_verify_complete(target, arena, false);
                        }
                        if (target_check.error == IR_ERROR_MEMORY)
                        {
                            result.status = INTERPRETER_RESOURCE;
                        }
                        else if (target_check.error != IR_OK)
                        {
                            result.status = INTERPRETER_INVALID;
                        }
                        if (result.status == INTERPRETER_OK && depth == options->max_frames)
                        {
                            result.status = INTERPRETER_FRAME_LIMIT;
                        }
                        if (result.status == INTERPRETER_OK)
                        {
                            result.status = ir_interpreter_arguments(function, call, target, passed, types, destination, depth + 1, &runtime, arena, parameters, &incoming);
                        }
                        if (result.status == INTERPRETER_OK)
                        {
                            result.status = ir_interpreter_frame(&frames[depth], program, callee, parameters, target->parameter_count, target->variadic ? &incoming : 0, i, depth + 1, options,
                                                                 active_values, active_locals, &runtime, arena);
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
                            .abi = records != 0 ? &records->call : 0,
                            .passed = records != 0 ? &function->passed[records->passed] : 0,
                            .destination = destination,
                            .result_size = result_size,
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
                        // Copies of aggregates made for a callee that didn't start go with their staging storage.
                        interpreter_memory_remove_owner(&runtime, depth + 1);
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
                    const IrInterpreterFrame* caller = depth > 1 ? &frames[depth - 2] : 0;
                    const IrCall* call = caller != 0 ? &caller->function->calls[caller->function->b[destination]] : 0;
                    const uint32_t room = call != 0 && call->abi != 0 ? caller->function->abi_calls[call->abi - 1].result_size : 0;
                    if (function->result_size != 0 && room != 0)
                    {
                        // An aggregate returned in registers (#131): its bytes, at most the call's room, from the address
                        // it returns to the call's destination, while the callee's memory still holds them.
                        const uint64_t into = caller->values[caller->function->c[destination]].low;
                        result.status = ir_interpreter_copy(&runtime, into, value.low, function->result_size < room ? function->result_size : room);
                    }
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
                case IR_BR_INDIRECT:
                {
                    // An address of one of this function's labels (ir_interpreter_symbol_address), or undefined.
                    const uint64_t offset = av.low - (uintptr_t)function->labels;
                    const uint64_t label = offset / sizeof(uint32_t);
                    result.status = function->labels != 0 && av.high == 0 && offset % sizeof(uint32_t) == 0 && label < function->label_count ?
                                        ir_interpreter_enter_block(frame, function->labels[label], frame->block, options, &result.steps) : INTERPRETER_UNDEFINED;
                    break;
                }
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
