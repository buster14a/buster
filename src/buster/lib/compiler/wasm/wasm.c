#include <buster/lib/compiler/wasm/wasm.h>

// This file is deliberately self-contained.  The only state shared with the
// rest of the compiler is the canonical IR model; all temporary vectors and
// encoded bytes belong to the caller supplied arena.

// Linear-memory layout policy: static data starts one 64 KiB region above
// address zero, and the shadow stack gets its own 64 KiB above the data.
// The equal values are coincidence, not a shared constant.
enum
{
    WASM64_DATA_BASE = 0x10000,
    WASM64_STACK_SIZE = 0x10000,
};

typedef enum Wasm64ValType
{
    WASM64_VALTYPE_INVALID = 0,
    WASM64_VALTYPE_I32 = 0x7f,
    WASM64_VALTYPE_I64 = 0x7e,
    WASM64_VALTYPE_F32 = 0x7d,
    WASM64_VALTYPE_F64 = 0x7c,
} Wasm64ValType;

typedef struct Wasm64Buffer Wasm64Buffer;
struct Wasm64Buffer
{
    Arena* arena;
    u8* data;
    u64 length;
    u64 capacity;
};

typedef struct Wasm64Signature Wasm64Signature;
struct Wasm64Signature
{
    Wasm64ValType* params;
    u32 param_count;
    Wasm64ValType result;
    bool has_result;
    u32 type_index;
};

typedef struct Wasm64FunctionRecord Wasm64FunctionRecord;
struct Wasm64FunctionRecord
{
    IrFunction* function;
    IrSymbol* symbol;
    Wasm64Signature signature;
    u32 function_index;
    u32 defined_index;
    bool imported;
    bool exported;
};

typedef struct Wasm64DataRecord Wasm64DataRecord;
struct Wasm64DataRecord
{
    IrGlobal* global;
    IrSymbol* symbol;
    u8* bytes;
    u64 size;
    u64 offset;
    u32 module_index;
    bool has_bytes;
};

typedef struct Wasm64StringRecord Wasm64StringRecord;
struct Wasm64StringRecord
{
    IrFunction* function;
    IrInstructionId instruction;
    String8 literal;
    u64 offset;
};

typedef struct Wasm64Context Wasm64Context;
struct Wasm64Context
{
    Arena* arena;
    IrProgram* program;
    IrModule* modules;
    u32 module_count;
    Wasm64Options options;
    Wasm64Error error;
    Wasm64Stats stats;
    Wasm64Buffer data_bytes;
    Wasm64Buffer type_payload;
    Wasm64Buffer import_payload;
    Wasm64Buffer function_payload;
    Wasm64Buffer memory_payload;
    Wasm64Buffer global_payload;
    Wasm64Buffer export_payload;
    Wasm64Buffer code_payload;
    Wasm64Buffer data_payload;
    Wasm64Signature* signatures;
    u32 signature_count;
    u32 signature_capacity;
    Wasm64FunctionRecord* functions;
    u32 function_count;
    u32 function_capacity;
    Wasm64DataRecord* data_records;
    u32 data_count;
    u32 data_capacity;
    Wasm64StringRecord* strings;
    u32 string_count;
    u32 string_capacity;
    u32* symbol_function_indices;
    u32* symbol_data_indices;
    u8* symbol_seen;
    u64 data_cursor;
    u64 stack_base;
    u32 stack_global_index;
    u32 memory_export_count;
};

typedef struct Wasm64FunctionEmitter Wasm64FunctionEmitter;
struct Wasm64FunctionEmitter
{
    Wasm64Context* context;
    IrFunction* function;
    Wasm64FunctionRecord* record;
    Wasm64Buffer body;
    u32* value_locals;
    u8* value_types;
    u32* value_offsets;
    u32* parameter_locals;
    u32 pc_local;
    u32 fp_local;
    u32 sp_local;
    u32 scratch_local;
    u32 temp_base;
    u8* local_types;
    u32 local_count;
    u32 extra_local_count;
    u32 frame_size;
};

static String8 wasm64_s8(char8 const* pointer)
{
    String8 result = {0};
    result.pointer = (char8*)pointer;
    while (pointer[result.length])
    {
        result.length += 1;
    }
    return result;
}

static bool wasm64_string_equal(String8 a, String8 b)
{
    return a.length == b.length && (!a.length || memory_compare(a.pointer, b.pointer, a.length));
}

static bool wasm64_string_has_hash(String8 value, u64* hash_index)
{
    for (u64 index = 0; index < value.length; index += 1)
    {
        if (value.pointer[index] == '#')
        {
            if (hash_index)
            {
                *hash_index = index;
            }
            return true;
        }
    }
    return false;
}

static String8 wasm64_string_slice(String8 value, u64 start, u64 end)
{
    if (start > value.length)
    {
        start = value.length;
    }
    if (end > value.length)
    {
        end = value.length;
    }
    if (end < start)
    {
        end = start;
    }
    return (String8){.pointer = value.pointer + start, .length = end - start};
}

static void wasm64_buffer_init(Wasm64Buffer* buffer, Arena* arena)
{
    *buffer = (Wasm64Buffer){.arena = arena};
}

static void wasm64_buffer_reserve(Wasm64Buffer* buffer, u64 additional)
{
    if (additional <= buffer->capacity - buffer->length)
    {
        return;
    }
    u64 required = buffer->length + additional;
    u64 capacity = buffer->capacity ? buffer->capacity : 256;
    while (capacity < required)
    {
        u64 next = capacity * 2;
        if (next < capacity || next > ARENA_MAX_RESERVATION)
        {
            capacity = required;
            break;
        }
        capacity = next;
    }
    u8* data = (u8*)arena_allocate_bytes(buffer->arena, capacity, 1);
    if (buffer->length)
    {
        memcpy(data, buffer->data, (size_t)buffer->length);
    }
    buffer->data = data;
    buffer->capacity = capacity;
}

static void wasm64_buffer_u8(Wasm64Buffer* buffer, u8 value)
{
    wasm64_buffer_reserve(buffer, 1);
    buffer->data[buffer->length] = value;
    buffer->length += 1;
}

static void wasm64_buffer_bytes(Wasm64Buffer* buffer, u8 const* data, u64 length)
{
    if (!length)
    {
        return;
    }
    wasm64_buffer_reserve(buffer, length);
    memcpy(buffer->data + buffer->length, data, (size_t)length);
    buffer->length += length;
}

static void wasm64_buffer_u32_leb(Wasm64Buffer* buffer, u32 value)
{
    do
    {
        u8 byte = (u8)(value & 0x7f);
        value >>= 7;
        if (value)
        {
            byte |= 0x80;
        }
        wasm64_buffer_u8(buffer, byte);
    } while (value);
}

static void wasm64_buffer_u64_leb(Wasm64Buffer* buffer, u64 value)
{
    do
    {
        u8 byte = (u8)(value & 0x7f);
        value >>= 7;
        if (value)
        {
            byte |= 0x80;
        }
        wasm64_buffer_u8(buffer, byte);
    } while (value);
}

static void wasm64_buffer_s32_leb(Wasm64Buffer* buffer, s32 value)
{
    bool more = true;
    while (more)
    {
        u8 byte = (u8)(value & 0x7f);
        value >>= 7;
        bool sign = (byte & 0x40) != 0;
        more = !((value == 0 && !sign) || (value == -1 && sign));
        if (more)
        {
            byte |= 0x80;
        }
        wasm64_buffer_u8(buffer, byte);
    }
}

static void wasm64_buffer_s64_leb(Wasm64Buffer* buffer, s64 value)
{
    bool more = true;
    while (more)
    {
        u8 byte = (u8)(value & 0x7f);
        value >>= 7;
        bool sign = (byte & 0x40) != 0;
        more = !((value == 0 && !sign) || (value == -1 && sign));
        if (more)
        {
            byte |= 0x80;
        }
        wasm64_buffer_u8(buffer, byte);
    }
}

static void wasm64_buffer_string(Wasm64Buffer* buffer, String8 value)
{
    // Wasm names are u32-length byte strings.  Rejecting a >4 GiB name is
    // handled by the caller's bounded source arenas; the cast is safe for all
    // legal compiler strings.
    wasm64_buffer_u32_leb(buffer, (u32)value.length);
    wasm64_buffer_bytes(buffer, (u8*)value.pointer, value.length);
}

static void wasm64_buffer_f32(Wasm64Buffer* buffer, u32 bits)
{
    wasm64_buffer_u8(buffer, (u8)(bits & 0xff));
    wasm64_buffer_u8(buffer, (u8)((bits >> 8) & 0xff));
    wasm64_buffer_u8(buffer, (u8)((bits >> 16) & 0xff));
    wasm64_buffer_u8(buffer, (u8)((bits >> 24) & 0xff));
}

static void wasm64_buffer_f64(Wasm64Buffer* buffer, u64 bits)
{
    for (u32 byte_index = 0; byte_index < 8; byte_index += 1)
    {
        wasm64_buffer_u8(buffer, (u8)(bits >> (byte_index * 8)));
    }
}

static void wasm64_buffer_u64_fixed(Wasm64Buffer* buffer, u64 value, u32 byte_count)
{
    for (u32 byte_index = 0; byte_index < byte_count; byte_index += 1)
    {
        wasm64_buffer_u8(buffer, (u8)(value >> (byte_index * 8)));
    }
}

static void wasm64_align_cursor(u64* cursor, u64 alignment)
{
    if (alignment > 1)
    {
        u64 mask = alignment - 1;
        if (*cursor <= UINT64_MAX - mask)
        {
            *cursor = (*cursor + mask) & ~mask;
        }
    }
}

static void wasm64_fail(Wasm64Context* context, Wasm64ErrorCode code, String8 message, IrFunction* function, IrBlock* block,
                        IrInstruction* instruction, IrSymbolId symbol)
{
    if (context->error.code != WASM64_ERROR_NONE)
    {
        return;
    }
    context->error.code = code;
    context->error.message = message;
    context->error.diagnostic = message;
    context->error.function = function ? function->id : IR_FUNCTION_ID_INVALID;
    context->error.block = block ? block->id : IR_BLOCK_ID_INVALID;
    context->error.instruction = instruction && function ? ir_instruction_self_id(function, instruction) : IR_INSTRUCTION_ID_INVALID;
    context->error.symbol = symbol;
    context->error.opcode = instruction ? instruction->opcode : IR_OPCODE_COUNT;
}

static bool wasm64_failed(Wasm64Context* context)
{
    return context->error.code != WASM64_ERROR_NONE;
}

static void wasm64_vec_reserve(Arena* arena, void** pointer, u32* capacity, u32 count, u32 element_size)
{
    if (count <= *capacity)
    {
        return;
    }
    u32 new_capacity = *capacity ? *capacity : 16;
    while (new_capacity < count)
    {
        u32 next = new_capacity * 2;
        if (next < new_capacity || next > UINT32_MAX / element_size)
        {
            new_capacity = count;
            break;
        }
        new_capacity = next;
    }
    void* next = arena_allocate_bytes(arena, (u64)new_capacity * element_size, BUSTER_ALIGN_OF(u64));
    if (*pointer && *capacity)
    {
        memcpy(next, *pointer, (size_t)((u64)*capacity * element_size));
    }
    *pointer = next;
    *capacity = new_capacity;
}

static IrType* wasm64_type(Wasm64Context* context, IrTypeId id)
{
    return ir_type_from_id(&context->program->types, id);
}

static IrSymbol* wasm64_symbol(Wasm64Context* context, IrSymbolId id)
{
    return ir_symbol_from_id(&context->program->symbols, id);
}

static bool wasm64_type_is_scalar(IrType* type)
{
    return type && (type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_FLOAT || type->kind == IR_TYPE_POINTER ||
                    type->kind == IR_TYPE_ENUM);
}

static bool wasm64_type_is_pointer(IrType* type)
{
    return type && type->kind == IR_TYPE_POINTER;
}

static u32 wasm64_integer_bits(IrType* type)
{
    u32 result;
    if (!type)
    {
        result = 0;
    }
    else if (type->kind == IR_TYPE_BOOLEAN)
    {
        result = 1;
    }
    else if (type->kind == IR_TYPE_ENUM)
    {
        result = type->layout.size <= 4 ? 32 : 64;
    }
    else
    {
        result = type->bit_width;
    }

    return result;
}

static bool wasm64_valtype_for_type(IrType* type, bool place, Wasm64ValType* result)
{
    if (place || wasm64_type_is_pointer(type))
    {
        if (result)
        {
            *result = WASM64_VALTYPE_I64;
        }
        return true;
    }
    if (!type)
    {
        return false;
    }
    switch (type->kind)
    {
    case IR_TYPE_BOOLEAN:
    case IR_TYPE_INTEGER:
        if (!type->bit_width || type->bit_width <= 32)
        {
            if (result)
            {
                *result = WASM64_VALTYPE_I32;
            }
            return true;
        }
        if (type->bit_width <= 64)
        {
            if (result)
            {
                *result = WASM64_VALTYPE_I64;
            }
            return true;
        }
        return false;
    case IR_TYPE_ENUM:
        if (type->layout.size <= 4)
        {
            if (result)
            {
                *result = WASM64_VALTYPE_I32;
            }
            return true;
        }
        if (type->layout.size <= 8)
        {
            if (result)
            {
                *result = WASM64_VALTYPE_I64;
            }
            return true;
        }
        return false;
    case IR_TYPE_FLOAT:
        if (type->bit_width == 32)
        {
            if (result)
            {
                *result = WASM64_VALTYPE_F32;
            }
            return true;
        }
        if (type->bit_width == 64)
        {
            if (result)
            {
                *result = WASM64_VALTYPE_F64;
            }
            return true;
        }
        return false;
    default:
        return false;
    }
}

static bool wasm64_type_is_integer(IrType* type)
{
    return type && (type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_ENUM);
}

