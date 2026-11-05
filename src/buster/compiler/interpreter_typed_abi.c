// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Typed-stream call metadata at the shared SysV bridge. Include after interpreter_native.c. No IR is built or executed.

static bool interpreter_typed_native(const InterpreterNativeRequest* request, const Tier0Function* function, uint32_t call, uint32_t first, uint64_t destination, uint32_t result_size, InterpreterValue* result)
{
    InterpreterNativeRequest native = *request;
    IrPassed places[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
    uint16_t types[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
    bool records = result_size != 0;
    for (uint32_t a = 0; a < request->argument_count; a += 1)
    {
        records |= function->argument_sizes != 0 && function->argument_sizes[first + a] != 0;
        records |= function->argument_locations != 0 && function->argument_locations[first + a].kind == BUSTER_ABI_LOCATION_IGNORE;
    }
    bool valid = !records || (function->calls != 0 && (request->argument_count == 0 || function->argument_locations != 0));
    if (request->result_type == IR_VOID && function->calls != 0 && function->calls[call].result.kind == BUSTER_ABI_RESULT_REGISTERS &&
        function->calls[call].result.registers[0] == BUSTER_ABI_REGISTER_X87 && result_size == 0)
    {
        // An ignored scalar x87 result still needs a pop, including when only its arguments need ABI records.
        native.result_type = IR_F80;
    }
    if (valid && records)
    {
        for (uint32_t a = 0; a < request->argument_count; a += 1)
        {
            const uint32_t size = function->argument_sizes != 0 ? function->argument_sizes[first + a] : 0;
            places[a] = (IrPassed){ .location = function->argument_locations[first + a], .size = size };
            types[a] = size != 0 ? IR_PTR : request->types[a];
        }
        native.types = types;
        native.abi = &function->calls[call];
        native.passed = places;
        native.destination = destination;
        native.result_size = result_size;
    }
    return valid && interpreter_native_invoke(&native, result);
}

static InterpreterStatus interpreter_typed_variadic(const Tier0Function* function, const InterpreterTypedIncoming* incoming, InterpreterValue* locals, uint32_t base, InterpreterRuntime* runtime, Arena* arena)
{
    const Tier0Function* caller = incoming->caller;
    const AbiCall call = caller->calls[incoming->call];
    const AbiCall named = *function->incoming_variadic;
    const uint32_t hidden = function->result.kind == BUSTER_ABI_RESULT_MEMORY;
    const uint32_t parameter_count = function->parameter_count - hidden;
    InterpreterStatus status = call.stack_bytes >= named.stack_bytes && call.stack_alignment_log2 >= BUSTER_ABI_STACK_ALIGNMENT_LOG2 &&
                               call.stack_alignment_log2 <= BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2 ? INTERPRETER_OK : INTERPRETER_INVALID;
    InterpreterNativeArea area = { 0 };
    for (uint32_t l = 0; status == INTERPRETER_OK && l < function->local_count; l += 1)
    {
        const uint32_t role = function->local_flags != 0 ? function->local_flags[l] & BUSTER_TYPED_LOCAL_ADDRESS_ONLY : 0;
        if (role == BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA)
        {
            area.registers = (uint8_t*)(uintptr_t)locals[base + l].low;
            InterpreterMemory* memory = 0;
            uint64_t offset = 0;
            status = interpreter_memory_range(runtime, (uintptr_t)area.registers, BUSTER_ABI_SAVE_AREA_BYTES, true, &memory, &offset) ? INTERPRETER_OK : INTERPRETER_MEMORY;
            area.registers_defined = memory != 0 ? memory->defined : 0;
        }
        else if (role == BUSTER_TYPED_LOCAL_OVERFLOW_ARG_AREA)
        {
            area.stack = (uint8_t*)(uintptr_t)(locals[base + l].low - named.stack_bytes);
            InterpreterMemory* memory = 0;
            uint64_t offset = 0;
            status = interpreter_memory_range(runtime, locals[base + l].low, call.stack_bytes - named.stack_bytes, true, &memory, &offset) ? INTERPRETER_OK : INTERPRETER_MEMORY;
            // Only the unnamed tail is registered. The full area and map are owned by this frame, including the prefix.
            area.stack_defined = memory != 0 && memory->defined != 0 ? memory->defined - named.stack_bytes : 0;
            area.stack_capacity = call.stack_bytes;
        }
    }
    if (status == INTERPRETER_OK && area.registers == 0)
    {
        area.registers = arena_push(arena, BUSTER_ABI_SAVE_AREA_BYTES, 16);
        area.registers_defined = arena_push(arena, BUSTER_ABI_SAVE_AREA_BYTES, 1);
        status = area.registers != 0 && area.registers_defined != 0 ? INTERPRETER_OK : INTERPRETER_RESOURCE;
    }
    if (status == INTERPRETER_OK && area.stack == 0)
    {
        area.stack = arena_push(arena, call.stack_bytes, UINT64_C(1) << call.stack_alignment_log2);
        area.stack_defined = arena_push(arena, call.stack_bytes, 1);
        area.stack_capacity = call.stack_bytes;
        status = area.stack != 0 && area.stack_defined != 0 ? INTERPRETER_OK : INTERPRETER_RESOURCE;
    }
    if (status == INTERPRETER_OK)
    {
        memset(area.registers, 0, BUSTER_ABI_SAVE_AREA_BYTES);
        memset(area.registers_defined, 0, BUSTER_ABI_SAVE_AREA_BYTES);
        memset(area.stack, 0, call.stack_bytes);
        memset(area.stack_defined, 0, call.stack_bytes);
        if (hidden != 0)
        {
            interpreter_native_store(area.registers, area.registers_defined, (const uint8_t*)&incoming->destination, 0, 8);
        }
    }
    uint32_t integers = hidden;
    uint32_t vectors = 0;
    for (uint32_t a = 0; status == INTERPRETER_OK && a < incoming->count; a += 1)
    {
        const uint32_t index = incoming->first + a;
        const AbiLocation place = caller->argument_locations[index];
        const uint32_t size = caller->argument_sizes != 0 ? caller->argument_sizes[index] : 0;
        const bool stacked = place.kind == BUSTER_ABI_LOCATION_STACK || place.kind == BUSTER_ABI_LOCATION_SPLIT;
        status = a >= parameter_count && stacked && place.stack_offset < named.stack_bytes ? INTERPRETER_INVALID : INTERPRETER_OK;
        for (uint32_t r = 0; a < parameter_count && r < 2; r += 1)
        {
            const uint8_t reg = place.kind == BUSTER_ABI_LOCATION_REGISTERS || place.kind == BUSTER_ABI_LOCATION_SPLIT ? place.registers[r] : BUSTER_ABI_REGISTER_NONE;
            integers += reg != BUSTER_ABI_REGISTER_NONE && reg < BUSTER_ABI_REGISTER_VECTOR;
            vectors += reg != BUSTER_ABI_REGISTER_NONE && reg >= BUSTER_ABI_REGISTER_VECTOR;
        }
        if (status != INTERPRETER_OK)
        {
            break;
        }
        const InterpreterValue value = incoming->values[a];
        const uint8_t* bytes = (const uint8_t*)&value;
        const uint8_t* defined = 0;
        uint8_t scalar_defined[16] = { 0 };
        uint32_t extent = tier0_class_int128(caller->argument_classes[index]) || caller->argument_classes[index] == BUSTER_TYPED_CLASS_F80 ? 16 : 8;
        if (size != 0)
        {
            InterpreterMemory* memory = 0;
            uint64_t offset = 0;
            const bool readable = interpreter_memory_range(runtime, value.low, size, false, &memory, &offset);
            status = readable ? INTERPRETER_OK : INTERPRETER_MEMORY;
            bytes = (const uint8_t*)(uintptr_t)value.low;
            defined = memory != 0 && memory->defined != 0 ? memory->defined + offset : 0;
            extent = size;
        }
        else
        {
            memset(scalar_defined, 1, interpreter_typed_bytes(caller->argument_classes[index]));
            defined = scalar_defined;
        }
        if (status == INTERPRETER_OK && !interpreter_native_put(&area, place, bytes, defined, extent))
        {
            status = INTERPRETER_INVALID;
        }
    }
    status = status == INTERPRETER_OK && (integers != named.integer_registers || vectors != named.vector_registers) ? INTERPRETER_INVALID : status;
    return status;
}
