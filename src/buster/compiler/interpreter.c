// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Scalar execution oracle for the executable typed stream (#176). Include after ir.c and tier0.c. The IR front and
// native-call bridge use the values, bounded memory registry, limits and result types below. The machine never recurses:
// every interpreted call owns an arena-backed frame. Input columns have their exact logical lengths and need no padding.
// Runtime memory checks cover registered allocations, including each live local, and defined-byte maps are optional.
// External pointers require explicit registration when checking is enabled. All scratch storage is released on return.
#ifndef BUSTER_INTERPRETER_INCLUDED
#define BUSTER_INTERPRETER_INCLUDED

typedef struct InterpreterValue InterpreterValue;
struct InterpreterValue
{
    uint64_t low;
    uint64_t high;
};

typedef enum InterpreterStatus
{
    INTERPRETER_OK,
    INTERPRETER_INVALID,
    INTERPRETER_RESOURCE,
    INTERPRETER_STEP_LIMIT,
    INTERPRETER_FRAME_LIMIT,
    INTERPRETER_VALUE_LIMIT,
    INTERPRETER_UNDEFINED,
    INTERPRETER_MEMORY,
    INTERPRETER_ARITHMETIC,
    INTERPRETER_SYMBOL,
    INTERPRETER_NATIVE,
} InterpreterStatus;

typedef struct InterpreterOptions InterpreterOptions;
struct InterpreterOptions
{
    uint64_t max_steps;
    uint32_t max_frames;
    uint32_t max_values;
    uint32_t max_locals;
    uint32_t max_memories;
    bool check_memory;
    bool check_undefined;
};

typedef struct InterpreterResult InterpreterResult;
struct InterpreterResult
{
    InterpreterValue value;
    InterpreterStatus status;
    uint32_t function;
    uint32_t entry;
    uint64_t steps;
};

typedef struct InterpreterMemory InterpreterMemory;
#define BUSTER_INTERPRETER_HEAP_OWNER UINT32_MAX
struct InterpreterMemory
{
    uint8_t* bytes;
    uint64_t size;
    uint8_t* defined;
    bool writable;
    uint32_t owner;
};

typedef struct InterpreterRuntime InterpreterRuntime;
struct InterpreterRuntime
{
    InterpreterMemory* memories;
    uint32_t memory_count;
    uint32_t memory_capacity;
    bool check_memory;
    bool check_undefined;
};

typedef struct InterpreterBinding InterpreterBinding;
struct InterpreterBinding
{
    SymbolId symbol;
    uint8_t* bytes;
    uint64_t size;
    bool writable;
};

#define BUSTER_INTERPRETER_NATIVE_ALLOCATE 1u
#define BUSTER_INTERPRETER_NATIVE_FREE 2u
#define BUSTER_INTERPRETER_NATIVE_REALLOCATE 4u
#define BUSTER_INTERPRETER_NATIVE_CALLOC 8u
#define BUSTER_INTERPRETER_NATIVE_COPY 16u
#define BUSTER_INTERPRETER_NATIVE_SET 32u

typedef struct InterpreterNativeBinding InterpreterNativeBinding;
struct InterpreterNativeBinding
{
    uint32_t symbol;
    void* address;
    uint32_t flags;
};

typedef struct InterpreterNativeRequest InterpreterNativeRequest;
struct InterpreterNativeRequest
{
    const InterpreterNativeBinding* binding;
    const uint16_t* types;
    const uint8_t* extensions;
    const InterpreterValue* arguments;
    uint32_t argument_count;
    uint32_t named_count;
    uint16_t result_type;
    bool variadic;
    InterpreterRuntime* runtime;
};

typedef struct InterpreterProgram InterpreterProgram;
struct InterpreterProgram
{
    const Tier0Function* functions;
    const SymbolId* function_symbols;
    uint32_t function_count;
    const InterpreterBinding* bindings;
    uint32_t binding_count;
    const InterpreterNativeBinding* natives;
    uint32_t native_count;
};

static __attribute__((unused)) bool interpreter_native_invoke(const InterpreterNativeRequest* request, InterpreterValue* result);
static __attribute__((unused)) void interpreter_native_release(InterpreterRuntime* runtime);
static __attribute__((unused)) bool interpreter_memory_add(InterpreterRuntime* runtime, uint8_t* bytes, uint64_t size, uint8_t* defined, bool writable, uint32_t owner);
static __attribute__((unused)) void interpreter_memory_remove(InterpreterRuntime* runtime, uint8_t* bytes);
static __attribute__((unused)) void interpreter_memory_remove_owner(InterpreterRuntime* runtime, uint32_t owner);
static __attribute__((unused)) InterpreterStatus interpreter_memory_access(InterpreterRuntime* runtime, uint64_t address, uint64_t bytes, bool write);
static __attribute__((unused)) InterpreterOptions interpreter_default_options(void);
static __attribute__((unused)) bool interpreter_mappings_valid(const void* function_symbols, uint32_t function_count, uint32_t function_stride, const InterpreterBinding* bindings, uint32_t binding_count, const InterpreterNativeBinding* natives, uint32_t native_count);
static __attribute__((unused)) bool interpreter_typed_run(const InterpreterProgram* program, SymbolId entry, const InterpreterValue* arguments, uint32_t argument_count, Arena* scratch, const InterpreterOptions* options, InterpreterResult* result);

static InterpreterOptions interpreter_default_options(void)
{
    const InterpreterOptions result = {
        .max_steps = 200000000ull,
        .max_frames = 1024,
        .max_values = 65536,
        .max_locals = 65536,
        .max_memories = 65536,
        .check_memory = true,
        .check_undefined = true,
    };
    return result;
}