static bool wasm64_type_is_float(IrType* type)
{
    return type && type->kind == IR_TYPE_FLOAT;
}

static u32 wasm64_type_size(IrType* type)
{
    return type && type->layout.resolved && type->layout.size <= UINT32_MAX ? (u32)type->layout.size : 0;
}

static u32 wasm64_log2_alignment(u32 alignment)
{
    u32 result = 0;
    while (alignment > 1)
    {
        alignment >>= 1;
        result += 1;
    }
    return result;
}

static bool wasm64_signature_equal(Wasm64Signature* a, Wasm64Signature* b)
{
    if (a->param_count != b->param_count || a->has_result != b->has_result || (a->has_result && a->result != b->result))
    {
        return false;
    }
    for (u32 index = 0; index < a->param_count; index += 1)
    {
        if (a->params[index] != b->params[index])
        {
            return false;
        }
    }
    return true;
}

static u32 wasm64_signature_add(Wasm64Context* context, Wasm64Signature signature)
{
    for (u32 index = 0; index < context->signature_count; index += 1)
    {
        if (wasm64_signature_equal(context->signatures + index, &signature))
        {
            return context->signatures[index].type_index;
        }
    }
    wasm64_vec_reserve(context->arena, (void**)&context->signatures, &context->signature_capacity, context->signature_count + 1,
                       sizeof(*context->signatures));
    signature.type_index = context->signature_count;
    context->signatures[context->signature_count] = signature;
    context->signature_count += 1;
    return signature.type_index;
}

static bool wasm64_function_signature(Wasm64Context* context, IrTypeId function_type_id, Wasm64Signature* result, IrFunction* function,
                                      IrSymbolId symbol)
{
    IrType* function_type = wasm64_type(context, function_type_id);
    if (!function_type || function_type->kind != IR_TYPE_FUNCTION)
    {
        wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_TYPE, wasm64_s8("Wasm function signature is not canonical"), function, 0, 0, symbol);
        return false;
    }
    if (function_type->is_variadic)
    {
        wasm64_fail(context, WASM64_ERROR_VARIADIC, wasm64_s8("variadic functions are not supported by Wasm64"), function, 0, 0, symbol);
        return false;
    }
    Wasm64Signature signature = {0};
    signature.param_count = function_type->parameter_count;
    if (signature.param_count)
    {
        signature.params = arena_allocate(context->arena, Wasm64ValType, signature.param_count);
    }
    for (u32 index = 0; index < signature.param_count; index += 1)
    {
        IrType* parameter_type = wasm64_type(context, function_type->parameter_types[index]);
        if (!wasm64_valtype_for_type(parameter_type, false, signature.params + index))
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI, wasm64_s8("aggregate or unsupported parameter ABI in Wasm64 function"),
                        function, 0, 0, symbol);
            return false;
        }
    }
    IrType* return_type = wasm64_type(context, function_type->return_type);
    if (!return_type)
    {
        wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_TYPE, wasm64_s8("missing Wasm64 return type"), function, 0, 0, symbol);
        return false;
    }
    if (return_type->kind != IR_TYPE_VOID)
    {
        if (!wasm64_valtype_for_type(return_type, false, &signature.result))
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI, wasm64_s8("aggregate or unsupported result ABI in Wasm64 function"),
                        function, 0, 0, symbol);
            return false;
        }
        signature.has_result = true;
    }
    *result = signature;
    return true;
}

static IrTypeId wasm64_function_type_from_symbol(Wasm64Context* context, IrSymbol* symbol)
{
    IrType* symbol_type = symbol ? wasm64_type(context, symbol->type) : 0;
    if (symbol_type && symbol_type->kind == IR_TYPE_POINTER)
    {
        symbol_type = wasm64_type(context, symbol_type->element_type);
    }
    return symbol_type && symbol_type->kind == IR_TYPE_FUNCTION ? symbol_type->id : IR_TYPE_ID_INVALID;
}

static bool wasm64_symbol_is_function_definition(Wasm64Context* context, IrSymbolId symbol_id, IrFunction** function_out, u32* module_out)
{
    if (context->program && symbol_id.value < context->program->symbols.count)
    {
        for (u32 module_index = 0; module_index < context->module_count; module_index += 1)
        {
            IrModule* module = context->modules + module_index;
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                if (function->symbol.value == symbol_id.value && function->state == IR_FUNCTION_LOWERED)
                {
                    if (function_out)
                    {
                        *function_out = function;
                    }
                    if (module_out)
                    {
                        *module_out = module_index;
                    }
                    return true;
                }
            }
        }
    }

    return false;
}

static String8 wasm64_import_module(String8 link_name)
{
    u64 hash = 0;
    if (wasm64_string_has_hash(link_name, &hash))
    {
        return wasm64_string_slice(link_name, 0, hash);
    }
    return wasm64_s8("env");
}

static String8 wasm64_import_name(String8 link_name, String8 fallback)
{
    u64 hash = 0;
    if (wasm64_string_has_hash(link_name, &hash))
    {
        return wasm64_string_slice(link_name, hash + 1, link_name.length);
    }
    return link_name.length ? link_name : fallback;
}

static Wasm64FunctionRecord* wasm64_function_record_for_symbol(Wasm64Context* context, IrSymbolId symbol)
{
    Wasm64FunctionRecord* result;
    if (symbol.value == IR_ID_UNDERLYING_INVALID || symbol.value >= context->program->symbols.count || !context->symbol_function_indices)
    {
        result = 0;
    }
    else
    {
        u32 index = context->symbol_function_indices[symbol.value];
        result = index == UINT32_MAX || index >= context->function_count ? 0 : context->functions + index;
    }

    return result;
}

static Wasm64DataRecord* wasm64_data_record_for_symbol(Wasm64Context* context, IrSymbolId symbol)
{
    Wasm64DataRecord* result;
    if (symbol.value == IR_ID_UNDERLYING_INVALID || symbol.value >= context->program->symbols.count || !context->symbol_data_indices)
    {
        result = 0;
    }
    else
    {
        u32 index = context->symbol_data_indices[symbol.value];
        result = index == UINT32_MAX || index >= context->data_count ? 0 : context->data_records + index;
    }

    return result;
}

static bool wasm64_add_function_record(Wasm64Context* context, IrFunction* function, IrSymbol* symbol, bool imported)
{
    Wasm64Signature signature = {0};
    IrTypeId type_id = function ? function->canonical_type : wasm64_function_type_from_symbol(context, symbol);
    if (type_id.value == IR_ID_UNDERLYING_INVALID || !wasm64_function_signature(context, type_id, &signature, function, symbol ? symbol->id : IR_SYMBOL_ID_INVALID))
    {
        return false;
    }
    signature.type_index = wasm64_signature_add(context, signature);
    wasm64_vec_reserve(context->arena, (void**)&context->functions, &context->function_capacity, context->function_count + 1,
                       sizeof(*context->functions));
    Wasm64FunctionRecord* record = context->functions + context->function_count;
    *record = (Wasm64FunctionRecord){.function = function, .symbol = symbol, .signature = signature, .imported = imported, .defined_index = UINT32_MAX};
    record->function_index = context->function_count;
    record->exported = !imported && context->options.export_functions && symbol && symbol->linkage != IR_LINKAGE_INTERNAL;
    if (symbol && symbol->id.value < context->program->symbols.count)
    {
        if (context->symbol_function_indices[symbol->id.value] != UINT32_MAX)
        {
            Wasm64FunctionRecord* previous = context->functions + context->symbol_function_indices[symbol->id.value];
            if (previous->function != function || previous->imported != imported || !wasm64_signature_equal(&previous->signature, &signature))
            {
                wasm64_fail(context, WASM64_ERROR_DUPLICATE_SYMBOL, wasm64_s8("duplicate or incompatible Wasm function symbol"), function, 0, 0, symbol->id);
                return false;
            }
            return true;
        }
        context->symbol_function_indices[symbol->id.value] = context->function_count;
    }
    context->function_count += 1;
    return true;
}

static bool wasm64_symbol_is_called(Wasm64Context* context, IrSymbolId symbol_id)
{
    for (u32 module_index = 0; module_index < context->module_count; module_index += 1)
    {
        IrModule* module = context->modules + module_index;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                if (instruction->opcode == IR_OPCODE_CALL && instruction->symbol.value == symbol_id.value)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool wasm64_collect_functions(Wasm64Context* context)
{
    u32 symbol_count = context->program->symbols.count;
    context->symbol_function_indices = arena_allocate(context->arena, u32, symbol_count ? symbol_count : 1);
    context->symbol_seen = arena_allocate(context->arena, u8, symbol_count ? symbol_count : 1);
    for (u32 symbol_index = 0; symbol_index < symbol_count; symbol_index += 1)
    {
        context->symbol_function_indices[symbol_index] = UINT32_MAX;
        context->symbol_seen[symbol_index] = 0;
    }

    // Imports occupy the first function indices.  Walk symbol ids, which are
    // stable across module emission, and retain only symbols used by the IR or
    // explicitly declared as imports/externals.
    for (u32 symbol_index = 0; symbol_index < symbol_count; symbol_index += 1)
    {
        IrSymbol* symbol = context->program->symbols.symbols + symbol_index;
        if (symbol->kind != IR_SYMBOL_FUNCTION || wasm64_symbol_is_function_definition(context, symbol->id, 0, 0))
        {
            continue;
        }
        bool needed = symbol->linkage == IR_LINKAGE_IMPORT || symbol->linkage == IR_LINKAGE_EXTERNAL || wasm64_symbol_is_called(context, symbol->id);
        if (!needed)
        {
            continue;
        }
        if (!wasm64_add_function_record(context, 0, symbol, true))
        {
            return false;
        }
    }

    for (u32 module_index = 0; module_index < context->module_count; module_index += 1)
    {
        IrModule* module = context->modules + module_index;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            if (function->state == IR_FUNCTION_DECLARATION)
            {
                continue;
            }
            if (function->state != IR_FUNCTION_LOWERED)
            {
                if (function->state == IR_FUNCTION_REJECTED)
                {
                    wasm64_fail(context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("IR function was rejected before Wasm64 emission"), function, 0, 0,
                                function->symbol);
                    return false;
                }
                continue;
            }
            IrSymbol* symbol = wasm64_symbol(context, function->symbol);
            if (!symbol || symbol->kind != IR_SYMBOL_FUNCTION)
            {
                wasm64_fail(context, WASM64_ERROR_INVALID_ARGUMENT, wasm64_s8("lowered Wasm64 function has no function symbol"), function, 0, 0,
                            function->symbol);
                return false;
            }
            if (!wasm64_add_function_record(context, function, symbol, false))
            {
                return false;
            }
        }
    }
    context->stats.function_count = context->function_count;
    context->stats.import_count = 0;
    context->stats.defined_function_count = 0;
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        Wasm64FunctionRecord* record = context->functions + index;
        if (record->imported)
        {
            context->stats.import_count += 1;
        }
        else
        {
            record->defined_index = context->stats.defined_function_count;
            context->stats.defined_function_count += 1;
        }
    }
    return !wasm64_failed(context);
}

static bool wasm64_global_initializer_bytes(Wasm64Context* context, IrGlobal* global, IrType* type, u8** bytes_out, u64* size_out)
{
    u64 size = type && type->layout.resolved ? type->layout.size : 0;
    if (size > ARENA_MAX_RESERVATION)
    {
        wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_TYPE, wasm64_s8("Wasm64 global is too large"), 0, 0, 0, global->symbol);
        return false;
    }
    u8* bytes = size ? arena_allocate(context->arena, u8, size) : 0;
    if (size)
    {
        memset(bytes, 0, (size_t)size);
    }
    switch (global->initializer_kind)
    {
    case IR_GLOBAL_INITIALIZER_ZERO:
        break;
    case IR_GLOBAL_INITIALIZER_BYTES:
        if (global->bytes.length != size || (size && !global->bytes.pointer))
        {
            wasm64_fail(context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("invalid Wasm64 global byte initializer"), 0, 0, 0, global->symbol);
            return false;
        }
        if (size)
        {
            memcpy(bytes, global->bytes.pointer, (size_t)size);
        }
        break;
    case IR_GLOBAL_INITIALIZER_INTEGER:
    case IR_GLOBAL_INITIALIZER_FLOAT:
        if (size > sizeof(u64))
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI, wasm64_s8("wide global initializer ABI is unsupported by Wasm64"), 0, 0, 0,
                        global->symbol);
            return false;
        }
        {
            u64 bits = global->initializer_bits;
            if (global->initializer_is_negative)
            {
                bits = 0 - bits;
            }
            wasm64_buffer_u64_fixed(&(Wasm64Buffer){.arena = context->arena, .data = bytes, .capacity = size, .length = 0}, bits, (u32)size);
        }
        break;
    case IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS:
        if (size < 8)
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_TYPE, wasm64_s8("pointer global is smaller than a Memory64 address"), 0, 0, 0,
                        global->symbol);
            return false;
        }
        break;
    case IR_GLOBAL_INITIALIZER_NONE:
    case IR_GLOBAL_INITIALIZER_COUNT:
        wasm64_fail(context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("missing Wasm64 global initializer"), 0, 0, 0, global->symbol);
        return false;
    }
    *bytes_out = bytes;
    *size_out = size;
    return true;
}