// A function-symbol slice can be a SymbolId column or the first field of wider function rows. Exactly count
// logical rows are read with memcpy, so no row alignment or trailing padding is required. All three symbol maps
// share one namespace. Addresses may alias; identities may not. The quadratic scan keeps this oracle boundary
// allocation-free and cannot reorder caller-owned mappings or introduce first-match ambiguity.
static SymbolId interpreter_mapping_function(const void* rows, uint32_t stride, uint32_t index)
{
    SymbolId result;
    memcpy(&result, (const uint8_t*)rows + (uint64_t)index * stride, sizeof(result));
    return result;
}

static bool interpreter_mappings_valid(const void* function_symbols, uint32_t function_count, uint32_t function_stride, const InterpreterBinding* bindings, uint32_t binding_count, const InterpreterNativeBinding* natives, uint32_t native_count)
{
    bool result = function_count == 0 || (function_symbols != 0 && function_stride >= sizeof(SymbolId));
    result = result && (binding_count == 0 || bindings != 0) && (native_count == 0 || natives != 0);
    if (result && function_count != 0)
    {
        const uint64_t last = (uint64_t)(function_count - 1) * function_stride;
        const uint64_t base = (uint64_t)(uintptr_t)function_symbols;
        result = last <= UINT64_MAX - sizeof(SymbolId) && last + sizeof(SymbolId) <= UINT64_MAX - base;
    }
    for (uint32_t f = 0; result && f < function_count; f += 1)
    {
        const SymbolId symbol = interpreter_mapping_function(function_symbols, function_stride, f);
        result = symbol != 0 && symbol != UINT32_MAX;
        for (uint32_t p = 0; result && p < f; p += 1)
        {
            result = symbol != interpreter_mapping_function(function_symbols, function_stride, p);
        }
    }
    for (uint32_t b = 0; result && b < binding_count; b += 1)
    {
        const SymbolId symbol = bindings[b].symbol;
        result = symbol != 0 && symbol != UINT32_MAX;
        for (uint32_t f = 0; result && f < function_count; f += 1)
        {
            result = symbol != interpreter_mapping_function(function_symbols, function_stride, f);
        }
        for (uint32_t p = 0; result && p < b; p += 1)
        {
            result = symbol != bindings[p].symbol;
        }
    }
    for (uint32_t n = 0; result && n < native_count; n += 1)
    {
        const SymbolId symbol = natives[n].symbol;
        result = symbol != 0 && symbol != UINT32_MAX;
        for (uint32_t f = 0; result && f < function_count; f += 1)
        {
            result = symbol != interpreter_mapping_function(function_symbols, function_stride, f);
        }
        for (uint32_t b = 0; result && b < binding_count; b += 1)
        {
            result = symbol != bindings[b].symbol;
        }
        for (uint32_t p = 0; result && p < n; p += 1)
        {
            result = symbol != natives[p].symbol;
        }
    }
    return result;
}

static bool interpreter_memory_add(InterpreterRuntime* runtime, uint8_t* bytes, uint64_t size, uint8_t* defined, bool writable, uint32_t owner)
{
    const uint64_t address = (uint64_t)(uintptr_t)bytes;
    bool result = runtime != 0 && runtime->memory_count < runtime->memory_capacity && size <= UINT64_MAX - address && (size == 0 || bytes != 0);
    if (result)
    {
        runtime->memories[runtime->memory_count++] = (InterpreterMemory){
            .bytes = bytes,
            .size = size,
            .defined = defined,
            .writable = writable,
            .owner = owner,
        };
    }
    return result;
}

static void interpreter_memory_remove(InterpreterRuntime* runtime, uint8_t* bytes)
{
    uint32_t destination = 0;
    for (uint32_t i = 0; i < runtime->memory_count; i += 1)
    {
        if (runtime->memories[i].bytes != bytes)
        {
            runtime->memories[destination++] = runtime->memories[i];
        }
    }
    runtime->memory_count = destination;
}

static void interpreter_memory_remove_owner(InterpreterRuntime* runtime, uint32_t owner)
{
    uint32_t destination = 0;
    for (uint32_t i = 0; i < runtime->memory_count; i += 1)
    {
        if (runtime->memories[i].owner != owner)
        {
            runtime->memories[destination++] = runtime->memories[i];
        }
    }
    runtime->memory_count = destination;
}

static InterpreterStatus interpreter_memory_access(InterpreterRuntime* runtime, uint64_t address, uint64_t bytes, bool write)
{
    InterpreterStatus result = runtime->check_memory ? INTERPRETER_MEMORY : INTERPRETER_OK;
    if (bytes == 0)
    {
        result = INTERPRETER_OK;
    }
    else if (address == 0 || bytes > UINT64_MAX - address)
    {
        result = INTERPRETER_MEMORY;
    }
    else
    {
        for (uint32_t i = 0; i < runtime->memory_count; i += 1)
        {
            InterpreterMemory* memory = &runtime->memories[i];
            const uint64_t base = (uint64_t)(uintptr_t)memory->bytes;
            const uint64_t offset = address - base;
            if (address >= base && offset <= memory->size && bytes <= memory->size - offset)
            {
                result = write && !memory->writable ? INTERPRETER_MEMORY : INTERPRETER_OK;
                if (result == INTERPRETER_OK && memory->defined != 0)
                {
                    for (uint64_t b = 0; b < bytes; b += 1)
                    {
                        if (write)
                        {
                            memory->defined[offset + b] = 1;
                        }
                        else if (runtime->check_undefined && memory->defined[offset + b] == 0)
                        {
                            result = INTERPRETER_UNDEFINED;
                        }
                    }
                }
                break;
            }
        }
    }
    return result;
}