static bool wasm64_data_add_global(Wasm64Context* context, IrGlobal* global, u32 module_index)
{
    IrSymbol* symbol = wasm64_symbol(context, global->symbol);
    IrType* type = wasm64_type(context, global->type);
    if (!symbol || symbol->kind != IR_SYMBOL_DATA || !type || !type->layout.resolved || symbol->id.value >= context->program->symbols.count)
    {
        wasm64_fail(context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("invalid Wasm64 global symbol or type"), 0, 0, 0, global->symbol);
        return false;
    }
    if (symbol->is_thread_local || global->is_thread_local)
    {
        wasm64_fail(context, WASM64_ERROR_TLS, wasm64_s8("TLS globals are not supported by Wasm64"), 0, 0, 0, global->symbol);
        return false;
    }
    if (context->symbol_data_indices[global->symbol.value] != UINT32_MAX)
    {
        wasm64_fail(context, WASM64_ERROR_DUPLICATE_SYMBOL, wasm64_s8("duplicate Wasm64 data symbol"), 0, 0, 0, global->symbol);
        return false;
    }
    u8* bytes = 0;
    u64 size = 0;
    if (!wasm64_global_initializer_bytes(context, global, type, &bytes, &size))
    {
        return false;
    }
    u64 alignment = global->alignment ? global->alignment : type->layout.alignment;
    if (!alignment || (alignment & (alignment - 1)))
    {
        alignment = 1;
    }
    wasm64_align_cursor(&context->data_cursor, alignment);
    Wasm64DataRecord record = {.global = global, .symbol = symbol, .bytes = bytes, .size = size, .offset = context->data_cursor, .module_index = module_index,
                               .has_bytes = false};
    if (size && (global->initializer_kind != IR_GLOBAL_INITIALIZER_ZERO || global->relocation_count))
    {
        record.has_bytes = true;
    }
    context->data_cursor += size;
    wasm64_vec_reserve(context->arena, (void**)&context->data_records, &context->data_capacity, context->data_count + 1, sizeof(*context->data_records));
    context->data_records[context->data_count] = record;
    context->symbol_data_indices[global->symbol.value] = context->data_count;
    context->data_count += 1;
    return true;
}

static bool wasm64_collect_data(Wasm64Context* context)
{
    u32 symbol_count = context->program->symbols.count;
    context->symbol_data_indices = arena_allocate(context->arena, u32, symbol_count ? symbol_count : 1);
    for (u32 symbol_index = 0; symbol_index < symbol_count; symbol_index += 1)
    {
        context->symbol_data_indices[symbol_index] = UINT32_MAX;
    }
    context->data_cursor = WASM64_DATA_BASE;
    for (u32 module_index = 0; module_index < context->module_count; module_index += 1)
    {
        IrModule* module = context->modules + module_index;
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            if (!wasm64_data_add_global(context, module->globals + global_index, module_index))
            {
                return false;
            }
        }
    }
    // String literals are static read-only bytes.  They receive deterministic
    // source-order offsets and a trailing NUL, which also makes C string
    // constants safe when handed to an imported function.
    for (u32 module_index = 0; module_index < context->module_count; module_index += 1)
    {
        IrModule* module = context->modules + module_index;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                if (instruction->opcode != IR_OPCODE_CONSTANT_STRING)
                {
                    continue;
                }
                IrType* type = wasm64_type(context, instruction->canonical_type);
                if (!type || type->kind != IR_TYPE_POINTER)
                {
                    wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI, wasm64_s8("string aggregate ABI is unsupported by Wasm64"), function,
                                0, instruction, IR_SYMBOL_ID_INVALID);
                    return false;
                }
                String8 literal = ir_instruction_extra(function, ir_instruction_self_id(function, instruction)).literal;
                wasm64_align_cursor(&context->data_cursor, 1);
                Wasm64StringRecord record = {.function = function, .instruction = ir_instruction_self_id(function, instruction), .literal = literal, .offset = context->data_cursor};
                context->data_cursor += literal.length + 1;
                wasm64_vec_reserve(context->arena, (void**)&context->strings, &context->string_capacity, context->string_count + 1, sizeof(*context->strings));
                context->strings[context->string_count] = record;
                context->string_count += 1;
            }
        }
    }
    wasm64_align_cursor(&context->data_cursor, 16);
    context->stack_base = context->data_cursor + WASM64_STACK_SIZE;
    if (context->stack_base < context->data_cursor)
    {
        wasm64_fail(context, WASM64_ERROR_ENCODING, wasm64_s8("Wasm64 memory layout overflow"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return false;
    }
    context->stats.static_data_bytes = context->data_cursor;
    return true;
}

static bool wasm64_apply_data_relocation(Wasm64Context* context, Wasm64DataRecord* record, IrSymbolId symbol, u64 offset, s64 addend,
                                         bool is_label_address)
{
    u32 const pointer_size = 8;
    if (is_label_address)
    {
        wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_INSTRUCTION, wasm64_s8("label-address relocations are unsupported by Wasm64"), 0, 0, 0, symbol);
        return false;
    }
    Wasm64DataRecord* target = wasm64_data_record_for_symbol(context, symbol);
    if (!target)
    {
        IrSymbol* target_symbol = wasm64_symbol(context, symbol);
        if (target_symbol && target_symbol->kind == IR_SYMBOL_FUNCTION)
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_INSTRUCTION, wasm64_s8("function-address relocations are unsupported by Wasm64"), 0, 0, 0,
                        symbol);
        }
        else
        {
            wasm64_fail(context, WASM64_ERROR_UNRESOLVED_SYMBOL, wasm64_s8("unresolved Wasm64 data relocation"), 0, 0, 0, symbol);
        }
        return false;
    }
    if (!record->bytes || offset > record->size || pointer_size > record->size - offset)
    {
        wasm64_fail(context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("invalid Wasm64 data relocation offset"), 0, 0, 0, record->symbol->id);
        return false;
    }
    u64 value = target->offset;
    if (addend >= 0)
    {
        if (value > UINT64_MAX - (u64)addend)
        {
            wasm64_fail(context, WASM64_ERROR_ENCODING, wasm64_s8("Wasm64 data relocation overflow"), 0, 0, 0, symbol);
            return false;
        }
        value += (u64)addend;
    }
    else
    {
        u64 magnitude = (u64)(-(addend + 1)) + 1;
        if (value < magnitude)
        {
            wasm64_fail(context, WASM64_ERROR_ENCODING, wasm64_s8("Wasm64 data relocation underflow"), 0, 0, 0, symbol);
            return false;
        }
        value -= magnitude;
    }
    wasm64_buffer_u64_fixed(&(Wasm64Buffer){.arena = context->arena, .data = record->bytes + offset, .capacity = pointer_size, .length = 0}, value,
                            pointer_size);
    record->has_bytes = true;
    return true;
}

static bool wasm64_apply_data_relocations(Wasm64Context* context)
{
    for (u32 record_index = 0; record_index < context->data_count; record_index += 1)
    {
        Wasm64DataRecord* record = context->data_records + record_index;
        IrGlobal* global = record->global;
        if (global->initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS &&
            !wasm64_apply_data_relocation(context, record, global->initializer_symbol, 0, global->initializer_addend, false))
        {
            return false;
        }
        for (u32 relocation_index = 0; relocation_index < global->relocation_count; relocation_index += 1)
        {
            IrGlobalRelocation* relocation = global->relocations + relocation_index;
            if (!wasm64_apply_data_relocation(context, record, relocation->symbol, relocation->offset, relocation->addend,
                                              relocation->is_label_address))
            {
                return false;
            }
        }
    }
    return true;
}

static bool wasm64_build_type_payload(Wasm64Context* context)
{
    wasm64_buffer_u32_leb(&context->type_payload, context->signature_count);
    for (u32 index = 0; index < context->signature_count; index += 1)
    {
        Wasm64Signature* signature = context->signatures + index;
        wasm64_buffer_u8(&context->type_payload, 0x60);
        wasm64_buffer_u32_leb(&context->type_payload, signature->param_count);
        for (u32 parameter_index = 0; parameter_index < signature->param_count; parameter_index += 1)
        {
            wasm64_buffer_u8(&context->type_payload, (u8)signature->params[parameter_index]);
        }
        wasm64_buffer_u32_leb(&context->type_payload, signature->has_result ? 1 : 0);
        if (signature->has_result)
        {
            wasm64_buffer_u8(&context->type_payload, (u8)signature->result);
        }
    }
    context->stats.type_count = context->signature_count;
    return true;
}

static bool wasm64_build_import_payload(Wasm64Context* context)
{
    u32 import_count = context->stats.import_count;
    wasm64_buffer_u32_leb(&context->import_payload, import_count);
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        Wasm64FunctionRecord* record = context->functions + index;
        if (!record->imported)
        {
            continue;
        }
        IrSymbol* symbol = record->symbol;
        String8 link_name = symbol && symbol->link_name.length ? symbol->link_name : symbol ? symbol->name : (String8){0};
        String8 module_name = wasm64_import_module(link_name);
        String8 import_name = wasm64_import_name(link_name, symbol ? symbol->name : (String8){0});
        wasm64_buffer_string(&context->import_payload, module_name);
        wasm64_buffer_string(&context->import_payload, import_name);
        wasm64_buffer_u8(&context->import_payload, 0x00); // function import
        wasm64_buffer_u32_leb(&context->import_payload, record->signature.type_index);
    }
    return true;
}

static bool wasm64_build_function_payload(Wasm64Context* context)
{
    wasm64_buffer_u32_leb(&context->function_payload, context->stats.defined_function_count);
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        Wasm64FunctionRecord* record = context->functions + index;
        if (!record->imported)
        {
            wasm64_buffer_u32_leb(&context->function_payload, record->signature.type_index);
        }
    }
    return true;
}

static u64 wasm64_pages_for_bytes(u64 bytes)
{
    u64 page_size = 65536;
    return bytes > UINT64_MAX - (page_size - 1) ? UINT64_MAX / page_size : (bytes + page_size - 1) / page_size;
}

static bool wasm64_build_memory_payload(Wasm64Context* context)
{
    u64 minimum = wasm64_pages_for_bytes(context->stack_base + 1);
    if (context->options.initial_pages > minimum)
    {
        minimum = context->options.initial_pages;
    }
    u64 maximum = context->options.maximum_pages;
    if (maximum && maximum < minimum)
    {
        wasm64_fail(context, WASM64_ERROR_ENCODING, wasm64_s8("Wasm64 memory maximum is smaller than its minimum"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return false;
    }
    context->stats.memory64 = true;
    context->stats.memory_min_pages = minimum;
    context->stats.memory_max_pages = maximum;
    wasm64_buffer_u32_leb(&context->memory_payload, 1);
    wasm64_buffer_u8(&context->memory_payload, maximum ? 0x05 : 0x04); // maximum + memory64, or memory64 alone
    wasm64_buffer_u64_leb(&context->memory_payload, minimum);
    if (maximum)
    {
        wasm64_buffer_u64_leb(&context->memory_payload, maximum);
    }
    return true;
}

static String8 wasm64_function_export_name(Wasm64FunctionRecord* record)
{
    if (!record->symbol)
    {
        return (String8){0};
    }
    String8 link_name = record->symbol->link_name.length ? record->symbol->link_name : record->symbol->name;
    u64 hash = 0;
    if (wasm64_string_has_hash(link_name, &hash))
    {
        return wasm64_string_slice(link_name, hash + 1, link_name.length);
    }
    return link_name;
}

static bool wasm64_build_global_payload(Wasm64Context* context)
{
    // One mutable i64 stack pointer.  Every generated function saves/restores
    // it around its linear-memory frame, making nested direct calls safe.
    wasm64_buffer_u32_leb(&context->global_payload, 1);
    wasm64_buffer_u8(&context->global_payload, WASM64_VALTYPE_I64);
    wasm64_buffer_u8(&context->global_payload, 0x01); // mutable
    wasm64_buffer_u8(&context->global_payload, 0x42);
    wasm64_buffer_s64_leb(&context->global_payload, (s64)context->stack_base);
    wasm64_buffer_u8(&context->global_payload, 0x0b);
    context->stack_global_index = 0;
    context->stats.global_count = 1;
    return true;
}

static bool wasm64_build_export_payload(Wasm64Context* context)
{
    String8 memory_name = context->options.memory_export_name.length ? context->options.memory_export_name : wasm64_s8("memory");
    u32 export_count = context->options.export_memory ? 1 : 0;
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        Wasm64FunctionRecord* record = context->functions + index;
        if (record->exported)
        {
            export_count += 1;
        }
    }
    wasm64_buffer_u32_leb(&context->export_payload, export_count);
    if (context->options.export_memory)
    {
        wasm64_buffer_string(&context->export_payload, memory_name);
        wasm64_buffer_u8(&context->export_payload, 0x02); // memory
        wasm64_buffer_u32_leb(&context->export_payload, 0);
    }
    u32 emitted = 0;
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        Wasm64FunctionRecord* record = context->functions + index;
        if (!record->exported)
        {
            continue;
        }
        String8 name = wasm64_function_export_name(record);
        if (!name.length || (context->options.export_memory && wasm64_string_equal(name, memory_name)))
        {
            wasm64_fail(context, WASM64_ERROR_DUPLICATE_SYMBOL, wasm64_s8("empty or reserved Wasm64 export name"), record->function, 0, 0,
                        record->symbol ? record->symbol->id : IR_SYMBOL_ID_INVALID);
            return false;
        }
        // Export names must be unique.  A duplicate is an error rather than a
        // silently replaced entry, which would make link order observable.
        for (u32 previous = 0; previous < index; previous += 1)
        {
            Wasm64FunctionRecord* prior = context->functions + previous;
            if (prior->exported && wasm64_string_equal(name, wasm64_function_export_name(prior)))
            {
                wasm64_fail(context, WASM64_ERROR_DUPLICATE_SYMBOL, wasm64_s8("duplicate Wasm64 export name"), record->function, 0, 0,
                            record->symbol ? record->symbol->id : IR_SYMBOL_ID_INVALID);
                return false;
            }
        }
        wasm64_buffer_string(&context->export_payload, name);
        wasm64_buffer_u8(&context->export_payload, 0x00); // function
        wasm64_buffer_u32_leb(&context->export_payload, record->function_index);
        emitted += 1;
    }
    context->stats.export_count = emitted + (context->options.export_memory ? 1 : 0);
    return true;
}

static void wasm64_fe_u8(Wasm64FunctionEmitter* emitter, u8 value)
{
    wasm64_buffer_u8(&emitter->body, value);
}

static void wasm64_fe_u32(Wasm64FunctionEmitter* emitter, u32 value)
{
    wasm64_buffer_u32_leb(&emitter->body, value);
}

static void wasm64_fe_i64(Wasm64FunctionEmitter* emitter, s64 value)
{
    wasm64_buffer_s64_leb(&emitter->body, value);
}

static void wasm64_fe_i32(Wasm64FunctionEmitter* emitter, s32 value)
{
    wasm64_buffer_s32_leb(&emitter->body, value);
}

static void wasm64_fe_local_get(Wasm64FunctionEmitter* emitter, u32 local)
{
    wasm64_fe_u8(emitter, 0x20);
    wasm64_fe_u32(emitter, local);
}

static void wasm64_fe_local_set(Wasm64FunctionEmitter* emitter, u32 local)
{
    wasm64_fe_u8(emitter, 0x21);
    wasm64_fe_u32(emitter, local);
}

static void wasm64_fe_local_tee(Wasm64FunctionEmitter* emitter, u32 local)
{
    wasm64_fe_u8(emitter, 0x22);
    wasm64_fe_u32(emitter, local);
}

static void wasm64_fe_i32_const(Wasm64FunctionEmitter* emitter, s32 value)
{
    wasm64_fe_u8(emitter, 0x41);
    wasm64_fe_i32(emitter, value);
}

static void wasm64_fe_i64_const(Wasm64FunctionEmitter* emitter, s64 value)
{
    wasm64_fe_u8(emitter, 0x42);
    wasm64_fe_i64(emitter, value);
}

static void wasm64_fe_f32_const(Wasm64FunctionEmitter* emitter, u32 bits)
{
    wasm64_fe_u8(emitter, 0x43);
    wasm64_buffer_f32(&emitter->body, bits);
}

static void wasm64_fe_f64_const(Wasm64FunctionEmitter* emitter, u64 bits)
{
    wasm64_fe_u8(emitter, 0x44);
    wasm64_buffer_f64(&emitter->body, bits);
}

static void wasm64_fe_global_get(Wasm64FunctionEmitter* emitter, u32 global)
{
    wasm64_fe_u8(emitter, 0x23);
    wasm64_fe_u32(emitter, global);
}

static void wasm64_fe_global_set(Wasm64FunctionEmitter* emitter, u32 global)
{
    wasm64_fe_u8(emitter, 0x24);
    wasm64_fe_u32(emitter, global);
}

static void wasm64_fe_emit_memarg(Wasm64FunctionEmitter* emitter, u32 alignment, u64 offset)
{
    wasm64_fe_u32(emitter, alignment);
    wasm64_buffer_u64_leb(&emitter->body, offset);
}

static IrType* wasm64_fe_value_ir_type(Wasm64FunctionEmitter* emitter, IrValueId value_id)
{
    if (value_id.value >= emitter->function->value_count)
    {
        return 0;
    }
    return wasm64_type(emitter->context, emitter->function->values[value_id.value].canonical_type);
}

static void wasm64_fe_integer_normalize(Wasm64FunctionEmitter* emitter, IrType* type, bool signed_value)
{
    if (!type || !wasm64_type_is_integer(type))
    {
        return;
    }
    u32 bits = wasm64_integer_bits(type);
    Wasm64ValType valtype = 0;
    wasm64_valtype_for_type(type, false, &valtype);
    if (type->kind == IR_TYPE_BOOLEAN)
    {
        wasm64_fe_u8(emitter, valtype == WASM64_VALTYPE_I64 ? 0x50 : 0x45); // eqz
        wasm64_fe_u8(emitter, valtype == WASM64_VALTYPE_I64 ? 0x45 : 0x45);
        // The two eqz operations turn a non-zero scalar into one for both
        // widths.  For i64 the first opcode above was intentionally replaced
        // below by the canonical i64.eqz sequence.
        return;
    }
    if (valtype == WASM64_VALTYPE_I32)
    {
        if (bits >= 32)
        {
            return;
        }
        if (signed_value)
        {
            if (bits == 8)
            {
                wasm64_fe_u8(emitter, 0xc0); // i32.extend8_s
            }
            else if (bits == 16)
            {
                wasm64_fe_u8(emitter, 0xc1); // i32.extend16_s
            }
            else
            {
                wasm64_fe_i32_const(emitter, (s32)(32 - bits));
                wasm64_fe_u8(emitter, 0x74); // shl
                wasm64_fe_i32_const(emitter, (s32)(32 - bits));
                wasm64_fe_u8(emitter, 0x75); // shr_s
            }
        }
        else
        {
            wasm64_fe_i32_const(emitter, (s32)((UINT32_C(1) << bits) - 1));
            wasm64_fe_u8(emitter, 0x71); // and
        }
    }
    else if (valtype == WASM64_VALTYPE_I64)
    {
        if (bits >= 64)
        {
            return;
        }
        if (signed_value)
        {
            wasm64_fe_i64_const(emitter, (s64)(64 - bits));
            wasm64_fe_u8(emitter, 0x86); // i64.shl
            wasm64_fe_i64_const(emitter, (s64)(64 - bits));
            wasm64_fe_u8(emitter, 0x87); // i64.shr_s
        }
        else
        {
            u64 mask = bits == 64 ? UINT64_MAX : ((UINT64_C(1) << bits) - 1);
            wasm64_fe_i64_const(emitter, (s64)mask);
            wasm64_fe_u8(emitter, 0x83); // i64.and
        }
    }
}

static void wasm64_fe_emit_value(Wasm64FunctionEmitter* emitter, IrValueId value_id)
{
    wasm64_fe_local_get(emitter, emitter->value_locals[value_id.value]);
}

static void wasm64_fe_emit_integer_value(Wasm64FunctionEmitter* emitter, IrValueId value_id, bool signed_value)
{
    wasm64_fe_emit_value(emitter, value_id);
    wasm64_fe_integer_normalize(emitter, wasm64_fe_value_ir_type(emitter, value_id), signed_value);
}

static void wasm64_fe_emit_result_set(Wasm64FunctionEmitter* emitter, IrInstruction* instruction, bool normalize_integer, bool signed_value)
{
    if (instruction->result.value == IR_ID_UNDERLYING_INVALID)
    {
        return;
    }
    if (normalize_integer)
    {
        wasm64_fe_integer_normalize(emitter, wasm64_type(emitter->context, instruction->canonical_type), signed_value);
    }
    wasm64_fe_local_set(emitter, emitter->value_locals[instruction->result.value]);
}

static void wasm64_fe_load(Wasm64FunctionEmitter* emitter, IrType* type)
{
    u32 size = wasm64_type_size(type);
    u32 alignment = type && type->layout.alignment ? wasm64_log2_alignment(type->layout.alignment) : 0;
    if (type && (type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_FUNCTION))
    {
        wasm64_fe_u8(emitter, 0x29); // i64.load
        wasm64_fe_emit_memarg(emitter, alignment, 0);
    }
    else if (type && type->kind == IR_TYPE_FLOAT)
    {
        wasm64_fe_u8(emitter, type->bit_width == 32 ? 0x2a : 0x2b);
        wasm64_fe_emit_memarg(emitter, alignment, 0);
    }
    else if (type && wasm64_type_is_integer(type))
    {
        Wasm64ValType valtype = 0;
        wasm64_valtype_for_type(type, false, &valtype);
        bool sign = type->kind == IR_TYPE_INTEGER && type->is_signed;
        if (valtype == WASM64_VALTYPE_I64)
        {
            wasm64_fe_u8(emitter, 0x29);
        }
        else if (size <= 1)
        {
            wasm64_fe_u8(emitter, sign ? 0x2c : 0x2d); // i32.load8_s/u
        }
        else if (size <= 2)
        {
            wasm64_fe_u8(emitter, sign ? 0x2e : 0x2f); // i32.load16_s/u
        }
        else
        {
            wasm64_fe_u8(emitter, sign ? 0x28 : 0x28); // i32.load (zero/sign same width)
        }
        wasm64_fe_emit_memarg(emitter, alignment, 0);
    }
}

static void wasm64_fe_store(Wasm64FunctionEmitter* emitter, IrType* type)
{
    u32 size = wasm64_type_size(type);
    u32 alignment = type && type->layout.alignment ? wasm64_log2_alignment(type->layout.alignment) : 0;
    if (type && (type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_FUNCTION))
    {
        wasm64_fe_u8(emitter, 0x37); // i64.store
        wasm64_fe_emit_memarg(emitter, alignment, 0);
    }
    else if (type && type->kind == IR_TYPE_FLOAT)
    {
        wasm64_fe_u8(emitter, type->bit_width == 32 ? 0x38 : 0x39);
        wasm64_fe_emit_memarg(emitter, alignment, 0);
    }
    else if (type && wasm64_type_is_integer(type))
    {
        Wasm64ValType valtype = 0;
        wasm64_valtype_for_type(type, false, &valtype);
        if (valtype == WASM64_VALTYPE_I64)
        {
            if (size <= 1)
            {
                wasm64_fe_u8(emitter, 0x3c); // i64.store8
            }
            else if (size <= 2)
            {
                wasm64_fe_u8(emitter, 0x3d); // i64.store16
            }
            else if (size <= 4)
            {
                wasm64_fe_u8(emitter, 0x3e); // i64.store32
            }
            else
            {
                wasm64_fe_u8(emitter, 0x37);
            }
        }
        else if (size <= 1)
        {
            wasm64_fe_u8(emitter, 0x3a); // i32.store8
        }
        else if (size <= 2)
        {
            wasm64_fe_u8(emitter, 0x3b); // i32.store16
        }
        else
        {
            wasm64_fe_u8(emitter, 0x36); // i32.store
        }
        wasm64_fe_emit_memarg(emitter, alignment, 0);
    }
}

static void wasm64_fe_emit_binary_opcode(Wasm64FunctionEmitter* emitter, IrBinaryOperation operation, IrType* type)
{
    bool is_float = wasm64_type_is_float(type);
    bool is_i64 = false;
    if (type)
    {
        Wasm64ValType valtype = 0;
        wasm64_valtype_for_type(type, false, &valtype);
        is_i64 = valtype == WASM64_VALTYPE_I64;
    }
    u8 opcode = 0;
    if (is_float)
    {
        bool is_f64 = type->bit_width == 64;
        switch (operation)
        {
        case IR_BINARY_FLOAT_ADD: opcode = is_f64 ? 0xa0 : 0x92; break;
        case IR_BINARY_FLOAT_SUBTRACT: opcode = is_f64 ? 0xa1 : 0x93; break;
        case IR_BINARY_FLOAT_MULTIPLY: opcode = is_f64 ? 0xa2 : 0x94; break;
        case IR_BINARY_FLOAT_DIVIDE: opcode = is_f64 ? 0xa3 : 0x95; break;
        case IR_BINARY_FLOAT_EQUAL: opcode = is_f64 ? 0x61 : 0x5b; break;
        case IR_BINARY_FLOAT_NOT_EQUAL: opcode = is_f64 ? 0x62 : 0x5c; break;
        case IR_BINARY_FLOAT_LESS: opcode = is_f64 ? 0x63 : 0x5d; break;
        case IR_BINARY_FLOAT_GREATER: opcode = is_f64 ? 0x64 : 0x5e; break;
        case IR_BINARY_FLOAT_LESS_EQUAL: opcode = is_f64 ? 0x65 : 0x5f; break;
        case IR_BINARY_FLOAT_GREATER_EQUAL: opcode = is_f64 ? 0x66 : 0x60; break;
        default: break;
        }
    }
    else
    {
        u8 base = is_i64 ? 0x7c : 0x6a;
        switch (operation)
        {
        case IR_BINARY_INTEGER_ADD: opcode = base; break;
        case IR_BINARY_INTEGER_SUBTRACT: opcode = base + 1; break;
        case IR_BINARY_INTEGER_MULTIPLY: opcode = base + 2; break;
        case IR_BINARY_SIGNED_DIVIDE: opcode = base + 3; break;
        case IR_BINARY_UNSIGNED_DIVIDE: opcode = base + 4; break;
        case IR_BINARY_SIGNED_REMAINDER: opcode = base + 5; break;
        case IR_BINARY_UNSIGNED_REMAINDER: opcode = base + 6; break;
        case IR_BINARY_SHIFT_LEFT: opcode = base + 8; break;
        case IR_BINARY_SIGNED_SHIFT_RIGHT: opcode = base + 9; break;
        case IR_BINARY_UNSIGNED_SHIFT_RIGHT: opcode = base + 10; break;
        case IR_BINARY_INTEGER_BITWISE_AND: opcode = base + 11; break;
        case IR_BINARY_INTEGER_BITWISE_OR: opcode = base + 12; break;
        case IR_BINARY_INTEGER_BITWISE_XOR: opcode = base + 13; break;
        case IR_BINARY_INTEGER_EQUAL: opcode = is_i64 ? 0x51 : 0x46; break;
        case IR_BINARY_INTEGER_NOT_EQUAL: opcode = is_i64 ? 0x52 : 0x47; break;
        case IR_BINARY_SIGNED_LESS: opcode = is_i64 ? 0x54 : 0x48; break;
        case IR_BINARY_UNSIGNED_LESS: opcode = is_i64 ? 0x55 : 0x49; break;
        case IR_BINARY_SIGNED_GREATER: opcode = is_i64 ? 0x56 : 0x4a; break;
        case IR_BINARY_UNSIGNED_GREATER: opcode = is_i64 ? 0x57 : 0x4b; break;
        case IR_BINARY_SIGNED_LESS_EQUAL: opcode = is_i64 ? 0x58 : 0x4c; break;
        case IR_BINARY_UNSIGNED_LESS_EQUAL: opcode = is_i64 ? 0x59 : 0x4d; break;
        case IR_BINARY_SIGNED_GREATER_EQUAL: opcode = is_i64 ? 0x5a : 0x4e; break;
        case IR_BINARY_UNSIGNED_GREATER_EQUAL: opcode = is_i64 ? 0x5b : 0x4f; break;
        case IR_BINARY_POINTER_EQUAL: opcode = 0x51; break;
        case IR_BINARY_POINTER_NOT_EQUAL: opcode = 0x52; break;
        case IR_BINARY_BOOLEAN_AND: opcode = 0x71; break;
        case IR_BINARY_BOOLEAN_OR: opcode = 0x72; break;
        case IR_BINARY_BOOLEAN_EQUAL: opcode = 0x46; break;
        case IR_BINARY_BOOLEAN_NOT_EQUAL: opcode = 0x47; break;
        default: break;
        }
    }
    if (!opcode)
    {
        wasm64_fail(emitter->context, WASM64_ERROR_UNSUPPORTED_INSTRUCTION, wasm64_s8("unsupported scalar Wasm64 binary operation"), emitter->function, 0, 0,
                    IR_SYMBOL_ID_INVALID);
        return;
    }
    wasm64_fe_u8(emitter, opcode);
}