// The typed front keeps signedness in its class column. Values are normalized at every producer, so arithmetic never
// depends on incidental register extension. Conversions to integers outside C's defined range report arithmetic faults.
static uint64_t interpreter_typed_normalize(uint32_t class, uint64_t value)
{
    uint64_t result = value;
    switch (class)
    {
    case BUSTER_TYPED_CLASS_I8:
        result = (uint64_t)(int64_t)(int8_t)value;
        break;
    case BUSTER_TYPED_CLASS_U8:
        result = (uint8_t)value;
        break;
    case BUSTER_TYPED_CLASS_I16:
        result = (uint64_t)(int64_t)(int16_t)value;
        break;
    case BUSTER_TYPED_CLASS_U16:
        result = (uint16_t)value;
        break;
    case BUSTER_TYPED_CLASS_I32:
        result = (uint64_t)(int64_t)(int32_t)value;
        break;
    case BUSTER_TYPED_CLASS_U32:
    case BUSTER_TYPED_CLASS_F32:
        result = (uint32_t)value;
        break;
    default:
        break;
    }
    return result;
}

static long double interpreter_typed_float(uint32_t class, uint64_t value)
{
    long double result;
    if (class == BUSTER_TYPED_CLASS_F32)
    {
        float f;
        const uint32_t bits = (uint32_t)value;
        memcpy(&f, &bits, sizeof(f));
        result = f;
    }
    else
    {
        double d;
        memcpy(&d, &value, sizeof(d));
        result = d;
    }
    return result;
}

static uint64_t interpreter_typed_float_bits(uint32_t class, long double value)
{
    uint64_t result = 0;
    if (class == BUSTER_TYPED_CLASS_F32)
    {
        const float f = (float)value;
        uint32_t bits;
        memcpy(&bits, &f, sizeof(bits));
        result = bits;
    }
    else
    {
        const double d = (double)value;
        memcpy(&result, &d, sizeof(result));
    }
    return result;
}

static uint64_t interpreter_typed_convert(uint32_t to, uint32_t from, uint64_t value, InterpreterStatus* status)
{
    uint64_t result = 0;
    const bool from_floating = tier0_class_floating(from);
    const bool to_floating = tier0_class_floating(to);
    if (from_floating && to_floating)
    {
        result = interpreter_typed_float_bits(to, interpreter_typed_float(from, value));
    }
    else if (to_floating)
    {
        const long double number = tier0_class_signed(from) ? (long double)(int64_t)value : (long double)value;
        result = interpreter_typed_float_bits(to, number);
    }
    else if (from_floating)
    {
        const long double number = interpreter_typed_float(from, value);
        const uint32_t bits = 8 * tier0_class_bytes(to);
        const bool sign = tier0_class_signed(to);
        const long double high = sign ? (long double)(1ull << (bits - 1)) : bits == 64 ? 18446744073709551616.0L : (long double)(1ull << bits);
        const long double low = sign ? -high : 0.0L;
        // Truncation toward zero permits negative fractional values above -1 for unsigned destinations.
        const bool fits = number == number && number < high && (sign ? number > low - 1.0L : number > -1.0L);
        if (!fits)
        {
            *status = INTERPRETER_ARITHMETIC;
        }
        else
        {
            result = sign ? (uint64_t)(int64_t)number : (uint64_t)number;
        }
    }
    else
    {
        result = value;
    }
    result = interpreter_typed_normalize(to, result);
    return result;
}

static uint64_t interpreter_typed_binary(uint32_t kind, uint32_t class, uint64_t a, uint64_t b, InterpreterStatus* status)
{
    uint64_t result = 0;
    if (tier0_class_floating(class))
    {
        if (kind > BUSTER_TYPED_DIV)
        {
            *status = INTERPRETER_INVALID;
        }
        else if (class == BUSTER_TYPED_CLASS_F32)
        {
            const float x = (float)interpreter_typed_float(class, a);
            const float y = (float)interpreter_typed_float(class, b);
            const float number = kind == BUSTER_TYPED_ADD ? x + y : kind == BUSTER_TYPED_SUB ? x - y : kind == BUSTER_TYPED_MUL ? x * y : x / y;
            result = interpreter_typed_float_bits(class, number);
        }
        else
        {
            const double x = (double)interpreter_typed_float(class, a);
            const double y = (double)interpreter_typed_float(class, b);
            const double number = kind == BUSTER_TYPED_ADD ? x + y : kind == BUSTER_TYPED_SUB ? x - y : kind == BUSTER_TYPED_MUL ? x * y : x / y;
            result = interpreter_typed_float_bits(class, number);
        }
    }
    else
    {
        const bool sign = tier0_class_signed(class);
        const uint32_t width = tier0_class_bytes(class) == 8 ? 64 : 32;
        const uint32_t shift = (uint32_t)b & (width - 1);
        switch (kind)
        {
        case BUSTER_TYPED_ADD:
            result = a + b;
            break;
        case BUSTER_TYPED_SUB:
            result = a - b;
            break;
        case BUSTER_TYPED_MUL:
            result = a * b;
            break;
        case BUSTER_TYPED_AND:
            result = a & b;
            break;
        case BUSTER_TYPED_OR:
            result = a | b;
            break;
        case BUSTER_TYPED_XOR:
            result = a ^ b;
            break;
        case BUSTER_TYPED_SHL:
            result = a << shift;
            break;
        case BUSTER_TYPED_SHR:
            result = sign ? (uint64_t)((int64_t)a >> shift) : a >> shift;
            break;
        case BUSTER_TYPED_DIV:
        case BUSTER_TYPED_MOD:
        {
            const int64_t x = (int64_t)a;
            const int64_t y = (int64_t)b;
            const int64_t minimum = width == 64 ? INT64_MIN : INT32_MIN;
            if (b == 0 || (sign && x == minimum && y == -1))
            {
                *status = INTERPRETER_ARITHMETIC;
            }
            else if (sign)
            {
                result = kind == BUSTER_TYPED_DIV ? (uint64_t)(x / y) : (uint64_t)(x % y);
            }
            else
            {
                result = kind == BUSTER_TYPED_DIV ? a / b : a % b;
            }
            break;
        }
        default:
            *status = INTERPRETER_INVALID;
            break;
        }
        result = interpreter_typed_normalize(class, result);
    }
    return result;
}