static void wasm64_fe_emit_unary_opcode(Wasm64FunctionEmitter* emitter, IrUnaryOperation operation, IrType* type)
{
    Wasm64ValType valtype = 0;
    wasm64_valtype_for_type(type, false, &valtype);
    bool is_i64 = valtype == WASM64_VALTYPE_I64;
    u8 opcode = 0;
    switch (operation)
    {
    case IR_UNARY_INTEGER_COUNT_LEADING_ZEROS: opcode = is_i64 ? 0x79 : 0x67; break;
    case IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS: opcode = is_i64 ? 0x7a : 0x68; break;
    case IR_UNARY_INTEGER_POPULATION_COUNT: opcode = is_i64 ? 0x7b : 0x69; break;
    default: break;
    }
    if (opcode)
    {
        wasm64_fe_u8(emitter, opcode);
    }
}

static void wasm64_fe_emit_parallel_copy(Wasm64FunctionEmitter* emitter, IrBlock* predecessor, IrBlock* target)
{
    u32 parameter_count = target ? target->parameter_count : 0;
    if (!parameter_count)
    {
        return;
    }
    u32 parameter_index = 0;
    for (IrBlockParameter* parameter = target->first_parameter; parameter; parameter = parameter->next, parameter_index += 1)
    {
        IrValueId source = IR_VALUE_ID_INVALID;
        for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
        {
            if (incoming->predecessor.value == predecessor->id.value)
            {
                source = incoming->value;
                break;
            }
        }
        if (source.value == IR_ID_UNDERLYING_INVALID || parameter->value.value >= emitter->function->value_count)
        {
            wasm64_fail(emitter->context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("missing Wasm64 block-parameter incoming value"), emitter->function,
                        target, 0, IR_SYMBOL_ID_INVALID);
            return;
        }
        wasm64_fe_emit_value(emitter, source);
        wasm64_fe_local_set(emitter, emitter->temp_base + parameter_index);
    }
    parameter_index = 0;
    for (IrBlockParameter* parameter = target->first_parameter; parameter; parameter = parameter->next, parameter_index += 1)
    {
        wasm64_fe_local_get(emitter, emitter->temp_base + parameter_index);
        wasm64_fe_local_set(emitter, emitter->value_locals[parameter->value.value]);
    }
}

static Wasm64StringRecord* wasm64_string_record_find(Wasm64Context* context, IrFunction* function, IrInstructionId instruction)
{
    for (u32 index = 0; index < context->string_count; index += 1)
    {
        Wasm64StringRecord* record = context->strings + index;
        if (record->function == function && record->instruction.value == instruction.value)
        {
            return record;
        }
    }
    return 0;
}

static void wasm64_fe_emit_cast(Wasm64FunctionEmitter* emitter, IrInstruction* instruction, IrType* source, IrType* destination)
{
    Wasm64ValType source_type = 0;
    Wasm64ValType destination_type = 0;
    bool source_place = false;
    bool destination_place = false;
    if (instruction->operands[0].value < emitter->function->value_count)
    {
        source_place = emitter->function->values[instruction->operands[0].value].category == IR_VALUE_PLACE;
    }
    if (!wasm64_valtype_for_type(source, source_place, &source_type) || !wasm64_valtype_for_type(destination, destination_place, &destination_type))
    {
        wasm64_fail(emitter->context, WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI, wasm64_s8("unsupported Wasm64 cast shape"), emitter->function, 0,
                    instruction, IR_SYMBOL_ID_INVALID);
        return;
    }
    bool source_integer = wasm64_type_is_integer(source) || source->kind == IR_TYPE_POINTER;
    bool destination_integer = wasm64_type_is_integer(destination) || destination->kind == IR_TYPE_POINTER;
    bool source_float = wasm64_type_is_float(source);
    bool destination_float = wasm64_type_is_float(destination);
    if (source_integer)
    {
        wasm64_fe_emit_integer_value(emitter, instruction->operands[0], source->kind == IR_TYPE_INTEGER && source->is_signed);
    }
    else
    {
        wasm64_fe_emit_value(emitter, instruction->operands[0]);
    }
    if (source_type == destination_type && instruction->conversion_operation == IR_CONVERSION_IDENTITY)
    {
        return;
    }
    if (source_integer && destination_integer)
    {
        if (source_type == WASM64_VALTYPE_I32 && destination_type == WASM64_VALTYPE_I64)
        {
            wasm64_fe_u8(emitter, source->kind == IR_TYPE_INTEGER && source->is_signed ? 0xac : 0xad);
        }
        else if (source_type == WASM64_VALTYPE_I64 && destination_type == WASM64_VALTYPE_I32)
        {
            wasm64_fe_u8(emitter, 0xa7);
        }
        wasm64_fe_integer_normalize(emitter, destination, destination->kind == IR_TYPE_INTEGER && destination->is_signed);
        return;
    }
    if (source_integer && destination_float)
    {
        if (destination_type == WASM64_VALTYPE_F32)
        {
            wasm64_fe_u8(emitter, source_type == WASM64_VALTYPE_I64 ? (source->kind == IR_TYPE_INTEGER && source->is_signed ? 0xb4 : 0xb5)
                                                                     : (source->kind == IR_TYPE_INTEGER && source->is_signed ? 0xb2 : 0xb3));
        }
        else
        {
            wasm64_fe_u8(emitter, source_type == WASM64_VALTYPE_I64 ? (source->kind == IR_TYPE_INTEGER && source->is_signed ? 0xb9 : 0xba)
                                                                     : (source->kind == IR_TYPE_INTEGER && source->is_signed ? 0xb7 : 0xb8));
        }
        return;
    }
    if (source_float && destination_integer)
    {
        if (destination_type == WASM64_VALTYPE_I32)
        {
            wasm64_fe_u8(emitter, source_type == WASM64_VALTYPE_F32 ? (destination->kind == IR_TYPE_INTEGER && destination->is_signed ? 0xa8 : 0xa9)
                                                                     : (destination->kind == IR_TYPE_INTEGER && destination->is_signed ? 0xaa : 0xab));
        }
        else
        {
            wasm64_fe_u8(emitter, source_type == WASM64_VALTYPE_F32 ? (destination->kind == IR_TYPE_INTEGER && destination->is_signed ? 0xae : 0xaf)
                                                                     : (destination->kind == IR_TYPE_INTEGER && destination->is_signed ? 0xb0 : 0xb1));
        }
        wasm64_fe_integer_normalize(emitter, destination, destination->kind == IR_TYPE_INTEGER && destination->is_signed);
        return;
    }
    if (source_float && destination_float)
    {
        if (source_type == WASM64_VALTYPE_F32 && destination_type == WASM64_VALTYPE_F64)
        {
            wasm64_fe_u8(emitter, 0xbb);
        }
        else if (source_type == WASM64_VALTYPE_F64 && destination_type == WASM64_VALTYPE_F32)
        {
            wasm64_fe_u8(emitter, 0xb6);
        }
        return;
    }
    if (instruction->conversion_operation == IR_CONVERSION_INTEGER_REINTERPRET || instruction->conversion_operation == IR_CONVERSION_POINTER_REINTERPRET)
    {
        if (source_type == WASM64_VALTYPE_I32 && destination_type == WASM64_VALTYPE_F32)
        {
            wasm64_fe_u8(emitter, 0xbe);
        }
        else if (source_type == WASM64_VALTYPE_F32 && destination_type == WASM64_VALTYPE_I32)
        {
            wasm64_fe_u8(emitter, 0xbc);
        }
        else if (source_type == WASM64_VALTYPE_I64 && destination_type == WASM64_VALTYPE_F64)
        {
            wasm64_fe_u8(emitter, 0xbf);
        }
        else if (source_type == WASM64_VALTYPE_F64 && destination_type == WASM64_VALTYPE_I64)
        {
            wasm64_fe_u8(emitter, 0xbd);
        }
        else if (source_type != destination_type)
        {
            wasm64_fail(emitter->context, WASM64_ERROR_UNSUPPORTED_INSTRUCTION, wasm64_s8("invalid Wasm64 reinterpret cast"), emitter->function, 0,
                        instruction, IR_SYMBOL_ID_INVALID);
        }
        return;
    }
    wasm64_fail(emitter->context, WASM64_ERROR_UNSUPPORTED_INSTRUCTION, wasm64_s8("unsupported Wasm64 conversion"), emitter->function, 0, instruction,
                IR_SYMBOL_ID_INVALID);
}

static u32 wasm64_fe_count_block_parameters(IrFunction* function)
{
    u32 count = 0;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        count += function->blocks[block_index].parameter_count;
    }
    return count;
}

static bool wasm64_fe_initialize(Wasm64FunctionEmitter* emitter, Wasm64Context* context, Wasm64FunctionRecord* record)
{
    IrFunction* function = record->function;
    *emitter = (Wasm64FunctionEmitter){.context = context, .function = function, .record = record};
    wasm64_buffer_init(&emitter->body, context->arena);
    emitter->value_locals = arena_allocate(context->arena, u32, function->value_count ? function->value_count : 1);
    emitter->value_types = arena_allocate(context->arena, u8, function->value_count ? function->value_count : 1);
    emitter->value_offsets = arena_allocate(context->arena, u32, function->value_count ? function->value_count : 1);
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        emitter->value_locals[value_index] = record->signature.param_count + value_index;
        emitter->value_offsets[value_index] = UINT32_MAX;
        IrValue* value = function->values + value_index;
        IrType* type = wasm64_type(context, value->canonical_type);
        Wasm64ValType valtype = 0;
        // A direct callee is represented as an SSA value by canonical IR, but
        // the call encoding consumes its symbol index rather than a runtime
        // operand.  Reserve an inert scalar local for that value so direct
        // calls do not look like aggregate ABI traffic.
        if (type && type->kind == IR_TYPE_FUNCTION)
        {
            valtype = WASM64_VALTYPE_I64;
        }
        else if (!wasm64_valtype_for_type(type, value->category == IR_VALUE_PLACE, &valtype))
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI, wasm64_s8("aggregate Wasm64 SSA value is unsupported"), function, 0, 0,
                        IR_SYMBOL_ID_INVALID);
            return false;
        }
        emitter->value_types[value_index] = (u8)valtype;
    }

    u64 frame_cursor = 0;
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->opcode != IR_OPCODE_LOCAL || instruction->result.value == IR_ID_UNDERLYING_INVALID)
        {
            continue;
        }
        IrType* type = wasm64_type(context, instruction->canonical_type);
        u64 size = type && type->layout.resolved ? type->layout.size : 0;
        u64 alignment = type && type->layout.alignment ? type->layout.alignment : 1;
        if (!size || size > UINT32_MAX || alignment > UINT32_MAX || alignment == 0 || alignment > (UINT64_C(1) << 31))
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_TYPE, wasm64_s8("invalid Wasm64 local frame layout"), function, 0, instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
        wasm64_align_cursor(&frame_cursor, alignment);
        if (frame_cursor > UINT32_MAX - size)
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_TYPE, wasm64_s8("Wasm64 local frame exceeds 32-bit offset"), function, 0, instruction,
                        IR_SYMBOL_ID_INVALID);
            return false;
        }
        emitter->value_offsets[instruction->result.value] = (u32)frame_cursor;
        frame_cursor += size;
    }
    wasm64_align_cursor(&frame_cursor, 16);
    if (frame_cursor > UINT32_MAX)
    {
        wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_TYPE, wasm64_s8("Wasm64 local frame exceeds 32-bit offset"), function, 0, 0, IR_SYMBOL_ID_INVALID);
        return false;
    }
    emitter->frame_size = (u32)frame_cursor;
    u32 temp_count = wasm64_fe_count_block_parameters(function);
    emitter->pc_local = record->signature.param_count + function->value_count;
    emitter->fp_local = emitter->pc_local + 1;
    emitter->sp_local = emitter->fp_local + 1;
    emitter->scratch_local = emitter->sp_local + 1;
    emitter->temp_base = emitter->scratch_local + 1;
    emitter->extra_local_count = 4 + temp_count;
    emitter->local_count = record->signature.param_count + function->value_count + emitter->extra_local_count;
    u32 declared_local_count = function->value_count + emitter->extra_local_count;
    emitter->local_types = arena_allocate(context->arena, u8, declared_local_count ? declared_local_count : 1);
    if (function->value_count)
    {
        memcpy(emitter->local_types, emitter->value_types, function->value_count);
    }
    u32 local_type_index = function->value_count;
    emitter->local_types[local_type_index++] = WASM64_VALTYPE_I32;
    emitter->local_types[local_type_index++] = WASM64_VALTYPE_I64;
    emitter->local_types[local_type_index++] = WASM64_VALTYPE_I64;
    emitter->local_types[local_type_index++] = WASM64_VALTYPE_I64;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        for (IrBlockParameter* parameter = function->blocks[block_index].first_parameter; parameter; parameter = parameter->next)
        {
            Wasm64ValType type = 0;
            IrType* parameter_type = wasm64_type(context, parameter->canonical_type);
            if (!wasm64_valtype_for_type(parameter_type, function->values[parameter->value.value].category == IR_VALUE_PLACE, &type))
            {
                wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI, wasm64_s8("aggregate Wasm64 block parameter is unsupported"), function,
                            function->blocks + block_index, 0, IR_SYMBOL_ID_INVALID);
                return false;
            }
            emitter->local_types[local_type_index++] = (u8)type;
        }
    }
    return true;
}