static uint64_t interpreter_typed_compare(uint32_t kind, uint32_t class, uint64_t a, uint64_t b)
{
    bool equal;
    bool less;
    bool greater;
    if (tier0_class_floating(class))
    {
        const long double x = interpreter_typed_float(class, a);
        const long double y = interpreter_typed_float(class, b);
        equal = x == y;
        less = x < y;
        greater = x > y;
    }
    else if (tier0_class_signed(class))
    {
        equal = a == b;
        less = (int64_t)a < (int64_t)b;
        greater = (int64_t)a > (int64_t)b;
    }
    else
    {
        equal = a == b;
        less = a < b;
        greater = a > b;
    }
    const bool holds = kind == BUSTER_TYPED_EQ ? equal : kind == BUSTER_TYPED_NE ? !equal : kind == BUSTER_TYPED_LT ? less : kind == BUSTER_TYPED_LE ? less || equal : kind == BUSTER_TYPED_GT ? greater : greater || equal;
    return holds;
}

static uint16_t interpreter_typed_ir_type(uint32_t class)
{
    static const uint16_t types[BUSTER_TYPED_CLASSES] = {
        IR_VOID,
        IR_I8,
        IR_I8,
        IR_I16,
        IR_I16,
        IR_I32,
        IR_I32,
        IR_I64,
        IR_I64,
        IR_F32,
        IR_F64,
    };
    const uint16_t result = types[class];
    return result;
}

typedef struct InterpreterTypedFrame InterpreterTypedFrame;
struct InterpreterTypedFrame
{
    uint32_t function;
    uint32_t entry;
    uint32_t stack_base;
    uint32_t local_base;
    uint8_t return_class;
};

static uint32_t interpreter_typed_find_function(const InterpreterProgram* program, SymbolId symbol)
{
    uint32_t result = UINT32_MAX;
    for (uint32_t f = 0; f < program->function_count; f += 1)
    {
        if (program->function_symbols[f] == symbol)
        {
            result = f;
            break;
        }
    }
    return result;
}

static const InterpreterBinding* interpreter_typed_find_binding(const InterpreterProgram* program, SymbolId symbol)
{
    const InterpreterBinding* result = 0;
    for (uint32_t b = 0; b < program->binding_count; b += 1)
    {
        if (program->bindings[b].symbol == symbol)
        {
            result = &program->bindings[b];
            break;
        }
    }
    return result;
}

// Interpreted function addresses are opaque tokens naming stable caller-owned function rows. Native functions
// retain their host address. Neither kind is registered as readable data, and GLOBAL/STORE still require data.
static bool interpreter_typed_symbol_address(const InterpreterProgram* program, SymbolId symbol, uint64_t* address)
{
    const uint32_t function = interpreter_typed_find_function(program, symbol);
    bool found = function != UINT32_MAX;
    if (found)
    {
        *address = (uint64_t)(uintptr_t)&program->functions[function];
    }
    for (uint32_t n = 0; !found && n < program->native_count; n += 1)
    {
        if (program->natives[n].symbol == symbol)
        {
            *address = (uint64_t)(uintptr_t)program->natives[n].address;
            found = program->natives[n].address != 0;
        }
    }
    if (!found)
    {
        const InterpreterBinding* binding = interpreter_typed_find_binding(program, symbol);
        if (binding != 0)
        {
            *address = (uint64_t)(uintptr_t)binding->bytes;
            found = true;
        }
    }
    return found;
}

static bool interpreter_typed_function_valid(const Tier0Function* function)
{
    bool result = function->count <= BUSTER_TIER0_MAX_ENTRIES && function->local_count <= BUSTER_TIER0_MAX_LOCALS && function->parameter_count <= function->local_count;
    result = result && (function->count == 0 || (function->kinds != 0 && function->classes != 0 && function->values != 0));
    result = result && (function->local_count == 0 || function->local_classes != 0);
    result = result && (function->call_count == 0 || (function->call_targets != 0 && function->call_first != 0 && function->call_argument_counts != 0 && function->call_variadic != 0));
    result = result && (function->argument_count == 0 || function->argument_classes != 0);
    for (uint32_t l = 0; result && l < function->local_count; l += 1)
    {
        result = function->local_classes[l] > BUSTER_TYPED_CLASS_VOID && function->local_classes[l] < BUSTER_TYPED_CLASSES;
    }
    for (uint32_t c = 0; result && c < function->call_count; c += 1)
    {
        result = function->call_first[c] <= function->argument_count && function->call_argument_counts[c] <= function->argument_count - function->call_first[c] && function->call_argument_counts[c] <= BUSTER_TIER0_MAX_CALL_ARGUMENTS;
    }
    for (uint32_t a = 0; result && a < function->argument_count; a += 1)
    {
        result = function->argument_classes[a] > BUSTER_TYPED_CLASS_VOID && function->argument_classes[a] < BUSTER_TYPED_CLASSES;
    }
    return result;
}