static void wasm64_fe_emit_prologue(Wasm64FunctionEmitter* emitter)
{
    wasm64_fe_global_get(emitter, emitter->context->stack_global_index);
    wasm64_fe_local_tee(emitter, emitter->fp_local);
    wasm64_fe_i64_const(emitter, emitter->frame_size);
    wasm64_fe_u8(emitter, 0x7c); // i64.add
    wasm64_fe_local_tee(emitter, emitter->sp_local);
    wasm64_fe_global_set(emitter, emitter->context->stack_global_index);
    wasm64_fe_i32_const(emitter, (s32)emitter->function->entry.value);
    wasm64_fe_local_set(emitter, emitter->pc_local);
}

static void wasm64_fe_emit_stack_allocate(Wasm64FunctionEmitter* emitter, IrInstruction* instruction)
{
    IrType* pointer_type = wasm64_type(emitter->context, instruction->canonical_type);
    u64 alignment = instruction->immediate_count == 1 ? instruction->immediates[0] : 1;
    if (alignment == 0 || (alignment & (alignment - 1)) || alignment > UINT32_MAX)
    {
        wasm64_fail(emitter->context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("invalid Wasm64 dynamic stack alignment"), emitter->function, 0, instruction,
                    IR_SYMBOL_ID_INVALID);
        return;
    }
    wasm64_fe_local_get(emitter, emitter->sp_local);
    wasm64_fe_i64_const(emitter, (s64)(alignment - 1));
    wasm64_fe_u8(emitter, 0x7c); // add
    wasm64_fe_i64_const(emitter, (s64)~(alignment - 1));
    wasm64_fe_u8(emitter, 0x83); // and
    wasm64_fe_local_tee(emitter, emitter->scratch_local);
    wasm64_fe_local_set(emitter, emitter->value_locals[instruction->result.value]);
    wasm64_fe_local_get(emitter, emitter->scratch_local);
    wasm64_fe_emit_value(emitter, instruction->operands[0]);
    IrType* size_type = wasm64_fe_value_ir_type(emitter, instruction->operands[0]);
    Wasm64ValType size_valtype = 0;
    wasm64_valtype_for_type(size_type, false, &size_valtype);
    if (size_valtype == WASM64_VALTYPE_I32)
    {
        wasm64_fe_u8(emitter, 0xad); // i64.extend_i32_u
    }
    wasm64_fe_u8(emitter, 0x7c); // add
    wasm64_fe_local_tee(emitter, emitter->sp_local);
    wasm64_fe_global_set(emitter, emitter->context->stack_global_index);
    BUSTER_UNUSED(pointer_type);
}

static void wasm64_fe_emit_address_add(Wasm64FunctionEmitter* emitter, u64 offset)
{
    if (offset)
    {
        wasm64_fe_i64_const(emitter, (s64)offset);
        wasm64_fe_u8(emitter, 0x7c);
    }
}

static IrBlock* wasm64_fe_block(Wasm64FunctionEmitter* emitter, IrBlockId id)
{
    return id.value < emitter->function->block_count ? emitter->function->blocks + id.value : 0;
}

static void wasm64_fe_emit_integer_constant(Wasm64FunctionEmitter* emitter, IrInstruction* instruction, IrType* type)
{
    u64 bits = instruction->immediate_count ? instruction->immediates[0] : 0;
    if (instruction->immediate_is_negative)
    {
        bits = 0 - bits;
    }
    Wasm64ValType valtype = 0;
    wasm64_valtype_for_type(type, false, &valtype);
    if (valtype == WASM64_VALTYPE_I64)
    {
        wasm64_fe_i64_const(emitter, (s64)bits);
    }
    else
    {
        wasm64_fe_i32_const(emitter, (s32)(u32)bits);
    }
    wasm64_fe_integer_normalize(emitter, type, type && type->kind == IR_TYPE_INTEGER && type->is_signed);
}

static void wasm64_fe_emit_return(Wasm64FunctionEmitter* emitter, IrInstruction* instruction)
{
    bool has_value = instruction->operand_count == 1;
    wasm64_fe_local_get(emitter, emitter->fp_local);
    wasm64_fe_global_set(emitter, emitter->context->stack_global_index);
    if (has_value)
    {
        wasm64_fe_emit_value(emitter, instruction->operands[0]);
    }
    wasm64_fe_u8(emitter, 0x0f); // return
}

static void wasm64_fe_emit_call(Wasm64FunctionEmitter* emitter, IrInstruction* instruction)
{
    if (!instruction->operand_count)
    {
        wasm64_fail(emitter->context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("Wasm64 call has no callee"), emitter->function, 0, instruction,
                    instruction->symbol);
        return;
    }
    IrValue* callee_value = emitter->function->values + instruction->operands[0].value;
    IrType* callee_type = wasm64_type(emitter->context, callee_value->canonical_type);
    bool indirect = callee_type && callee_type->kind == IR_TYPE_POINTER;
    if (indirect)
    {
        wasm64_fail(emitter->context, WASM64_ERROR_INDIRECT_CALL, wasm64_s8("indirect calls are unsupported by Wasm64"), emitter->function, 0,
                    instruction, instruction->symbol);
        return;
    }
    Wasm64FunctionRecord* record = wasm64_function_record_for_symbol(emitter->context, instruction->symbol);
    if (!record)
    {
        wasm64_fail(emitter->context, WASM64_ERROR_UNRESOLVED_SYMBOL, wasm64_s8("unresolved direct Wasm64 call"), emitter->function, 0, instruction,
                    instruction->symbol);
        return;
    }
    for (u32 argument_index = 1; argument_index < instruction->operand_count; argument_index += 1)
    {
        wasm64_fe_emit_value(emitter, instruction->operands[argument_index]);
    }
    wasm64_fe_u8(emitter, 0x10);
    wasm64_fe_u32(emitter, record->function_index);
    if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
    {
        wasm64_fe_local_set(emitter, emitter->value_locals[instruction->result.value]);
    }
}

static void wasm64_fe_emit_switch(Wasm64FunctionEmitter* emitter, IrBlock* predecessor, IrInstruction* instruction)
{
    IrType* switched_type = wasm64_fe_value_ir_type(emitter, instruction->operands[0]);
    Wasm64ValType valtype = 0;
    wasm64_valtype_for_type(switched_type, false, &valtype);
    for (u32 case_index = 0; case_index < instruction->immediate_count; case_index += 1)
    {
        wasm64_fe_emit_value(emitter, instruction->operands[0]);
        if (valtype == WASM64_VALTYPE_I64)
        {
            wasm64_fe_i64_const(emitter, (s64)instruction->immediates[case_index]);
            wasm64_fe_u8(emitter, 0x51); // i64.eq
        }
        else
        {
            wasm64_fe_i32_const(emitter, (s32)(u32)instruction->immediates[case_index]);
            wasm64_fe_u8(emitter, 0x46); // i32.eq
        }
        wasm64_fe_u8(emitter, 0x04); // if
        wasm64_fe_u8(emitter, 0x40); // empty block type
        IrBlock* target = wasm64_fe_block(emitter, instruction->targets[case_index]);
        if (target)
        {
            wasm64_fe_emit_parallel_copy(emitter, predecessor, target);
            wasm64_fe_i32_const(emitter, (s32)target->id.value);
            wasm64_fe_local_set(emitter, emitter->pc_local);
            wasm64_fe_u8(emitter, 0x0c); // br to dispatch loop (if + loop)
            wasm64_fe_u32(emitter, 2);
        }
        wasm64_fe_u8(emitter, 0x0b); // end if
    }
    if (instruction->target_count)
    {
        IrBlock* target = wasm64_fe_block(emitter, instruction->targets[instruction->target_count - 1]);
        if (target)
        {
            wasm64_fe_emit_parallel_copy(emitter, predecessor, target);
            wasm64_fe_i32_const(emitter, (s32)target->id.value);
            wasm64_fe_local_set(emitter, emitter->pc_local);
        }
    }
    wasm64_fe_u8(emitter, 0x0c);
    wasm64_fe_u32(emitter, 1);
}

static void wasm64_fe_emit_instruction(Wasm64FunctionEmitter* emitter, IrBlock* block, IrInstruction* instruction)
{
    Wasm64Context* context = emitter->context;
    IrType* type = wasm64_type(context, instruction->canonical_type);
    if (!type)
    {
        wasm64_fail(context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("missing canonical Wasm64 instruction type"), emitter->function, block, instruction,
                    IR_SYMBOL_ID_INVALID);
        return;
    }
    switch (instruction->opcode)
    {
    case IR_OPCODE_ARGUMENT:
    {
        if (instruction->immediate_count != 1 || instruction->immediates[0] >= emitter->record->signature.param_count)
        {
            wasm64_fail(context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("invalid Wasm64 argument index"), emitter->function, block, instruction,
                        IR_SYMBOL_ID_INVALID);
            return;
        }
        wasm64_fe_local_get(emitter, (u32)instruction->immediates[0]);
        wasm64_fe_local_set(emitter, emitter->value_locals[instruction->result.value]);
    }
    break;
    case IR_OPCODE_LOCAL:
    {
        u32 offset = emitter->value_offsets[instruction->result.value];
        if (offset == UINT32_MAX)
        {
            wasm64_fail(context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("missing Wasm64 local frame offset"), emitter->function, block, instruction,
                        IR_SYMBOL_ID_INVALID);
            return;
        }
        wasm64_fe_local_get(emitter, emitter->fp_local);
        wasm64_fe_emit_address_add(emitter, offset);
        wasm64_fe_local_set(emitter, emitter->value_locals[instruction->result.value]);
    }
    break;
    case IR_OPCODE_STACK_ALLOCATE:
        wasm64_fe_emit_stack_allocate(emitter, instruction);
        break;
    case IR_OPCODE_STACK_SAVE:
        wasm64_fe_local_get(emitter, emitter->sp_local);
        wasm64_fe_local_set(emitter, emitter->value_locals[instruction->result.value]);
        break;
    case IR_OPCODE_STACK_RESTORE:
        wasm64_fe_emit_value(emitter, instruction->operands[0]);
        wasm64_fe_local_tee(emitter, emitter->sp_local);
        wasm64_fe_global_set(emitter, context->stack_global_index);
        break;
    case IR_OPCODE_GLOBAL:
    {
        Wasm64DataRecord* record = wasm64_data_record_for_symbol(context, instruction->symbol);
        if (!record)
        {
            wasm64_fail(context, WASM64_ERROR_UNRESOLVED_SYMBOL, wasm64_s8("unresolved Wasm64 global"), emitter->function, block, instruction,
                        instruction->symbol);
            return;
        }
        wasm64_fe_i64_const(emitter, (s64)record->offset);
        wasm64_fe_local_set(emitter, emitter->value_locals[instruction->result.value]);
    }
    break;
    case IR_OPCODE_LOAD:
    {
        wasm64_fe_emit_value(emitter, instruction->operands[0]);
        wasm64_fe_load(emitter, type);
        wasm64_fe_emit_result_set(emitter, instruction, wasm64_type_is_integer(type), type->kind == IR_TYPE_INTEGER && type->is_signed);
    }
    break;
    case IR_OPCODE_STORE:
    {
        IrType* stored_type = wasm64_fe_value_ir_type(emitter, instruction->operands[1]);
        if (!stored_type || !wasm64_type_is_scalar(stored_type))
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI, wasm64_s8("aggregate Wasm64 store is unsupported"), emitter->function, block,
                        instruction, IR_SYMBOL_ID_INVALID);
            return;
        }
        wasm64_fe_emit_value(emitter, instruction->operands[0]);
        wasm64_fe_emit_value(emitter, instruction->operands[1]);
        wasm64_fe_store(emitter, stored_type);
    }
    break;
    case IR_OPCODE_CONSTANT_INTEGER:
    case IR_OPCODE_ENUM:
        wasm64_fe_emit_integer_constant(emitter, instruction, type);
        wasm64_fe_emit_result_set(emitter, instruction, false, false);
        break;
    case IR_OPCODE_CONSTANT_FLOAT:
        if (type->bit_width == 32)
        {
            wasm64_fe_f32_const(emitter, (u32)instruction->immediates[0]);
        }
        else if (type->bit_width == 64)
        {
            wasm64_fe_f64_const(emitter, instruction->immediates[0]);
        }
        else
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_TYPE, wasm64_s8("unsupported Wasm64 float width"), emitter->function, block, instruction,
                        IR_SYMBOL_ID_INVALID);
            return;
        }
        wasm64_fe_emit_result_set(emitter, instruction, false, false);
        break;
    case IR_OPCODE_CONSTANT_STRING:
    {
        Wasm64StringRecord* record = wasm64_string_record_find(context, emitter->function, ir_instruction_self_id(emitter->function, instruction));
        if (!record)
        {
            wasm64_fail(context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("missing Wasm64 string data record"), emitter->function, block, instruction,
                        IR_SYMBOL_ID_INVALID);
            return;
        }
        wasm64_fe_i64_const(emitter, (s64)record->offset);
        wasm64_fe_emit_result_set(emitter, instruction, false, false);
    }
    break;
    case IR_OPCODE_UNDEFINED:
        if (wasm64_type_is_float(type))
        {
            if (type->bit_width == 32)
            {
                wasm64_fe_f32_const(emitter, 0);
            }
            else
            {
                wasm64_fe_f64_const(emitter, 0);
            }
        }
        else if (type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_ENUM)
        {
            wasm64_valtype_for_type(type, false, 0);
            if (wasm64_integer_bits(type) > 32 || type->kind == IR_TYPE_POINTER)
            {
                wasm64_fe_i64_const(emitter, 0);
            }
            else
            {
                wasm64_fe_i32_const(emitter, 0);
            }
        }
        else
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI, wasm64_s8("aggregate Wasm64 undefined value is unsupported"), emitter->function,
                        block, instruction, IR_SYMBOL_ID_INVALID);
            return;
        }
        wasm64_fe_emit_result_set(emitter, instruction, false, false);
        break;
    case IR_OPCODE_FUNCTION:
    {
        Wasm64FunctionRecord* record = wasm64_function_record_for_symbol(context, instruction->symbol);
        if (!record)
        {
            wasm64_fail(context, WASM64_ERROR_UNRESOLVED_SYMBOL, wasm64_s8("unresolved Wasm64 function reference"), emitter->function, block, instruction,
                        instruction->symbol);
            return;
        }
        // Function values cannot be called indirectly; the numeric marker is
        // retained only for diagnostics and for frontends that carry an unused
        // function reference alongside a direct call.
        wasm64_fe_i64_const(emitter, record->function_index);
        wasm64_fe_emit_result_set(emitter, instruction, false, false);
    }
    break;
    case IR_OPCODE_LENGTH:
    {
        IrValue* operand = emitter->function->values + instruction->operands[0].value;
        IrType* iterable = wasm64_type(context, operand->canonical_type);
        if (!iterable || (iterable->kind != IR_TYPE_ARRAY && iterable->kind != IR_TYPE_VECTOR))
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI, wasm64_s8("dynamic Wasm64 slice/range length is unsupported"), emitter->function,
                        block, instruction, IR_SYMBOL_ID_INVALID);
            return;
        }
        wasm64_fe_i64_const(emitter, (s64)iterable->element_count);
        wasm64_fe_emit_result_set(emitter, instruction, false, false);
    }
    break;
    case IR_OPCODE_INDEX:
    {
        IrType* base = wasm64_fe_value_ir_type(emitter, instruction->operands[0]);
        IrType* element = base ? wasm64_type(context, base->element_type) : 0;
        if (!element || !element->layout.resolved)
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_TYPE, wasm64_s8("invalid Wasm64 index element type"), emitter->function, block, instruction,
                        IR_SYMBOL_ID_INVALID);
            return;
        }
        wasm64_fe_emit_value(emitter, instruction->operands[0]);
        wasm64_fe_emit_value(emitter, instruction->operands[1]);
        IrType* index_type = wasm64_fe_value_ir_type(emitter, instruction->operands[1]);
        Wasm64ValType index_valtype = 0;
        wasm64_valtype_for_type(index_type, false, &index_valtype);
        if (index_valtype == WASM64_VALTYPE_I32)
        {
            wasm64_fe_u8(emitter, 0xad); // i64.extend_i32_u
        }
        wasm64_fe_i64_const(emitter, (s64)element->layout.size);
        wasm64_fe_u8(emitter, 0x7e); // i64.mul
        wasm64_fe_u8(emitter, 0x7c); // i64.add
        wasm64_fe_emit_result_set(emitter, instruction, false, false);
    }
    break;
    case IR_OPCODE_FIELD:
    {
        IrType* base = wasm64_fe_value_ir_type(emitter, instruction->operands[0]);
        u64 field_index = instruction->immediate_count ? instruction->immediates[0] : UINT64_MAX;
        if (!base || (base->kind != IR_TYPE_STRUCT && base->kind != IR_TYPE_UNION) || field_index >= base->field_count)
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_TYPE, wasm64_s8("invalid Wasm64 field address"), emitter->function, block, instruction,
                        IR_SYMBOL_ID_INVALID);
            return;
        }
        wasm64_fe_emit_value(emitter, instruction->operands[0]);
        wasm64_fe_emit_address_add(emitter, base->fields[field_index].offset);
        wasm64_fe_emit_result_set(emitter, instruction, false, false);
    }
    break;
    case IR_OPCODE_ADDRESS_OF:
    case IR_OPCODE_DEREFERENCE:
        wasm64_fe_emit_value(emitter, instruction->operands[0]);
        wasm64_fe_emit_result_set(emitter, instruction, false, false);
        break;
    case IR_OPCODE_CAST:
        wasm64_fe_emit_cast(emitter, instruction, wasm64_fe_value_ir_type(emitter, instruction->operands[0]), type);
        wasm64_fe_emit_result_set(emitter, instruction, false, false);
        break;
    case IR_OPCODE_UNARY:
    {
        IrType* operand_type = wasm64_fe_value_ir_type(emitter, instruction->operands[0]);
        if (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE || instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT ||
            instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS || instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS ||
            instruction->unary_operation == IR_UNARY_INTEGER_POPULATION_COUNT)
        {
            wasm64_fe_emit_integer_value(emitter, instruction->operands[0], operand_type && operand_type->kind == IR_TYPE_INTEGER && operand_type->is_signed);
            if (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE)
            {
                if (wasm64_integer_bits(type) > 32)
                {
                    wasm64_fe_i64_const(emitter, -1);
                    wasm64_fe_u8(emitter, 0x7e); // i64.mul
                }
                else
                {
                    wasm64_fe_i32_const(emitter, -1);
                    wasm64_fe_u8(emitter, 0x6c); // i32.mul
                }
            }
            else if (instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT)
            {
                if (wasm64_integer_bits(type) > 32)
                {
                    wasm64_fe_i64_const(emitter, -1);
                    wasm64_fe_u8(emitter, 0x83);
                }
                else
                {
                    wasm64_fe_i32_const(emitter, -1);
                    wasm64_fe_u8(emitter, 0x73);
                }
            }
            else
            {
                wasm64_fe_emit_unary_opcode(emitter, instruction->unary_operation, type);
            }
            wasm64_fe_emit_result_set(emitter, instruction, true, type->kind == IR_TYPE_INTEGER && type->is_signed);
        }
        else if (instruction->unary_operation == IR_UNARY_FLOAT_NEGATE)
        {
            // f32/f64 neg have opcodes 0x8c/0x9a; a zero subtraction is
            // equivalent and keeps the opcode table compact.
            wasm64_fe_emit_value(emitter, instruction->operands[0]);
            if (type->bit_width == 32)
            {
                wasm64_fe_u8(emitter, 0x8c);
            }
            else
            {
                wasm64_fe_u8(emitter, 0x9a);
            }
            wasm64_fe_emit_result_set(emitter, instruction, false, false);
        }
        else if (instruction->unary_operation == IR_UNARY_BOOLEAN_NOT)
        {
            wasm64_fe_emit_value(emitter, instruction->operands[0]);
            wasm64_fe_u8(emitter, 0x45);
            wasm64_fe_emit_result_set(emitter, instruction, false, false);
        }
        else
        {
            wasm64_fail(context, WASM64_ERROR_SIMD, wasm64_s8("vector/SIMD unary operation is unsupported by Wasm64"), emitter->function, block,
                        instruction, IR_SYMBOL_ID_INVALID);
        }
    }
    break;
    case IR_OPCODE_BINARY:
    {
        IrType* operand_type = wasm64_fe_value_ir_type(emitter, instruction->operands[0]);
        IrBinaryOperation operation = instruction->binary_operation;
        bool integer = wasm64_type_is_integer(operand_type) || (operand_type && operand_type->kind == IR_TYPE_POINTER);
        bool signed_value = operand_type && operand_type->kind == IR_TYPE_INTEGER && operand_type->is_signed;
        if (operation == IR_BINARY_RANGE)
        {
            wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI, wasm64_s8("range aggregate is unsupported by Wasm64"), emitter->function, block,
                        instruction, IR_SYMBOL_ID_INVALID);
            return;
        }
        if (integer)
        {
            wasm64_fe_emit_integer_value(emitter, instruction->operands[0], signed_value);
            wasm64_fe_emit_integer_value(emitter, instruction->operands[1], signed_value);
        }
        else
        {
            wasm64_fe_emit_value(emitter, instruction->operands[0]);
            wasm64_fe_emit_value(emitter, instruction->operands[1]);
        }
        wasm64_fe_emit_binary_opcode(emitter, operation, operand_type);
        wasm64_fe_emit_result_set(emitter, instruction, integer && operation <= IR_BINARY_INTEGER_BITWISE_XOR,
                                  signed_value && (operation == IR_BINARY_INTEGER_ADD || operation == IR_BINARY_INTEGER_SUBTRACT || operation == IR_BINARY_INTEGER_MULTIPLY ||
                                                   operation == IR_BINARY_SIGNED_DIVIDE || operation == IR_BINARY_SIGNED_REMAINDER || operation == IR_BINARY_SIGNED_SHIFT_RIGHT));
    }
    break;
    case IR_OPCODE_CALL:
        wasm64_fe_emit_call(emitter, instruction);
        break;
    case IR_OPCODE_BRANCH:
    {
        IrBlock* target = wasm64_fe_block(emitter, instruction->targets[0]);
        if (target)
        {
            wasm64_fe_emit_parallel_copy(emitter, block, target);
            wasm64_fe_i32_const(emitter, (s32)target->id.value);
            wasm64_fe_local_set(emitter, emitter->pc_local);
        }
        wasm64_fe_u8(emitter, 0x0c);
        wasm64_fe_u32(emitter, 1);
    }
    break;
    case IR_OPCODE_BRANCH_IF:
    {
        wasm64_fe_emit_value(emitter, instruction->operands[0]);
        wasm64_fe_u8(emitter, 0x04);
        wasm64_fe_u8(emitter, 0x40);
        IrBlock* true_target = wasm64_fe_block(emitter, instruction->targets[0]);
        if (true_target)
        {
            wasm64_fe_emit_parallel_copy(emitter, block, true_target);
            wasm64_fe_i32_const(emitter, (s32)true_target->id.value);
            wasm64_fe_local_set(emitter, emitter->pc_local);
        }
        wasm64_fe_u8(emitter, 0x05); // else
        IrBlock* false_target = wasm64_fe_block(emitter, instruction->targets[1]);
        if (false_target)
        {
            wasm64_fe_emit_parallel_copy(emitter, block, false_target);
            wasm64_fe_i32_const(emitter, (s32)false_target->id.value);
            wasm64_fe_local_set(emitter, emitter->pc_local);
        }
        wasm64_fe_u8(emitter, 0x0b); // end if
        wasm64_fe_u8(emitter, 0x0c);
        wasm64_fe_u32(emitter, 1);
    }
    break;
    case IR_OPCODE_SWITCH:
        wasm64_fe_emit_switch(emitter, block, instruction);
        break;
    case IR_OPCODE_RETURN:
        wasm64_fe_emit_return(emitter, instruction);
        break;
    case IR_OPCODE_UNREACHABLE:
        wasm64_fe_u8(emitter, 0x00);
        break;
    case IR_OPCODE_DEBUG_TRAP:
        wasm64_fe_u8(emitter, 0x00);
        break;
    case IR_OPCODE_REVERSE:
        wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI, wasm64_s8("aggregate reverse is unsupported by Wasm64"), emitter->function, block,
                    instruction, IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_ARRAY:
    case IR_OPCODE_AGGREGATE:
    case IR_OPCODE_SLICE:
    case IR_OPCODE_VA_START:
    case IR_OPCODE_VA_COPY:
    case IR_OPCODE_VA_END:
    case IR_OPCODE_VA_ARG:
        wasm64_fail(context, instruction->opcode >= IR_OPCODE_VA_START && instruction->opcode <= IR_OPCODE_VA_ARG ? WASM64_ERROR_VARIADIC
                                                                                                                     : WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI,
                    wasm64_s8("aggregate or variadic Wasm64 operation is unsupported"), emitter->function, block, instruction, IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_ATOMIC_LOAD:
    case IR_OPCODE_ATOMIC_STORE:
    case IR_OPCODE_ATOMIC_READ_MODIFY_WRITE:
    case IR_OPCODE_ATOMIC_COMPARE_EXCHANGE:
    case IR_OPCODE_ATOMIC_FENCE:
        wasm64_fail(context, WASM64_ERROR_ATOMIC, wasm64_s8("atomic operations are unsupported by Wasm64"), emitter->function, block, instruction,
                    IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_SIMD:
        wasm64_fail(context, WASM64_ERROR_SIMD, wasm64_s8("SIMD operations are unsupported by Wasm64"), emitter->function, block, instruction,
                    IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_INLINE_ASSEMBLY:
        wasm64_fail(context, WASM64_ERROR_INLINE_ASSEMBLY, wasm64_s8("inline assembly is unsupported by Wasm64"), emitter->function, block, instruction,
                    IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_LABEL_ADDRESS:
    case IR_OPCODE_INDIRECT_BRANCH:
        wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_INSTRUCTION, wasm64_s8("label control flow is unsupported by Wasm64"), emitter->function, block,
                    instruction, IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_CLEAR_INSTRUCTION_CACHE:
        wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_INSTRUCTION, wasm64_s8("instruction-cache operations are unsupported by Wasm64"), emitter->function,
                    block, instruction, IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_COUNT:
        wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_INSTRUCTION, wasm64_s8("invalid Wasm64 opcode"), emitter->function, block, instruction,
                    IR_SYMBOL_ID_INVALID);
        break;
    }
}

static bool wasm64_fe_emit_function(Wasm64Context* context, Wasm64FunctionRecord* record)
{
    Wasm64FunctionEmitter emitter = {0};
    if (!wasm64_fe_initialize(&emitter, context, record))
    {
        return false;
    }
    // Local declarations are grouped by valtype as required by the Wasm
    // binary format.  Parameters are part of the function type and therefore
    // do not appear here; every SSA value and frame/control temporary is an
    // explicit local.
    u32 group_count = 0;
    u32 declared_local_count = emitter.local_count - record->signature.param_count;
    for (u32 index = 0; index < declared_local_count;)
    {
        u8 type = emitter.local_types[index];
        u32 end = index + 1;
        while (end < declared_local_count && emitter.local_types[end] == type)
        {
            end += 1;
        }
        group_count += 1;
        index = end;
    }
    wasm64_buffer_u32_leb(&emitter.body, group_count);
    for (u32 index = 0; index < declared_local_count;)
    {
        u8 type = emitter.local_types[index];
        u32 end = index + 1;
        while (end < declared_local_count && emitter.local_types[end] == type)
        {
            end += 1;
        }
        wasm64_buffer_u32_leb(&emitter.body, end - index);
        wasm64_buffer_u8(&emitter.body, type);
        index = end;
    }

    wasm64_fe_emit_prologue(&emitter);
    wasm64_fe_u8(&emitter, 0x02); // block $exit
    wasm64_fe_u8(&emitter, 0x40);
    wasm64_fe_u8(&emitter, 0x03); // loop $dispatch
    wasm64_fe_u8(&emitter, 0x40);
    for (u32 block_index = 0; block_index < emitter.function->block_count; block_index += 1)
    {
        IrBlock* block = emitter.function->blocks + block_index;
        wasm64_fe_local_get(&emitter, emitter.pc_local);
        wasm64_fe_i32_const(&emitter, (s32)block->id.value);
        wasm64_fe_u8(&emitter, 0x46); // i32.eq
        wasm64_fe_u8(&emitter, 0x04); // if
        wasm64_fe_u8(&emitter, 0x40);
        IrInstructionId instruction_id = block->first_instruction;
        while (instruction_id.value != IR_ID_UNDERLYING_INVALID && !wasm64_failed(context))
        {
            if (instruction_id.value >= emitter.function->instruction_count)
            {
                wasm64_fail(context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("Wasm64 block instruction out of range"), emitter.function, block, 0,
                            IR_SYMBOL_ID_INVALID);
                break;
            }
            IrInstruction* instruction = emitter.function->instructions + instruction_id.value;
            wasm64_fe_emit_instruction(&emitter, block, instruction);
            instruction_id = instruction->next;
        }
        wasm64_fe_u8(&emitter, 0x0b); // end if
    }
    wasm64_fe_u8(&emitter, 0x0c); // no matching block: leave dispatcher
    wasm64_fe_u32(&emitter, 1);
    wasm64_fe_u8(&emitter, 0x0b); // end loop
    wasm64_fe_u8(&emitter, 0x0b); // end block
    // Every valid source return emits an explicit Wasm `return`.  Mark the
    // dispatcher fallthrough unreachable so result-producing functions also
    // validate when the structured block itself has the empty block type.
    wasm64_fe_u8(&emitter, 0x00); // unreachable
    wasm64_fe_u8(&emitter, 0x0b); // end function body

    wasm64_buffer_u32_leb(&context->code_payload, (u32)emitter.body.length);
    wasm64_buffer_bytes(&context->code_payload, emitter.body.data, emitter.body.length);
    context->stats.code_bytes += emitter.body.length;
    return !wasm64_failed(context);
}

static bool wasm64_build_code_payload(Wasm64Context* context)
{
    wasm64_buffer_u32_leb(&context->code_payload, context->stats.defined_function_count);
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        Wasm64FunctionRecord* record = context->functions + index;
        if (!record->imported && !wasm64_fe_emit_function(context, record))
        {
            return false;
        }
    }
    return true;
}