static InterpreterStatus interpreter_typed_enter(const Tier0Function* function, uint64_t* locals, uint8_t* defined, uint32_t local_base, const InterpreterValue* arguments, uint32_t count, InterpreterRuntime* runtime, uint32_t owner)
{
    InterpreterStatus result = count == function->parameter_count ? INTERPRETER_OK : INTERPRETER_INVALID;
    for (uint32_t l = 0; result == INTERPRETER_OK && l < function->local_count; l += 1)
    {
        const uint32_t index = local_base + l;
        const uint32_t bytes = tier0_class_bytes(function->local_classes[l]);
        locals[index] = l < count ? interpreter_typed_normalize(function->local_classes[l], arguments[l].low) : 0;
        memset(defined + (uint64_t)index * 8, l < count ? 1 : 0, 8);
        if (!interpreter_memory_add(runtime, (uint8_t*)&locals[index], bytes, defined + (uint64_t)index * 8, true, owner))
        {
            result = INTERPRETER_RESOURCE;
        }
    }
    return result;
}

static bool interpreter_typed_run(const InterpreterProgram* program, SymbolId entry, const InterpreterValue* arguments, uint32_t argument_count, Arena* scratch, const InterpreterOptions* options, InterpreterResult* result)
{
    const uint64_t start = scratch->length;
    const InterpreterOptions limits = options != 0 ? *options : interpreter_default_options();
    *result = (InterpreterResult){ 0 };
    bool valid = program != 0 && program->function_count != 0 && program->functions != 0 && program->function_symbols != 0;
    valid = valid && (argument_count == 0 || arguments != 0) && (program->binding_count == 0 || program->bindings != 0) && (program->native_count == 0 || program->natives != 0);
    valid = valid && limits.max_frames != 0 && limits.max_frames < BUSTER_INTERPRETER_HEAP_OWNER && limits.max_values != 0 && limits.max_locals != 0 && limits.max_memories != 0;
    valid = valid && interpreter_mappings_valid(program->function_symbols, program->function_count, sizeof(SymbolId), program->bindings, program->binding_count, program->natives, program->native_count);
    for (uint32_t f = 0; valid && f < program->function_count; f += 1)
    {
        valid = interpreter_typed_function_valid(&program->functions[f]);
    }
    uint32_t initial = valid ? interpreter_typed_find_function(program, entry) : UINT32_MAX;
    result->status = valid ? initial != UINT32_MAX ? INTERPRETER_OK : INTERPRETER_SYMBOL : INTERPRETER_INVALID;
    InterpreterTypedFrame* frames = 0;
    InterpreterValue* stack = 0;
    uint8_t* classes = 0;
    uint64_t* locals = 0;
    uint8_t* defined = 0;
    InterpreterRuntime runtime = {
        .memory_capacity = limits.max_memories,
        .check_memory = limits.check_memory,
        .check_undefined = limits.check_undefined,
    };
    uint32_t depth = 0;
    uint32_t top = 0;
    uint32_t local_top = 0;
    if (result->status == INTERPRETER_OK)
    {
        frames = BUSTER_ARENA_PUSH_ARRAY(scratch, InterpreterTypedFrame, limits.max_frames);
        stack = BUSTER_ARENA_PUSH_ARRAY(scratch, InterpreterValue, limits.max_values);
        classes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, limits.max_values);
        locals = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, limits.max_locals);
        defined = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)limits.max_locals * 8);
        runtime.memories = BUSTER_ARENA_PUSH_ARRAY(scratch, InterpreterMemory, limits.max_memories);
        if (frames == 0 || stack == 0 || classes == 0 || locals == 0 || defined == 0 || runtime.memories == 0)
        {
            result->status = INTERPRETER_RESOURCE;
        }
    }
    for (uint32_t b = 0; result->status == INTERPRETER_OK && b < program->binding_count; b += 1)
    {
        const InterpreterBinding* binding = &program->bindings[b];
        if (!interpreter_memory_add(&runtime, binding->bytes, binding->size, 0, binding->writable, 0))
        {
            result->status = INTERPRETER_RESOURCE;
        }
    }
    if (result->status == INTERPRETER_OK)
    {
        const Tier0Function* function = &program->functions[initial];
        result->status = function->local_count <= limits.max_locals ? interpreter_typed_enter(function, locals, defined, 0, arguments, argument_count, &runtime, 1) : INTERPRETER_RESOURCE;
        frames[0] = (InterpreterTypedFrame){
            .function = initial,
        };
        depth = 1;
        local_top = function->local_count;
    }
    while (depth != 0 && result->status == INTERPRETER_OK)
    {
        InterpreterTypedFrame* frame = &frames[depth - 1];
        const Tier0Function* function = &program->functions[frame->function];
        result->function = frame->function;
        result->entry = frame->entry;
        if (result->steps >= limits.max_steps)
        {
            result->status = INTERPRETER_STEP_LIMIT;
            break;
        }
        result->steps += 1;
        const bool end = frame->entry == function->count;
        if (frame->entry > function->count)
        {
            result->status = INTERPRETER_INVALID;
            break;
        }
        const uint32_t i = frame->entry;
        const uint32_t kind = end ? BUSTER_TYPED_RETURN : function->kinds[i];
        const uint32_t class = end ? BUSTER_TYPED_CLASS_VOID : function->classes[i];
        const uint64_t value = end ? 0 : function->values[i];
        frame->entry += !end;
        if (kind == BUSTER_TYPED_NONE || kind >= BUSTER_TYPED_KINDS || class >= BUSTER_TYPED_CLASSES)
        {
            result->status = INTERPRETER_INVALID;
            break;
        }
        uint32_t needed = 0;
        uint32_t pushed = 0;
        if (kind >= BUSTER_TYPED_CONST && kind <= BUSTER_TYPED_GLOBAL_ADDRESS)
        {
            pushed = 1;
        }
        else if (kind == BUSTER_TYPED_DUP)
        {
            needed = 1;
            pushed = 1;
        }
        else if ((kind >= BUSTER_TYPED_ADD && kind <= BUSTER_TYPED_SET) || kind == BUSTER_TYPED_PTR_ADD)
        {
            needed = 2;
        }
        else if (kind == BUSTER_TYPED_CALL)
        {
            if (value >= function->call_count)
            {
                result->status = INTERPRETER_INVALID;
                break;
            }
            needed = function->call_argument_counts[value];
            pushed = class != BUSTER_TYPED_CLASS_VOID && needed == 0;
        }
        else if (kind != BUSTER_TYPED_JUMP && !(kind == BUSTER_TYPED_RETURN && class == BUSTER_TYPED_CLASS_VOID))
        {
            needed = 1;
        }
        if (top < frame->stack_base || needed > top - frame->stack_base || pushed > limits.max_values - top)
        {
            result->status = needed > top - frame->stack_base ? INTERPRETER_INVALID : INTERPRETER_VALUE_LIMIT;
            break;
        }
        if (class == BUSTER_TYPED_CLASS_VOID && kind != BUSTER_TYPED_CALL && kind != BUSTER_TYPED_JUMP && kind != BUSTER_TYPED_RETURN)
        {
            result->status = INTERPRETER_INVALID;
            break;
        }
        bool shape = true;
        if (kind == BUSTER_TYPED_DUP || kind == BUSTER_TYPED_NEG || kind == BUSTER_TYPED_NOT || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL || kind == BUSTER_TYPED_STORE_GLOBAL || kind == BUSTER_TYPED_SET_GLOBAL || kind == BUSTER_TYPED_DISCARD || kind == BUSTER_TYPED_JUMP_VALUE || (kind == BUSTER_TYPED_RETURN && class != BUSTER_TYPED_CLASS_VOID))
        {
            shape = classes[top - 1] == class;
        }
        else if (kind == BUSTER_TYPED_LOAD)
        {
            shape = classes[top - 1] == BUSTER_TYPED_CLASS_U64;
        }
        else if (kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET)
        {
            shape = classes[top - 2] == BUSTER_TYPED_CLASS_U64 && classes[top - 1] == class;
        }
        else if (kind == BUSTER_TYPED_PTR_ADD)
        {
            shape = class == BUSTER_TYPED_CLASS_U64 && classes[top - 2] == BUSTER_TYPED_CLASS_U64 && (classes[top - 1] == BUSTER_TYPED_CLASS_I64 || classes[top - 1] == BUSTER_TYPED_CLASS_U64) && value != 0;
        }
        else if (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE)
        {
            shape = class == BUSTER_TYPED_CLASS_I32 && classes[top - 2] == classes[top - 1];
        }
        else if (kind >= BUSTER_TYPED_ADD && kind <= BUSTER_TYPED_SHR)
        {
            shape = classes[top - 2] == class && ((kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR) ? tier0_class_integer(classes[top - 1]) : classes[top - 1] == class);
        }
        else if (kind == BUSTER_TYPED_BRANCH_FALSE || kind == BUSTER_TYPED_BRANCH_TRUE)
        {
            shape = tier0_class_integer(class) && classes[top - 1] == class;
        }
        if (kind == BUSTER_TYPED_LOCAL_ADDRESS || kind == BUSTER_TYPED_GLOBAL_ADDRESS)
        {
            shape = class == BUSTER_TYPED_CLASS_U64;
        }
        if (kind == BUSTER_TYPED_RETURN)
        {
            shape = shape && top - frame->stack_base == (class != BUSTER_TYPED_CLASS_VOID);
        }
        if (kind >= BUSTER_TYPED_BRANCH_FALSE && kind <= BUSTER_TYPED_JUMP_VALUE)
        {
            shape = shape && value <= function->count;
        }
        if (!shape || (kind == BUSTER_TYPED_NOT && !tier0_class_integer(class)))
        {
            result->status = INTERPRETER_INVALID;
            break;
        }
        uint64_t address = 0;
        bool read = false;
        bool write = false;
        if (kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_LOCAL_ADDRESS || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
        {
            if (value >= function->local_count || (kind != BUSTER_TYPED_LOCAL_ADDRESS && function->local_classes[value] != class))
            {
                result->status = INTERPRETER_INVALID;
                break;
            }
            address = (uint64_t)(uintptr_t)&locals[frame->local_base + value];
            read = kind == BUSTER_TYPED_LOCAL;
            write = kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL;
        }
        else if (kind == BUSTER_TYPED_GLOBAL_ADDRESS)
        {
            if (value > UINT32_MAX || !interpreter_typed_symbol_address(program, (SymbolId)value, &address))
            {
                result->status = INTERPRETER_SYMBOL;
                break;
            }
        }
        else if (kind == BUSTER_TYPED_GLOBAL || kind == BUSTER_TYPED_STORE_GLOBAL || kind == BUSTER_TYPED_SET_GLOBAL)
        {
            const InterpreterBinding* binding = value <= UINT32_MAX ? interpreter_typed_find_binding(program, (SymbolId)value) : 0;
            if (binding == 0)
            {
                result->status = INTERPRETER_SYMBOL;
                break;
            }
            address = (uint64_t)(uintptr_t)binding->bytes;
            read = kind == BUSTER_TYPED_GLOBAL;
            write = kind == BUSTER_TYPED_STORE_GLOBAL || kind == BUSTER_TYPED_SET_GLOBAL;
        }
        else if (kind == BUSTER_TYPED_LOAD || kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET)
        {
            address = stack[top - needed].low;
            read = kind == BUSTER_TYPED_LOAD;
            write = !read;
        }
        if (read || write)
        {
            result->status = interpreter_memory_access(&runtime, address, tier0_class_bytes(class), write);
            if (result->status != INTERPRETER_OK)
            {
                break;
            }
        }
        const uint32_t operand = needed != 0 ? classes[top - 1] : BUSTER_TYPED_CLASS_VOID;
        switch (kind)
        {
        case BUSTER_TYPED_CONST:
        case BUSTER_TYPED_LOCAL:
        case BUSTER_TYPED_GLOBAL:
        case BUSTER_TYPED_LOCAL_ADDRESS:
        case BUSTER_TYPED_GLOBAL_ADDRESS:
        {
            uint64_t loaded = kind == BUSTER_TYPED_CONST ? value : address;
            if (read)
            {
                loaded = 0;
                memcpy(&loaded, (const void*)(uintptr_t)address, tier0_class_bytes(class));
            }
            stack[top] = (InterpreterValue){
                .low = interpreter_typed_normalize(class, loaded),
            };
            classes[top++] = (uint8_t)class;
            break;
        }
        case BUSTER_TYPED_DUP:
            stack[top] = stack[top - 1];
            classes[top] = classes[top - 1];
            top += 1;
            break;
        case BUSTER_TYPED_NEG:
            stack[top - 1].low = tier0_class_floating(class) ? stack[top - 1].low ^ (class == BUSTER_TYPED_CLASS_F32 ? 0x80000000ull : 0x8000000000000000ull) : interpreter_typed_normalize(class, 0 - stack[top - 1].low);
            break;
        case BUSTER_TYPED_NOT:
            stack[top - 1].low = interpreter_typed_normalize(class, ~stack[top - 1].low);
            break;
        case BUSTER_TYPED_CONVERT:
            stack[top - 1].low = interpreter_typed_convert(class, operand, stack[top - 1].low, &result->status);
            classes[top - 1] = (uint8_t)class;
            break;
        case BUSTER_TYPED_LOAD:
            stack[top - 1].low = 0;
            memcpy(&stack[top - 1].low, (const void*)(uintptr_t)address, tier0_class_bytes(class));
            stack[top - 1].low = interpreter_typed_normalize(class, stack[top - 1].low);
            classes[top - 1] = (uint8_t)class;
            break;
        case BUSTER_TYPED_PTR_ADD:
            stack[top - 2].low += stack[top - 1].low * value;
            top -= 1;
            break;
        case BUSTER_TYPED_EQ:
        case BUSTER_TYPED_NE:
        case BUSTER_TYPED_LT:
        case BUSTER_TYPED_LE:
        case BUSTER_TYPED_GT:
        case BUSTER_TYPED_GE:
            stack[top - 2].low = interpreter_typed_compare(kind, operand, stack[top - 2].low, stack[top - 1].low);
            classes[top - 2] = BUSTER_TYPED_CLASS_I32;
            top -= 1;
            break;
        case BUSTER_TYPED_STORE:
        case BUSTER_TYPED_SET:
            memcpy((void*)(uintptr_t)address, &stack[top - 1].low, tier0_class_bytes(class));
            stack[top - 2] = stack[top - 1];
            classes[top - 2] = (uint8_t)class;
            top -= kind == BUSTER_TYPED_STORE ? 1 : 2;
            break;
        case BUSTER_TYPED_STORE_LOCAL:
        case BUSTER_TYPED_SET_LOCAL:
        case BUSTER_TYPED_STORE_GLOBAL:
        case BUSTER_TYPED_SET_GLOBAL:
            memcpy((void*)(uintptr_t)address, &stack[top - 1].low, tier0_class_bytes(class));
            top -= kind == BUSTER_TYPED_SET_LOCAL || kind == BUSTER_TYPED_SET_GLOBAL;
            break;
        case BUSTER_TYPED_CALL:
        {
            const uint32_t count = function->call_argument_counts[value];
            const uint32_t first = function->call_first[value];
            const SymbolId symbol = function->call_targets[value];
            const uint32_t callee = interpreter_typed_find_function(program, symbol);
            for (uint32_t a = 0; a < count; a += 1)
            {
                if (classes[top - count + a] != function->argument_classes[first + a])
                {
                    result->status = INTERPRETER_INVALID;
                }
            }
            if (result->status != INTERPRETER_OK)
            {
                break;
            }
            if (callee != UINT32_MAX)
            {
                const Tier0Function* target = &program->functions[callee];
                bool compatible = count == target->parameter_count;
                for (uint32_t a = 0; compatible && a < count; a += 1)
                {
                    compatible = function->argument_classes[first + a] == target->local_classes[a];
                }
                if (!compatible)
                {
                    result->status = INTERPRETER_INVALID;
                }
                else if (depth == limits.max_frames || target->local_count > limits.max_locals - local_top)
                {
                    result->status = depth == limits.max_frames ? INTERPRETER_FRAME_LIMIT : INTERPRETER_RESOURCE;
                }
                else
                {
                    result->status = interpreter_typed_enter(target, locals, defined, local_top, stack + top - count, count, &runtime, depth + 1);
                    frames[depth] = (InterpreterTypedFrame){
                        .function = callee,
                        .stack_base = top - count,
                        .local_base = local_top,
                        .return_class = (uint8_t)class,
                    };
                    top -= count;
                    local_top += target->local_count;
                    depth += 1;
                }
            }
            else
            {
                const InterpreterNativeBinding* binding = 0;
                for (uint32_t n = 0; n < program->native_count; n += 1)
                {
                    if (program->natives[n].symbol == symbol)
                    {
                        binding = &program->natives[n];
                        break;
                    }
                }
                uint16_t types[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
                uint8_t extensions[BUSTER_TIER0_MAX_CALL_ARGUMENTS];
                for (uint32_t a = 0; a < count; a += 1)
                {
                    const uint32_t argument_class = function->argument_classes[first + a];
                    types[a] = interpreter_typed_ir_type(argument_class);
                    extensions[a] = tier0_class_signed(argument_class);
                }
                const uint32_t flags = binding != 0 ? binding->flags : 0;
                const bool pointer_result = flags == BUSTER_INTERPRETER_NATIVE_ALLOCATE || flags == BUSTER_INTERPRETER_NATIVE_CALLOC || flags == BUSTER_INTERPRETER_NATIVE_REALLOCATE || flags == BUSTER_INTERPRETER_NATIVE_COPY || flags == BUSTER_INTERPRETER_NATIVE_SET;
                if (count != 0 && (flags == BUSTER_INTERPRETER_NATIVE_FREE || flags == BUSTER_INTERPRETER_NATIVE_REALLOCATE || flags == BUSTER_INTERPRETER_NATIVE_COPY || flags == BUSTER_INTERPRETER_NATIVE_SET))
                {
                    types[0] = function->argument_classes[first] == BUSTER_TYPED_CLASS_U64 ? IR_PTR : types[0];
                }
                if (count > 1 && flags == BUSTER_INTERPRETER_NATIVE_COPY)
                {
                    types[1] = function->argument_classes[first + 1] == BUSTER_TYPED_CLASS_U64 ? IR_PTR : types[1];
                }
                const InterpreterNativeRequest request = {
                    .binding = binding,
                    .types = types,
                    .extensions = extensions,
                    .arguments = stack + top - count,
                    .argument_count = count,
                    // This stream has already promoted every variadic argument, so their fixed/unnamed split is
                    // immaterial for the scalar machine classes it can express.
                    .named_count = count,
                    .result_type = pointer_result && class == BUSTER_TYPED_CLASS_U64 ? IR_PTR : interpreter_typed_ir_type(class),
                    .variadic = function->call_variadic[value] != 0,
                    .runtime = &runtime,
                };
                InterpreterValue returned = { 0 };
                if (binding == 0 || !interpreter_native_invoke(&request, &returned))
                {
                    result->status = binding == 0 ? INTERPRETER_SYMBOL : INTERPRETER_NATIVE;
                }
                else
                {
                    top -= count;
                    if (class != BUSTER_TYPED_CLASS_VOID)
                    {
                        returned.low = interpreter_typed_normalize(class, returned.low);
                        stack[top] = returned;
                        classes[top++] = (uint8_t)class;
                    }
                }
            }
            break;
        }
        case BUSTER_TYPED_DISCARD:
            top -= 1;
            break;
        case BUSTER_TYPED_BRANCH_FALSE:
        case BUSTER_TYPED_BRANCH_TRUE:
            top -= 1;
            if ((stack[top].low != 0) == (kind == BUSTER_TYPED_BRANCH_TRUE))
            {
                frame->entry = value <= function->count ? (uint32_t)value : UINT32_MAX;
            }
            break;
        case BUSTER_TYPED_JUMP:
        case BUSTER_TYPED_JUMP_VALUE:
            frame->entry = value <= function->count ? (uint32_t)value : UINT32_MAX;
            break;
        case BUSTER_TYPED_RETURN:
        {
            const InterpreterValue returned = class != BUSTER_TYPED_CLASS_VOID ? stack[top - 1] : (InterpreterValue){ 0 };
            if (depth > 1 && class != frame->return_class)
            {
                result->status = INTERPRETER_INVALID;
                break;
            }
            interpreter_memory_remove_owner(&runtime, depth);
            local_top = frame->local_base;
            top = frame->stack_base;
            depth -= 1;
            if (depth != 0 && class != BUSTER_TYPED_CLASS_VOID)
            {
                if (top == limits.max_values)
                {
                    result->status = INTERPRETER_VALUE_LIMIT;
                }
                else
                {
                    stack[top] = returned;
                    classes[top++] = (uint8_t)class;
                }
            }
            result->value = returned;
            break;
        }
        default:
            stack[top - 2].low = interpreter_typed_binary(kind, class, stack[top - 2].low, stack[top - 1].low, &result->status);
            classes[top - 2] = (uint8_t)class;
            top -= 1;
            break;
        }
    }
    interpreter_native_release(&runtime);
    arena_pop_to(scratch, start);
    const bool ok = result->status == INTERPRETER_OK;
    return ok;
}

#endif