static bool wasm64_build_data_payload(Wasm64Context* context)
{
    u32 segment_count = 0;
    for (u32 index = 0; index < context->data_count; index += 1)
    {
        if (context->data_records[index].has_bytes)
        {
            segment_count += 1;
        }
    }
    for (u32 index = 0; index < context->string_count; index += 1)
    {
        segment_count += 1;
    }
    wasm64_buffer_u32_leb(&context->data_payload, segment_count);
    for (u32 index = 0; index < context->data_count; index += 1)
    {
        Wasm64DataRecord* record = context->data_records + index;
        if (!record->has_bytes)
        {
            continue;
        }
        wasm64_buffer_u32_leb(&context->data_payload, 0); // active, memory 0 implicit
        wasm64_buffer_u8(&context->data_payload, 0x42);
        wasm64_buffer_s64_leb(&context->data_payload, (s64)record->offset);
        wasm64_buffer_u8(&context->data_payload, 0x0b);
        wasm64_buffer_u32_leb(&context->data_payload, (u32)record->size);
        wasm64_buffer_bytes(&context->data_payload, record->bytes, record->size);
    }
    for (u32 index = 0; index < context->string_count; index += 1)
    {
        Wasm64StringRecord* record = context->strings + index;
        wasm64_buffer_u32_leb(&context->data_payload, 0);
        wasm64_buffer_u8(&context->data_payload, 0x42);
        wasm64_buffer_s64_leb(&context->data_payload, (s64)record->offset);
        wasm64_buffer_u8(&context->data_payload, 0x0b);
        wasm64_buffer_u32_leb(&context->data_payload, (u32)(record->literal.length + 1));
        wasm64_buffer_bytes(&context->data_payload, (u8*)record->literal.pointer, record->literal.length);
        wasm64_buffer_u8(&context->data_payload, 0);
    }
    context->stats.data_segment_count = segment_count;
    return true;
}

static void wasm64_append_section(Wasm64Buffer* output, u8 id, Wasm64Buffer* payload)
{
    if (!payload->length)
    {
        return;
    }
    wasm64_buffer_u8(output, id);
    wasm64_buffer_u32_leb(output, (u32)payload->length);
    wasm64_buffer_bytes(output, payload->data, payload->length);
}

static bool wasm64_build_module(Wasm64Context* context, ByteSlice* output)
{
    Wasm64Buffer result = {0};
    wasm64_buffer_init(&result, context->arena);
    wasm64_buffer_u8(&result, 0x00);
    wasm64_buffer_u8(&result, 0x61);
    wasm64_buffer_u8(&result, 0x73);
    wasm64_buffer_u8(&result, 0x6d);
    wasm64_buffer_u8(&result, 0x01);
    wasm64_buffer_u8(&result, 0x00);
    wasm64_buffer_u8(&result, 0x00);
    wasm64_buffer_u8(&result, 0x00);
    wasm64_append_section(&result, 1, &context->type_payload);
    wasm64_append_section(&result, 2, &context->import_payload);
    wasm64_append_section(&result, 3, &context->function_payload);
    wasm64_append_section(&result, 5, &context->memory_payload);
    wasm64_append_section(&result, 6, &context->global_payload);
    wasm64_append_section(&result, 7, &context->export_payload);
    wasm64_append_section(&result, 10, &context->code_payload);
    wasm64_append_section(&result, 11, &context->data_payload);
    *output = (ByteSlice){.pointer = result.data, .length = result.length};
    context->stats.binary_bytes = result.length;
    return true;
}

static void wasm64_context_initialize(Wasm64Context* context, Arena* arena, IrProgram* program, IrModule* modules, u32 module_count,
                                      Wasm64Options options)
{
    *context = (Wasm64Context){.arena = arena, .program = program, .modules = modules, .module_count = module_count, .options = options};
    context->error = (Wasm64Error){.code = WASM64_ERROR_NONE,
                                   .function = IR_FUNCTION_ID_INVALID,
                                   .block = IR_BLOCK_ID_INVALID,
                                   .instruction = IR_INSTRUCTION_ID_INVALID,
                                   .symbol = IR_SYMBOL_ID_INVALID,
                                   .opcode = IR_OPCODE_COUNT};
    context->stats = (Wasm64Stats){.memory64 = true, .deterministic = options.deterministic, .module_count = module_count};
    wasm64_buffer_init(&context->type_payload, arena);
    wasm64_buffer_init(&context->import_payload, arena);
    wasm64_buffer_init(&context->function_payload, arena);
    wasm64_buffer_init(&context->memory_payload, arena);
    wasm64_buffer_init(&context->global_payload, arena);
    wasm64_buffer_init(&context->export_payload, arena);
    wasm64_buffer_init(&context->code_payload, arena);
    wasm64_buffer_init(&context->data_payload, arena);
}

static bool wasm64_validate_inputs(Wasm64Context* context)
{
    if (!context->arena || !context->program || (!context->modules && context->module_count))
    {
        wasm64_fail(context, WASM64_ERROR_INVALID_ARGUMENT, wasm64_s8("invalid Wasm64 emitter input"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return false;
    }
    if (context->program->data_layout.pointer.size && context->program->data_layout.pointer.size != 8)
    {
        wasm64_fail(context, WASM64_ERROR_UNSUPPORTED_TYPE, wasm64_s8("Wasm64 requires a 64-bit canonical pointer layout"), 0, 0, 0,
                    IR_SYMBOL_ID_INVALID);
        return false;
    }
    for (u32 module_index = 0; module_index < context->module_count; module_index += 1)
    {
        IrModule* module = context->modules + module_index;
        IrValidationResult validation = ir_validate_canonical_module(context->program, module);
        if (validation.error != IR_VALIDATION_NONE)
        {
            IrFunction* function = validation.function.value < module->function_count ? module->functions + validation.function.value : 0;
            IrBlock* block = function && validation.block.value < function->block_count ? function->blocks + validation.block.value : 0;
            IrInstruction* instruction = function && validation.instruction.value < function->instruction_count ? function->instructions + validation.instruction.value : 0;
            wasm64_fail(context, WASM64_ERROR_IR_VALIDATION, wasm64_s8("canonical IR validation failed before Wasm64 emission"), function, block, instruction,
                        IR_SYMBOL_ID_INVALID);
            return false;
        }
    }
    return true;
}

Wasm64Artifact wasm64_emit_with_options(Arena* arena, IrProgram* program, IrModule* modules, u32 module_count, Wasm64Options options)
{
    Wasm64Context context = {0};
    wasm64_context_initialize(&context, arena, program, modules, module_count, options);
    ByteSlice bytes = {0};
    bool valid = wasm64_validate_inputs(&context);
    if (valid)
    {
        valid = wasm64_collect_functions(&context);
    }
    if (valid)
    {
        valid = wasm64_collect_data(&context);
    }
    if (valid)
    {
        valid = wasm64_apply_data_relocations(&context);
    }
    if (valid)
    {
        valid = wasm64_build_type_payload(&context) && wasm64_build_import_payload(&context) && wasm64_build_function_payload(&context) &&
                wasm64_build_memory_payload(&context) && wasm64_build_global_payload(&context) && wasm64_build_export_payload(&context) &&
                wasm64_build_code_payload(&context) && wasm64_build_data_payload(&context);
    }
    if (valid && !wasm64_failed(&context))
    {
        valid = wasm64_build_module(&context, &bytes);
    }
    if (!valid && context.error.code == WASM64_ERROR_NONE)
    {
        wasm64_fail(&context, WASM64_ERROR_ENCODING, wasm64_s8("Wasm64 module encoding failed"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
    }
    Wasm64Artifact artifact = {
        .bytes = valid && !wasm64_failed(&context) ? bytes : (ByteSlice){0},
        .wasm = valid && !wasm64_failed(&context) ? bytes : (ByteSlice){0},
        .binary = valid && !wasm64_failed(&context) ? bytes : (ByteSlice){0},
        .error = context.error,
        .stats = context.stats,
        .success = valid && !wasm64_failed(&context),
    };
    return artifact;
}

Wasm64Artifact wasm64_emit(Arena* arena, IrProgram* program, IrModule* modules, u32 module_count)
{
    Wasm64Options options = WASM64_OPTIONS_DEFAULT;
    return wasm64_emit_with_options(arena, program, modules, module_count, options);
}

Wasm64Artifact wasm64_emit_program(Arena* arena, IrProgram* program)
{
    if (!program)
    {
        Wasm64Options options = WASM64_OPTIONS_DEFAULT;
        return wasm64_emit_with_options(arena, 0, 0, 0, options);
    }
    return wasm64_emit(arena, program, program->modules, program->module_count);
}

bool wasm64_artifact_is_valid(Wasm64Artifact artifact)
{
    return artifact.success && artifact.error.code == WASM64_ERROR_NONE && artifact.bytes.pointer && artifact.bytes.length >= 8;
}

String8 wasm64_error_code_name(Wasm64ErrorCode code)
{
    switch (code)
    {
    case WASM64_ERROR_NONE: return wasm64_s8("none");
    case WASM64_ERROR_INVALID_ARGUMENT: return wasm64_s8("invalid_argument");
    case WASM64_ERROR_IR_VALIDATION: return wasm64_s8("ir_validation");
    case WASM64_ERROR_UNSUPPORTED_TYPE: return wasm64_s8("unsupported_type");
    case WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI: return wasm64_s8("unsupported_aggregate_abi");
    case WASM64_ERROR_VARIADIC: return wasm64_s8("variadic");
    case WASM64_ERROR_INDIRECT_CALL: return wasm64_s8("indirect_call");
    case WASM64_ERROR_ATOMIC: return wasm64_s8("atomic");
    case WASM64_ERROR_SIMD: return wasm64_s8("simd");
    case WASM64_ERROR_INLINE_ASSEMBLY: return wasm64_s8("inline_assembly");
    case WASM64_ERROR_TLS: return wasm64_s8("tls");
    case WASM64_ERROR_UNSUPPORTED_INSTRUCTION: return wasm64_s8("unsupported_instruction");
    case WASM64_ERROR_UNRESOLVED_SYMBOL: return wasm64_s8("unresolved_symbol");
    case WASM64_ERROR_DUPLICATE_SYMBOL: return wasm64_s8("duplicate_symbol");
    case WASM64_ERROR_ENCODING: return wasm64_s8("encoding");
    case WASM64_ERROR_COUNT: break;
    }
    return wasm64_s8("unknown");
}
