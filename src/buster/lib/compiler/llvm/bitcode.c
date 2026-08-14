#include <buster/lib/compiler/llvm/bitcode.h>

// LLVM's bitstream is LSB-first. The writer intentionally emits
// unabbreviated records: this keeps the implementation small and auditable,
// while remaining a fully conforming, self-describing LLVM bitcode stream.

enum
{
    LLVM_BC_END_BLOCK = 0,
    LLVM_BC_ENTER_SUBBLOCK = 1,
    LLVM_BC_DEFINE_ABBREV = 2,
    LLVM_BC_UNABBREV_RECORD = 3,

    LLVM_BC_MODULE_BLOCK = 8,
    LLVM_BC_CONSTANTS_BLOCK = 11,
    LLVM_BC_FUNCTION_BLOCK = 12,
    LLVM_BC_VALUE_SYMTAB_BLOCK = 14,
    LLVM_BC_TYPE_BLOCK = 17,

    LLVM_BC_MODULE_VERSION = 1,
    LLVM_BC_MODULE_TRIPLE = 2,
    LLVM_BC_MODULE_DATALAYOUT = 3,
    LLVM_BC_MODULE_GLOBALVAR = 7,
    LLVM_BC_MODULE_FUNCTION = 8,
    LLVM_BC_MODULE_SOURCE_FILENAME = 16,

    LLVM_BC_TYPE_NUMENTRY = 1,
    LLVM_BC_TYPE_VOID = 2,
    LLVM_BC_TYPE_FLOAT = 3,
    LLVM_BC_TYPE_DOUBLE = 4,
    LLVM_BC_TYPE_INTEGER = 7,
    LLVM_BC_TYPE_HALF = 10,
    LLVM_BC_TYPE_ARRAY = 11,
    LLVM_BC_TYPE_VECTOR = 12,
    LLVM_BC_TYPE_X86_FP80 = 13,
    LLVM_BC_TYPE_FP128 = 14,
    LLVM_BC_TYPE_STRUCT_ANON = 18,
    LLVM_BC_TYPE_FUNCTION = 21,
    LLVM_BC_TYPE_OPAQUE_POINTER = 25,

    LLVM_BC_VST_ENTRY = 1,
    LLVM_BC_VST_BBENTRY = 2,

    LLVM_BC_CST_SETTYPE = 1,
    LLVM_BC_CST_NULL = 2,
    LLVM_BC_CST_UNDEF = 3,
    LLVM_BC_CST_INTEGER = 4,
    LLVM_BC_CST_WIDE_INTEGER = 5,
    LLVM_BC_CST_FLOAT = 6,
    LLVM_BC_CST_AGGREGATE = 7,
    LLVM_BC_CST_STRING = 8,
    LLVM_BC_CST_CE_CAST = 11,
    LLVM_BC_CST_CE_GEP = 32,

    LLVM_BC_FUNC_DECLAREBLOCKS = 1,
    LLVM_BC_FUNC_BINOP = 2,
    LLVM_BC_FUNC_CAST = 3,
    LLVM_BC_FUNC_EXTRACTELT = 6,
    LLVM_BC_FUNC_INSERTELT = 7,
    LLVM_BC_FUNC_CMP2 = 28,
    LLVM_BC_FUNC_RET = 10,
    LLVM_BC_FUNC_BR = 11,
    LLVM_BC_FUNC_SWITCH = 12,
    LLVM_BC_FUNC_UNREACHABLE = 15,
    LLVM_BC_FUNC_PHI = 16,
    LLVM_BC_FUNC_ALLOCA = 19,
    LLVM_BC_FUNC_LOAD = 20,
    LLVM_BC_FUNC_EXTRACTVAL = 26,
    LLVM_BC_FUNC_INSERTVAL = 27,
    LLVM_BC_FUNC_INDIRECTBR = 31,
    LLVM_BC_FUNC_CALL = 34,
    LLVM_BC_FUNC_FENCE = 36,
    LLVM_BC_FUNC_LOADATOMIC = 41,
    LLVM_BC_FUNC_GEP = 43,
    LLVM_BC_FUNC_STORE = 44,
    LLVM_BC_FUNC_STOREATOMIC = 45,
    LLVM_BC_FUNC_CMPXCHG = 46,
    LLVM_BC_FUNC_UNOP = 56,
    LLVM_BC_FUNC_ATOMICRMW = 59,

    LLVM_BC_CAST_TRUNC = 0,
    LLVM_BC_CAST_ZEXT = 1,
    LLVM_BC_CAST_SEXT = 2,
    LLVM_BC_CAST_FPTOUI = 3,
    LLVM_BC_CAST_FPTOSI = 4,
    LLVM_BC_CAST_UITOFP = 5,
    LLVM_BC_CAST_SITOFP = 6,
    LLVM_BC_CAST_FPTRUNC = 7,
    LLVM_BC_CAST_FPEXT = 8,
    LLVM_BC_CAST_PTRTOINT = 9,
    LLVM_BC_CAST_INTTOPTR = 10,
    LLVM_BC_CAST_BITCAST = 11,

    LLVM_BC_BINOP_ADD = 0,
    LLVM_BC_BINOP_SUB = 1,
    LLVM_BC_BINOP_MUL = 2,
    LLVM_BC_BINOP_UDIV = 3,
    LLVM_BC_BINOP_SDIV = 4,
    LLVM_BC_BINOP_UREM = 5,
    LLVM_BC_BINOP_SREM = 6,
    LLVM_BC_BINOP_SHL = 7,
    LLVM_BC_BINOP_LSHR = 8,
    LLVM_BC_BINOP_ASHR = 9,
    LLVM_BC_BINOP_AND = 10,
    LLVM_BC_BINOP_OR = 11,
    LLVM_BC_BINOP_XOR = 12,

    LLVM_BC_ORDERING_MONOTONIC = 2,
    LLVM_BC_ORDERING_ACQUIRE = 3,
    LLVM_BC_ORDERING_RELEASE = 4,
    LLVM_BC_ORDERING_ACQREL = 5,
    LLVM_BC_ORDERING_SEQCST = 6,

    LLVM_BC_CALL_EXPLICIT_TYPE = 1 << 15,

    LLVM_BC_LINKAGE_EXTERNAL = 0,
    LLVM_BC_LINKAGE_INTERNAL = 3,
};

#define LLVM_BC_INVALID_ID UINT32_MAX

typedef struct LlvmBcBuffer LlvmBcBuffer;
struct LlvmBcBuffer
{
    Arena* arena;
    u8* data;
    u64 length;
    u64 capacity;
};

typedef struct LlvmBcBlockState LlvmBcBlockState;
struct LlvmBcBlockState
{
    u64 length_patch;
    u64 content_start;
    u32 abbreviation_width;
};

typedef struct LlvmBcBitstream LlvmBcBitstream;
struct LlvmBcBitstream
{
    LlvmBcBuffer buffer;
    LlvmBcBlockState blocks[32];
    u64 bit_position;
    u32 abbreviation_width;
    u32 depth;
    bool failed;
};

typedef struct LlvmBcTypeRecord LlvmBcTypeRecord;
struct LlvmBcTypeRecord
{
    u64* operands;
    u32 code;
    u32 operand_count;
};

typedef struct LlvmBcConstant LlvmBcConstant;
struct LlvmBcConstant
{
    u64* operands;
    u32 type_id;
    u32 code;
    u32 operand_count;
};

typedef struct LlvmBcGlobal LlvmBcGlobal;
struct LlvmBcGlobal
{
    IrGlobal* global;
    IrSymbol* symbol;
    String8 name;
    ByteSlice synthetic_bytes;
    IrTypeId canonical_type;
    u32 value_id;
    u32 storage_type_id;
    u32 initializer_value_id;
    u32 alignment;
    bool synthetic;
    bool declaration;
    bool read_only;
    bool is_thread_local;
};

typedef struct LlvmBcFunction LlvmBcFunction;
struct LlvmBcFunction
{
    IrFunction* function;
    IrSymbol* symbol;
    String8 name;
    IrTypeId canonical_type;
    IrBlock** block_order;
    u32* block_indices;
    u32 value_id;
    u32 type_id;
    u32 calling_convention;
    u32* value_ids;
    u32* value_type_ids;
    u32* emitted_counts;
    u32 first_local_value_id;
    u32 final_value_id;
    bool declaration;
    bool synthetic;
};

typedef struct LlvmBcString LlvmBcString;
struct LlvmBcString
{
    IrFunction* function;
    IrInstructionId instruction;
    IrValueId result;
    String8 bytes;
    u32 global_index;
};

typedef struct LlvmBcContext LlvmBcContext;
struct LlvmBcContext
{
    Arena* arena;
    IrProgram* program;
    IrModule* modules;
    u32 module_count;
    LlvmBitcodeOptions options;
    LlvmBitcodeError error;
    LlvmBitcodeStats stats;
    LlvmBcBitstream stream;

    LlvmBcTypeRecord* types;
    u32 type_count;
    u32 type_capacity;
    u32* ir_type_ids;
    u32 void_type_id;
    u32 pointer_type_id;
    u32 i1_type_id;
    u32 i8_type_id;
    u32 i32_type_id;
    u32 i64_type_id;

    LlvmBcConstant* constants;
    u32 constant_count;
    u32 constant_capacity;

    LlvmBcGlobal* globals;
    u32 global_count;
    u32 global_capacity;
    LlvmBcFunction* functions;
    u32 function_count;
    u32 function_capacity;
    LlvmBcString* strings;
    u32 string_count;
    u32 string_capacity;

    u32* symbol_value_ids;
    u8* symbol_seen;
    u32 module_value_count;
    bool constants_locked;
};

static String8 llvm_bc_s8(char8 const* pointer)
{
    String8 result = {.pointer = (char8*)pointer};
    while (pointer[result.length])
    {
        result.length += 1;
    }
    return result;
}

static void llvm_bc_fail(LlvmBcContext* context, LlvmBitcodeErrorCode code, String8 message, IrFunction* function, IrBlock* block,
                         IrInstruction* instruction, IrSymbolId symbol)
{
    if (context->error.code != LLVM_BITCODE_ERROR_NONE)
    {
        return;
    }
    context->error.code = code;
    context->error.message = message;
    context->error.diagnostic = message;
    context->error.function = function ? function->id : IR_FUNCTION_ID_INVALID;
    context->error.block = block ? block->id : IR_BLOCK_ID_INVALID;
    context->error.instruction = instruction ? instruction->id : IR_INSTRUCTION_ID_INVALID;
    context->error.symbol = symbol;
    context->error.opcode = instruction ? instruction->opcode : IR_OPCODE_COUNT;
}

static bool llvm_bc_failed(LlvmBcContext* context)
{
    return context->error.code != LLVM_BITCODE_ERROR_NONE || context->stream.failed;
}

static void llvm_bc_vec_reserve(Arena* arena, void** pointer, u32* capacity, u32 count, u32 element_size, u32 alignment)
{
    if (count <= *capacity)
    {
        return;
    }
    u32 next_capacity = *capacity ? *capacity : 16;
    while (next_capacity < count)
    {
        u32 next = next_capacity * 2;
        if (next < next_capacity || next > UINT32_MAX / element_size)
        {
            next_capacity = count;
            break;
        }
        next_capacity = next;
    }
    void* next = arena_allocate_bytes(arena, (u64)next_capacity * element_size, alignment);
    if (*pointer && *capacity)
    {
        memcpy(next, *pointer, (size_t)((u64)*capacity * element_size));
    }
    *pointer = next;
    *capacity = next_capacity;
}

static void llvm_bc_buffer_reserve(LlvmBcBuffer* buffer, u64 required_length)
{
    if (required_length <= buffer->capacity)
    {
        return;
    }
    u64 capacity = buffer->capacity ? buffer->capacity : 256;
    while (capacity < required_length)
    {
        u64 next = capacity * 2;
        if (next < capacity || next > ARENA_MAX_RESERVATION)
        {
            capacity = required_length;
            break;
        }
        capacity = next;
    }
    u8* data = (u8*)arena_allocate_bytes(buffer->arena, capacity, 1);
    if (buffer->length)
    {
        memcpy(data, buffer->data, (size_t)buffer->length);
    }
    if (capacity > buffer->length)
    {
        memset(data + buffer->length, 0, (size_t)(capacity - buffer->length));
    }
    buffer->data = data;
    buffer->capacity = capacity;
}

static void llvm_bc_write_bits(LlvmBcBitstream* stream, u64 value, u32 bit_count)
{
    if (stream->failed || bit_count > 64 || stream->bit_position > UINT64_MAX - bit_count)
    {
        stream->failed = true;
        return;
    }
    u64 end_bit = stream->bit_position + bit_count;
    u64 required = (end_bit + 7) / 8;
    llvm_bc_buffer_reserve(&stream->buffer, required);
    if (required > stream->buffer.length)
    {
        memset(stream->buffer.data + stream->buffer.length, 0, (size_t)(required - stream->buffer.length));
        stream->buffer.length = required;
    }
    for (u32 bit = 0; bit < bit_count; bit += 1)
    {
        u64 position = stream->bit_position + bit;
        u8 mask = (u8)(1u << (position & 7));
        if ((value >> bit) & 1)
        {
            stream->buffer.data[position >> 3] |= mask;
        }
        else
        {
            stream->buffer.data[position >> 3] &= (u8)~mask;
        }
    }
    stream->bit_position = end_bit;
}

static void llvm_bc_write_vbr(LlvmBcBitstream* stream, u64 value, u32 width)
{
    if (width < 2 || width > 32)
    {
        stream->failed = true;
        return;
    }
    u32 payload = width - 1;
    u64 mask = (UINT64_C(1) << payload) - 1;
    while (value > mask)
    {
        llvm_bc_write_bits(stream, (value & mask) | (UINT64_C(1) << payload), width);
        value >>= payload;
    }
    llvm_bc_write_bits(stream, value, width);
}

static void llvm_bc_align32(LlvmBcBitstream* stream)
{
    u64 aligned = (stream->bit_position + 31) & ~UINT64_C(31);
    if (aligned > stream->bit_position)
    {
        llvm_bc_write_bits(stream, 0, (u32)(aligned - stream->bit_position));
    }
}

static void llvm_bc_write_u32_at(LlvmBcBitstream* stream, u64 byte_offset, u32 value)
{
    if (byte_offset > stream->buffer.length || stream->buffer.length - byte_offset < 4)
    {
        stream->failed = true;
        return;
    }
    stream->buffer.data[byte_offset + 0] = (u8)value;
    stream->buffer.data[byte_offset + 1] = (u8)(value >> 8);
    stream->buffer.data[byte_offset + 2] = (u8)(value >> 16);
    stream->buffer.data[byte_offset + 3] = (u8)(value >> 24);
}

static void llvm_bc_enter_block(LlvmBcBitstream* stream, u32 block_id, u32 new_width)
{
    if (stream->depth >= (u32)(sizeof(stream->blocks) / sizeof(stream->blocks[0])))
    {
        stream->failed = true;
        return;
    }
    llvm_bc_write_bits(stream, LLVM_BC_ENTER_SUBBLOCK, stream->abbreviation_width);
    llvm_bc_write_vbr(stream, block_id, 8);
    llvm_bc_write_vbr(stream, new_width, 4);
    llvm_bc_align32(stream);
    LlvmBcBlockState* state = stream->blocks + stream->depth++;
    state->abbreviation_width = stream->abbreviation_width;
    state->length_patch = stream->bit_position / 8;
    llvm_bc_write_bits(stream, 0, 32);
    state->content_start = stream->bit_position / 8;
    stream->abbreviation_width = new_width;
}

static void llvm_bc_exit_block(LlvmBcBitstream* stream)
{
    if (!stream->depth)
    {
        stream->failed = true;
        return;
    }
    llvm_bc_write_bits(stream, LLVM_BC_END_BLOCK, stream->abbreviation_width);
    llvm_bc_align32(stream);
    LlvmBcBlockState state = stream->blocks[--stream->depth];
    u64 end = stream->bit_position / 8;
    if (end < state.content_start || ((end - state.content_start) & 3) || (end - state.content_start) / 4 > UINT32_MAX)
    {
        stream->failed = true;
        return;
    }
    llvm_bc_write_u32_at(stream, state.length_patch, (u32)((end - state.content_start) / 4));
    stream->abbreviation_width = state.abbreviation_width;
}

static void llvm_bc_record(LlvmBcBitstream* stream, u32 code, u64 const* operands, u32 operand_count)
{
    llvm_bc_write_bits(stream, LLVM_BC_UNABBREV_RECORD, stream->abbreviation_width);
    llvm_bc_write_vbr(stream, code, 6);
    llvm_bc_write_vbr(stream, operand_count, 6);
    for (u32 index = 0; index < operand_count; index += 1)
    {
        llvm_bc_write_vbr(stream, operands[index], 6);
    }
}

static void llvm_bc_string_record(LlvmBcBitstream* stream, u32 code, String8 string)
{
    llvm_bc_write_bits(stream, LLVM_BC_UNABBREV_RECORD, stream->abbreviation_width);
    llvm_bc_write_vbr(stream, code, 6);
    llvm_bc_write_vbr(stream, string.length, 6);
    for (u64 index = 0; index < string.length; index += 1)
    {
        llvm_bc_write_vbr(stream, (u8)string.pointer[index], 6);
    }
}

static u32 llvm_bc_add_type_record(LlvmBcContext* context, u32 code, u64 const* operands, u32 operand_count)
{
    for (u32 index = 0; index < context->type_count; index += 1)
    {
        LlvmBcTypeRecord* record = context->types + index;
        if (record->code != code || record->operand_count != operand_count)
        {
            continue;
        }
        bool equal = true;
        for (u32 operand = 0; operand < operand_count; operand += 1)
        {
            equal &= record->operands[operand] == operands[operand];
        }
        if (equal)
        {
            return index;
        }
    }
    llvm_bc_vec_reserve(context->arena, (void**)&context->types, &context->type_capacity, context->type_count + 1,
                        sizeof(*context->types), BUSTER_ALIGN_OF(LlvmBcTypeRecord));
    LlvmBcTypeRecord* record = context->types + context->type_count;
    *record = (LlvmBcTypeRecord){.code = code, .operand_count = operand_count};
    if (operand_count)
    {
        record->operands = arena_allocate(context->arena, u64, operand_count);
        memcpy(record->operands, operands, (size_t)operand_count * sizeof(*operands));
    }
    return context->type_count++;
}

static u32 llvm_bc_integer_type(LlvmBcContext* context, u32 width)
{
    u64 operand = width;
    return llvm_bc_add_type_record(context, LLVM_BC_TYPE_INTEGER, &operand, 1);
}

static u32 llvm_bc_array_type(LlvmBcContext* context, u64 count, u32 element_type)
{
    u64 operands[2] = {count, element_type};
    return llvm_bc_add_type_record(context, LLVM_BC_TYPE_ARRAY, operands, 2);
}

static IrType* llvm_bc_ir_type(LlvmBcContext* context, IrTypeId id)
{
    return ir_type_from_id(&context->program->types, id);
}

static IrSymbol* llvm_bc_ir_symbol(LlvmBcContext* context, IrSymbolId id)
{
    return ir_symbol_from_id(&context->program->symbols, id);
}

static bool llvm_bc_type_dependencies_ready(LlvmBcContext* context, IrType* type)
{
    if (!type)
    {
        return false;
    }
    switch (type->kind)
    {
    case IR_TYPE_ARRAY:
    case IR_TYPE_VECTOR:
        return type->element_type.value < context->program->types.count && context->ir_type_ids[type->element_type.value] != LLVM_BC_INVALID_ID;
    case IR_TYPE_FUNCTION:
    {
        if (type->return_type.value >= context->program->types.count || context->ir_type_ids[type->return_type.value] == LLVM_BC_INVALID_ID)
        {
            return false;
        }
        for (u32 parameter_index = 0; parameter_index < type->parameter_count; parameter_index += 1)
        {
            if (type->parameter_types[parameter_index].value >= context->program->types.count ||
                context->ir_type_ids[type->parameter_types[parameter_index].value] == LLVM_BC_INVALID_ID)
            {
                return false;
            }
        }
        return true;
    }
    case IR_TYPE_STRUCT:
    {
        for (u32 field_index = 0; field_index < type->field_count; field_index += 1)
        {
            if (type->fields[field_index].type.value >= context->program->types.count ||
                context->ir_type_ids[type->fields[field_index].type.value] == LLVM_BC_INVALID_ID)
            {
                return false;
            }
        }
        return true;
    }
    case IR_TYPE_ENUM:
        return type->unqualified_type.value == IR_ID_UNDERLYING_INVALID ||
               (type->unqualified_type.value < context->program->types.count &&
                context->ir_type_ids[type->unqualified_type.value] != LLVM_BC_INVALID_ID);
    case IR_TYPE_VOID:
    case IR_TYPE_BOOLEAN:
    case IR_TYPE_INTEGER:
    case IR_TYPE_FLOAT:
    case IR_TYPE_VA_LIST:
    case IR_TYPE_POINTER:
    case IR_TYPE_SLICE:
    case IR_TYPE_RANGE:
    case IR_TYPE_UNION:
    case IR_TYPE_COUNT:
        return true;
    }
    return false;
}

static bool llvm_bc_add_ir_type(LlvmBcContext* context, u32 type_index)
{
    IrType* type = context->program->types.types + type_index;
    if (context->ir_type_ids[type_index] != LLVM_BC_INVALID_ID)
    {
        return true;
    }
    if (type->is_atomic && type->unqualified_type.value < context->program->types.count &&
        context->ir_type_ids[type->unqualified_type.value] != LLVM_BC_INVALID_ID)
    {
        context->ir_type_ids[type_index] = context->ir_type_ids[type->unqualified_type.value];
        return true;
    }
    if (!llvm_bc_type_dependencies_ready(context, type))
    {
        return false;
    }
    u32 result = LLVM_BC_INVALID_ID;
    switch (type->kind)
    {
    case IR_TYPE_VOID:
        result = context->void_type_id;
        break;
    case IR_TYPE_BOOLEAN:
        result = context->i1_type_id;
        break;
    case IR_TYPE_INTEGER:
        if (!type->bit_width || type->bit_width > (1u << 23))
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("invalid LLVM integer width"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
            return false;
        }
        result = llvm_bc_integer_type(context, type->bit_width);
        break;
    case IR_TYPE_FLOAT:
        switch (type->bit_width)
        {
        case 16:
            result = llvm_bc_add_type_record(context, LLVM_BC_TYPE_HALF, 0, 0);
            break;
        case 32:
            result = llvm_bc_add_type_record(context, LLVM_BC_TYPE_FLOAT, 0, 0);
            break;
        case 64:
            result = llvm_bc_add_type_record(context, LLVM_BC_TYPE_DOUBLE, 0, 0);
            break;
        case 80:
            result = llvm_bc_add_type_record(context, LLVM_BC_TYPE_X86_FP80, 0, 0);
            break;
        case 128:
            result = llvm_bc_add_type_record(context, LLVM_BC_TYPE_FP128, 0, 0);
            break;
        default:
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("unsupported LLVM floating-point width"), 0, 0, 0,
                         IR_SYMBOL_ID_INVALID);
            return false;
        }
        break;
    case IR_TYPE_POINTER:
        result = context->pointer_type_id;
        break;
    case IR_TYPE_ARRAY:
    {
        u64 operands[2] = {type->element_count, context->ir_type_ids[type->element_type.value]};
        result = llvm_bc_add_type_record(context, LLVM_BC_TYPE_ARRAY, operands, 2);
        break;
    }
    case IR_TYPE_VECTOR:
    {
        u64 operands[2] = {type->element_count, context->ir_type_ids[type->element_type.value]};
        result = llvm_bc_add_type_record(context, LLVM_BC_TYPE_VECTOR, operands, 2);
        break;
    }
    case IR_TYPE_FUNCTION:
    {
        u32 count = type->parameter_count + 2;
        u64* operands = arena_allocate(context->arena, u64, count);
        operands[0] = type->is_variadic;
        operands[1] = context->ir_type_ids[type->return_type.value];
        for (u32 index = 0; index < type->parameter_count; index += 1)
        {
            operands[index + 2] = context->ir_type_ids[type->parameter_types[index].value];
        }
        result = llvm_bc_add_type_record(context, LLVM_BC_TYPE_FUNCTION, operands, count);
        break;
    }
    case IR_TYPE_STRUCT:
    {
        bool bit_fields = false;
        for (u32 index = 0; index < type->field_count; index += 1)
        {
            bit_fields |= type->fields[index].is_bit_field;
        }
        if (bit_fields)
        {
            result = llvm_bc_array_type(context, type->layout.size, context->i8_type_id);
        }
        else
        {
            u64* operands = arena_allocate(context->arena, u64, type->field_count + 1);
            operands[0] = 0;
            for (u32 index = 0; index < type->field_count; index += 1)
            {
                operands[index + 1] = context->ir_type_ids[type->fields[index].type.value];
            }
            result = llvm_bc_add_type_record(context, LLVM_BC_TYPE_STRUCT_ANON, operands, type->field_count + 1);
        }
        break;
    }
    case IR_TYPE_UNION:
    case IR_TYPE_VA_LIST:
    case IR_TYPE_SLICE:
    case IR_TYPE_RANGE:
        result = llvm_bc_array_type(context, type->layout.size, context->i8_type_id);
        break;
    case IR_TYPE_ENUM:
        if (type->unqualified_type.value < context->program->types.count)
        {
            result = context->ir_type_ids[type->unqualified_type.value];
        }
        else
        {
            u32 width = type->bit_width ? type->bit_width : (u32)(type->layout.size * 8);
            result = llvm_bc_integer_type(context, width ? width : 32);
        }
        break;
    case IR_TYPE_COUNT:
        break;
    }
    if (result == LLVM_BC_INVALID_ID)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("unsupported canonical type in LLVM bitcode"), 0, 0, 0,
                     IR_SYMBOL_ID_INVALID);
        return false;
    }
    context->ir_type_ids[type_index] = result;
    return true;
}

static bool llvm_bc_build_types(LlvmBcContext* context)
{
    context->void_type_id = llvm_bc_add_type_record(context, LLVM_BC_TYPE_VOID, 0, 0);
    u64 pointer_operand = 0;
    context->pointer_type_id = llvm_bc_add_type_record(context, LLVM_BC_TYPE_OPAQUE_POINTER, &pointer_operand, 1);
    context->i1_type_id = llvm_bc_integer_type(context, 1);
    context->i8_type_id = llvm_bc_integer_type(context, 8);
    context->i32_type_id = llvm_bc_integer_type(context, 32);
    context->i64_type_id = llvm_bc_integer_type(context, 64);

    u32 count = context->program->types.count;
    context->ir_type_ids = arena_allocate(context->arena, u32, count ? count : 1);
    for (u32 index = 0; index < count; index += 1)
    {
        context->ir_type_ids[index] = LLVM_BC_INVALID_ID;
    }
    u32 unresolved = count;
    while (unresolved)
    {
        u32 progress = 0;
        for (u32 index = 0; index < count; index += 1)
        {
            if (context->ir_type_ids[index] == LLVM_BC_INVALID_ID && llvm_bc_add_ir_type(context, index))
            {
                unresolved -= 1;
                progress += 1;
            }
            if (llvm_bc_failed(context))
            {
                return false;
            }
        }
        if (!progress)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("cyclic or unresolved LLVM type dependency"), 0, 0, 0,
                         IR_SYMBOL_ID_INVALID);
            return false;
        }
    }
    return true;
}

static bool llvm_bc_string_equal(String8 left, String8 right)
{
    return left.length == right.length && (!left.length || memcmp(left.pointer, right.pointer, (size_t)left.length) == 0);
}

static String8 llvm_bc_symbol_name(IrSymbol* symbol, String8 fallback)
{
    if (symbol)
    {
        if (symbol->link_name.length)
        {
            return symbol->link_name;
        }
        if (symbol->name.length)
        {
            return symbol->name;
        }
    }
    return fallback;
}

static String8 llvm_bc_generated_name(LlvmBcContext* context, char8 const* prefix, u32 index)
{
    u64 prefix_length = 0;
    while (prefix[prefix_length])
    {
        prefix_length += 1;
    }
    char8 digits[10];
    u32 digit_count = 0;
    do
    {
        digits[digit_count++] = (char8)('0' + index % 10);
        index /= 10;
    } while (index);
    char8* bytes = arena_allocate(context->arena, char8, prefix_length + digit_count);
    memcpy(bytes, prefix, (size_t)prefix_length);
    for (u32 digit = 0; digit < digit_count; digit += 1)
    {
        bytes[prefix_length + digit] = digits[digit_count - digit - 1];
    }
    return (String8){.pointer = bytes, .length = prefix_length + digit_count};
}

static u32 llvm_bc_alignment(u64 alignment)
{
    if (!alignment)
    {
        return 0;
    }
    if ((alignment & (alignment - 1)) || alignment > (UINT64_C(1) << 62))
    {
        return UINT32_MAX;
    }
    u32 encoded = 1;
    while (alignment > 1)
    {
        alignment >>= 1;
        encoded += 1;
    }
    return encoded;
}

static u32 llvm_bc_access_alignment(LlvmBcContext* context, IrValue* pointer_value, IrTypeId accessed_type)
{
    u64 alignment = pointer_value ? pointer_value->alignment : 0;
    if (!alignment)
    {
        IrType* type = llvm_bc_ir_type(context, accessed_type);
        if (type && type->layout.resolved)
        {
            alignment = type->layout.alignment;
        }
    }
    return llvm_bc_alignment(alignment);
}

static u32 llvm_bc_linkage(IrSymbol* symbol)
{
    return symbol && symbol->linkage == IR_LINKAGE_INTERNAL ? LLVM_BC_LINKAGE_INTERNAL : LLVM_BC_LINKAGE_EXTERNAL;
}

static u32 llvm_bc_calling_convention(IrCallingConvention convention)
{
    switch (convention)
    {
    case IR_CALLING_CONVENTION_C:
        return 0;
    case IR_CALLING_CONVENTION_SYSTEMV:
        return 78;
    case IR_CALLING_CONVENTION_WIN64:
        return 79;
    case IR_CALLING_CONVENTION_COUNT:
        break;
    }
    return UINT32_MAX;
}

static bool llvm_bc_name_available(LlvmBcContext* context, String8 name, IrSymbol* symbol)
{
    if (!name.length)
    {
        return true;
    }
    for (u32 index = 0; index < context->global_count; index += 1)
    {
        LlvmBcGlobal* global = context->globals + index;
        if (llvm_bc_string_equal(global->name, name) && global->symbol != symbol)
        {
            return false;
        }
    }
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        LlvmBcFunction* function = context->functions + index;
        if (llvm_bc_string_equal(function->name, name) && function->symbol != symbol)
        {
            return false;
        }
    }
    return true;
}

static bool llvm_bc_add_global_entity(LlvmBcContext* context, IrGlobal* global, IrSymbol* symbol, bool declaration)
{
    if (!symbol || symbol->kind != IR_SYMBOL_DATA || symbol->id.value >= context->program->symbols.count)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_INVALID_ARGUMENT, llvm_bc_s8("invalid data symbol for LLVM bitcode global"), 0, 0, 0,
                     symbol ? symbol->id : IR_SYMBOL_ID_INVALID);
        return false;
    }
    if (context->symbol_seen[symbol->id.value])
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_DUPLICATE_SYMBOL, llvm_bc_s8("duplicate LLVM bitcode data symbol"), 0, 0, 0, symbol->id);
        return false;
    }
    String8 name = llvm_bc_symbol_name(symbol, (String8){0});
    if (!name.length)
    {
        name = llvm_bc_generated_name(context, "__buster.global.", context->global_count);
    }
    if (!llvm_bc_name_available(context, name, symbol))
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_DUPLICATE_SYMBOL, llvm_bc_s8("duplicate LLVM bitcode linkage name"), 0, 0, 0, symbol->id);
        return false;
    }
    IrTypeId type = global ? global->type : symbol->type;
    if (type.value >= context->program->types.count)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("global has no canonical LLVM storage type"), 0, 0, 0, symbol->id);
        return false;
    }
    llvm_bc_vec_reserve(context->arena, (void**)&context->globals, &context->global_capacity, context->global_count + 1,
                        sizeof(*context->globals), BUSTER_ALIGN_OF(LlvmBcGlobal));
    LlvmBcGlobal* record = context->globals + context->global_count;
    *record = (LlvmBcGlobal){.global = global,
                             .symbol = symbol,
                             .name = name,
                             .canonical_type = type,
                             .value_id = LLVM_BC_INVALID_ID,
                             .storage_type_id = context->ir_type_ids[type.value],
                             .initializer_value_id = LLVM_BC_INVALID_ID,
                             .alignment = global ? global->alignment : 0,
                             .declaration = declaration,
                             .read_only = global ? global->is_read_only : false,
                             .is_thread_local = (global && global->is_thread_local) || symbol->is_thread_local};
    context->symbol_seen[symbol->id.value] = 1;
    context->global_count += 1;
    return true;
}

static bool llvm_bc_add_function_entity(LlvmBcContext* context, IrFunction* function, IrSymbol* symbol, bool declaration)
{
    if (!symbol || symbol->kind != IR_SYMBOL_FUNCTION || symbol->id.value >= context->program->symbols.count)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_INVALID_ARGUMENT, llvm_bc_s8("invalid function symbol for LLVM bitcode"), function, 0, 0,
                     symbol ? symbol->id : IR_SYMBOL_ID_INVALID);
        return false;
    }
    if (context->symbol_seen[symbol->id.value])
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_DUPLICATE_SYMBOL, llvm_bc_s8("duplicate LLVM bitcode function symbol"), function, 0, 0, symbol->id);
        return false;
    }
    IrTypeId type_id = function ? function->canonical_type : symbol->type;
    IrType* type = llvm_bc_ir_type(context, type_id);
    if (!type || type->kind != IR_TYPE_FUNCTION)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("function has no canonical LLVM function type"), function, 0, 0,
                     symbol->id);
        return false;
    }
    u32 calling_convention = llvm_bc_calling_convention(type->calling_convention);
    if (calling_convention == UINT32_MAX)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("unsupported LLVM calling convention"), function, 0, 0, symbol->id);
        return false;
    }
    String8 fallback = function ? function->name : (String8){0};
    String8 name = llvm_bc_symbol_name(symbol, fallback);
    if (!name.length)
    {
        name = llvm_bc_generated_name(context, "__buster.function.", context->function_count);
    }
    if (!llvm_bc_name_available(context, name, symbol))
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_DUPLICATE_SYMBOL, llvm_bc_s8("duplicate LLVM bitcode linkage name"), function, 0, 0, symbol->id);
        return false;
    }
    llvm_bc_vec_reserve(context->arena, (void**)&context->functions, &context->function_capacity, context->function_count + 1,
                        sizeof(*context->functions), BUSTER_ALIGN_OF(LlvmBcFunction));
    LlvmBcFunction* record = context->functions + context->function_count;
    *record = (LlvmBcFunction){.function = function,
                               .symbol = symbol,
                               .name = name,
                               .canonical_type = type_id,
                               .value_id = LLVM_BC_INVALID_ID,
                               .type_id = context->ir_type_ids[type_id.value],
                               .calling_convention = calling_convention,
                               .declaration = declaration};
    context->symbol_seen[symbol->id.value] = 1;
    context->function_count += 1;
    return true;
}

static bool llvm_bc_collect_entities(LlvmBcContext* context)
{
    u32 symbol_count = context->program->symbols.count;
    context->symbol_value_ids = arena_allocate(context->arena, u32, symbol_count ? symbol_count : 1);
    context->symbol_seen = arena_allocate(context->arena, u8, symbol_count ? symbol_count : 1);
    for (u32 index = 0; index < symbol_count; index += 1)
    {
        context->symbol_value_ids[index] = LLVM_BC_INVALID_ID;
        context->symbol_seen[index] = 0;
    }

    for (u32 module_index = 0; module_index < context->module_count; module_index += 1)
    {
        IrModule* module = context->modules + module_index;
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            IrGlobal* global = module->globals + global_index;
            IrSymbol* symbol = llvm_bc_ir_symbol(context, global->symbol);
            bool declaration = !symbol || !symbol->is_definition || global->initializer_kind == IR_GLOBAL_INITIALIZER_NONE;
            if (!llvm_bc_add_global_entity(context, global, symbol, declaration))
            {
                return false;
            }
        }
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            if (function->state == IR_FUNCTION_REJECTED)
            {
                llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("IR function was rejected before LLVM bitcode emission"), function,
                             0, 0, function->symbol);
                return false;
            }
            if (function->state == IR_FUNCTION_NOT_LOWERED)
            {
                continue;
            }
            IrSymbol* symbol = llvm_bc_ir_symbol(context, function->symbol);
            bool declaration = function->state == IR_FUNCTION_DECLARATION || !symbol || !symbol->is_definition;
            if (!llvm_bc_add_function_entity(context, function, symbol, declaration))
            {
                return false;
            }
        }
    }

    // Keep import discovery deterministic by walking stable symbol ids. This
    // also permits a canonical module to refer to a declaration without
    // carrying a redundant IrFunction/IrGlobal row for it.
    for (u32 symbol_index = 0; symbol_index < symbol_count; symbol_index += 1)
    {
        IrSymbol* symbol = context->program->symbols.symbols + symbol_index;
        if (context->symbol_seen[symbol_index] || symbol->kind == IR_SYMBOL_TYPE)
        {
            continue;
        }
        if (symbol->linkage != IR_LINKAGE_IMPORT && symbol->linkage != IR_LINKAGE_EXTERNAL)
        {
            continue;
        }
        if (symbol->kind == IR_SYMBOL_FUNCTION)
        {
            if (!llvm_bc_add_function_entity(context, 0, symbol, true))
            {
                return false;
            }
        }
        else if (symbol->kind == IR_SYMBOL_DATA)
        {
            if (!llvm_bc_add_global_entity(context, 0, symbol, true))
            {
                return false;
            }
        }
    }

    for (u32 module_index = 0; module_index < context->module_count; module_index += 1)
    {
        IrModule* module = context->modules + module_index;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            if (function->state != IR_FUNCTION_LOWERED)
            {
                continue;
            }
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                if (instruction->opcode != IR_OPCODE_CONSTANT_STRING)
                {
                    continue;
                }
                IrInstructionExtra extra = ir_instruction_extra(function, instruction->id);
                llvm_bc_vec_reserve(context->arena, (void**)&context->strings, &context->string_capacity, context->string_count + 1,
                                    sizeof(*context->strings), BUSTER_ALIGN_OF(LlvmBcString));
                LlvmBcString* string = context->strings + context->string_count;
                *string = (LlvmBcString){.function = function,
                                         .instruction = instruction->id,
                                         .result = instruction->result,
                                         .bytes = extra.literal,
                                         .global_index = context->global_count};

                llvm_bc_vec_reserve(context->arena, (void**)&context->globals, &context->global_capacity, context->global_count + 1,
                                    sizeof(*context->globals), BUSTER_ALIGN_OF(LlvmBcGlobal));
                LlvmBcGlobal* global = context->globals + context->global_count;
                u32 storage_type = llvm_bc_array_type(context, extra.literal.length, context->i8_type_id);
                String8 name = llvm_bc_generated_name(context, "__buster.str.", context->string_count);
                *global = (LlvmBcGlobal){.name = name,
                                         .synthetic_bytes = {.pointer = (u8*)extra.literal.pointer, .length = extra.literal.length},
                                         .canonical_type = IR_TYPE_ID_INVALID,
                                         .value_id = LLVM_BC_INVALID_ID,
                                         .storage_type_id = storage_type,
                                         .initializer_value_id = LLVM_BC_INVALID_ID,
                                         .alignment = 1,
                                         .synthetic = true,
                                         .read_only = true};
                context->global_count += 1;
                context->string_count += 1;
            }
        }
    }

    u32 value_id = 0;
    for (u32 index = 0; index < context->global_count; index += 1)
    {
        context->globals[index].value_id = value_id++;
        if (context->globals[index].symbol)
        {
            context->symbol_value_ids[context->globals[index].symbol->id.value] = context->globals[index].value_id;
        }
    }
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        context->functions[index].value_id = value_id++;
        if (context->functions[index].symbol)
        {
            context->symbol_value_ids[context->functions[index].symbol->id.value] = context->functions[index].value_id;
        }
    }
    context->module_value_count = value_id;
    context->stats.global_count = context->global_count;
    context->stats.function_count = context->function_count;
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        context->stats.defined_function_count += !context->functions[index].declaration;
    }
    return !llvm_bc_failed(context);
}

static u64 llvm_bc_encode_integer_bits(u64 bits, u32 width)
{
    if (!width || width > 64)
    {
        return bits << 1;
    }
    if (width < 64)
    {
        bits &= (UINT64_C(1) << width) - 1;
    }
    u64 sign = UINT64_C(1) << (width - 1);
    if (!(bits & sign))
    {
        return bits << 1;
    }
    if (bits == sign)
    {
        return 1;
    }
    u64 mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
    u64 magnitude = ((~bits) + 1) & mask;
    return (magnitude << 1) | 1;
}

static u32 llvm_bc_add_constant(LlvmBcContext* context, u32 type_id, u32 code, u64 const* operands, u32 operand_count)
{
    for (u32 index = 0; index < context->constant_count; index += 1)
    {
        LlvmBcConstant* constant = context->constants + index;
        if (constant->type_id != type_id || constant->code != code || constant->operand_count != operand_count)
        {
            continue;
        }
        bool equal = true;
        for (u32 operand = 0; operand < operand_count; operand += 1)
        {
            equal &= constant->operands[operand] == operands[operand];
        }
        if (equal)
        {
            return context->module_value_count + index;
        }
    }
    if (context->constants_locked)
    {
        String8 message = code == LLVM_BC_CST_NULL           ? llvm_bc_s8("LLVM null constant discovered after value numbering")
                          : code == LLVM_BC_CST_UNDEF        ? llvm_bc_s8("LLVM undefined constant discovered after value numbering")
                          : code == LLVM_BC_CST_INTEGER      ? llvm_bc_s8("LLVM integer constant discovered after value numbering")
                          : code == LLVM_BC_CST_WIDE_INTEGER ? llvm_bc_s8("LLVM wide-integer constant discovered after value numbering")
                          : code == LLVM_BC_CST_FLOAT        ? llvm_bc_s8("LLVM floating constant discovered after value numbering")
                          : code == LLVM_BC_CST_AGGREGATE    ? llvm_bc_s8("LLVM aggregate constant discovered after value numbering")
                          : code == LLVM_BC_CST_STRING       ? llvm_bc_s8("LLVM string constant discovered after value numbering")
                                                             : llvm_bc_s8("LLVM constant expression discovered after value numbering");
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_VALUE_NUMBERING, message, 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return LLVM_BC_INVALID_ID;
    }
    llvm_bc_vec_reserve(context->arena, (void**)&context->constants, &context->constant_capacity, context->constant_count + 1,
                        sizeof(*context->constants), BUSTER_ALIGN_OF(LlvmBcConstant));
    LlvmBcConstant* constant = context->constants + context->constant_count;
    *constant = (LlvmBcConstant){.type_id = type_id, .code = code, .operand_count = operand_count};
    if (operand_count)
    {
        constant->operands = arena_allocate(context->arena, u64, operand_count);
        memcpy(constant->operands, operands, (size_t)operand_count * sizeof(*operands));
    }
    return context->module_value_count + context->constant_count++;
}

static u32 llvm_bc_null_constant(LlvmBcContext* context, u32 type_id)
{
    return llvm_bc_add_constant(context, type_id, LLVM_BC_CST_NULL, 0, 0);
}

static u32 llvm_bc_undef_constant(LlvmBcContext* context, u32 type_id)
{
    return llvm_bc_add_constant(context, type_id, LLVM_BC_CST_UNDEF, 0, 0);
}

static u32 llvm_bc_integer_constant_for_type_id(LlvmBcContext* context, u32 type_id, u32 width, u64 bits)
{
    u64 operand = llvm_bc_encode_integer_bits(bits, width);
    return llvm_bc_add_constant(context, type_id, LLVM_BC_CST_INTEGER, &operand, 1);
}

static u32 llvm_bc_wide_integer_constant_for_type_id(LlvmBcContext* context, u32 type_id, u32 width, u64 magnitude, bool negative)
{
    if (width <= 64)
    {
        u64 bits = negative ? 0 - magnitude : magnitude;
        return llvm_bc_integer_constant_for_type_id(context, type_id, width, bits);
    }
    u32 word_count = (width + 63) / 64;
    u64* operands = arena_allocate(context->arena, u64, word_count);
    u64 low = negative ? 0 - magnitude : magnitude;
    u64 extension = negative && magnitude ? UINT64_MAX : 0;
    u32 active_words = 0;
    for (u32 index = 0; index < word_count; index += 1)
    {
        u64 word = index ? extension : low;
        if (index + 1 == word_count && (width & 63))
        {
            word &= (UINT64_C(1) << (width & 63)) - 1;
        }
        operands[index] = llvm_bc_encode_integer_bits(word, 64);
        if (word)
        {
            active_words = index + 1;
        }
    }
    return llvm_bc_add_constant(context, type_id, LLVM_BC_CST_WIDE_INTEGER, operands, active_words);
}

static u32 llvm_bc_float_constant(LlvmBcContext* context, IrType* type, u64 bits)
{
    if (!type || type->kind != IR_TYPE_FLOAT || (type->bit_width != 32 && type->bit_width != 64))
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("only f32/f64 LLVM bitcode constants are supported"), 0, 0, 0,
                     IR_SYMBOL_ID_INVALID);
        return LLVM_BC_INVALID_ID;
    }
    return llvm_bc_add_constant(context, context->ir_type_ids[type->id.value], LLVM_BC_CST_FLOAT, &bits, 1);
}

static u32 llvm_bc_value_type_id(LlvmBcContext* context, IrFunction* function, IrValueId value_id)
{
    if (!function || value_id.value >= function->value_count)
    {
        return LLVM_BC_INVALID_ID;
    }
    IrValue* value = function->values + value_id.value;
    if (value->category == IR_VALUE_PLACE)
    {
        return context->pointer_type_id;
    }
    IrType* type = llvm_bc_ir_type(context, value->canonical_type);
    if (!type)
    {
        return LLVM_BC_INVALID_ID;
    }
    if (type->kind == IR_TYPE_FUNCTION || type->kind == IR_TYPE_POINTER)
    {
        return context->pointer_type_id;
    }
    return context->ir_type_ids[value->canonical_type.value];
}

static u32 llvm_bc_integer_width(IrType* type)
{
    if (!type)
    {
        return 0;
    }
    if (type->kind == IR_TYPE_BOOLEAN)
    {
        return 1;
    }
    if (type->kind == IR_TYPE_ENUM && !type->bit_width)
    {
        return type->layout.size && type->layout.size <= 8 ? (u32)(type->layout.size * 8) : 32;
    }
    return type->bit_width;
}

static u32 llvm_bc_scalar_integer_constant(LlvmBcContext* context, IrType* type, u64 magnitude, bool negative)
{
    u32 width = llvm_bc_integer_width(type);
    if (!type || (type->kind != IR_TYPE_INTEGER && type->kind != IR_TYPE_BOOLEAN && type->kind != IR_TYPE_ENUM) || !width)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("invalid LLVM bitcode integer constant type"), 0, 0, 0,
                     IR_SYMBOL_ID_INVALID);
        return LLVM_BC_INVALID_ID;
    }
    return llvm_bc_wide_integer_constant_for_type_id(context, context->ir_type_ids[type->id.value], width, magnitude, negative);
}

static u32 llvm_bc_all_ones_constant(LlvmBcContext* context, IrType* type)
{
    if (!type)
    {
        return LLVM_BC_INVALID_ID;
    }
    if (type->kind == IR_TYPE_VECTOR)
    {
        IrType* element = llvm_bc_ir_type(context, type->element_type);
        u32 element_id = llvm_bc_all_ones_constant(context, element);
        if (element_id == LLVM_BC_INVALID_ID || type->element_count > UINT32_MAX)
        {
            return LLVM_BC_INVALID_ID;
        }
        u32 count = (u32)type->element_count;
        u64* operands = arena_allocate(context->arena, u64, count ? count : 1);
        for (u32 index = 0; index < count; index += 1)
        {
            operands[index] = element_id;
        }
        return llvm_bc_add_constant(context, context->ir_type_ids[type->id.value], LLVM_BC_CST_AGGREGATE, operands, count);
    }
    u32 width = llvm_bc_integer_width(type);
    if (!width)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("unsupported all-ones LLVM constant type"), 0, 0, 0,
                     IR_SYMBOL_ID_INVALID);
        return LLVM_BC_INVALID_ID;
    }
    if (width > 64)
    {
        return llvm_bc_wide_integer_constant_for_type_id(context, context->ir_type_ids[type->id.value], width, 1, true);
    }
    u64 bits = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
    return llvm_bc_integer_constant_for_type_id(context, context->ir_type_ids[type->id.value], width, bits);
}

static LlvmBcString* llvm_bc_string_for_instruction(LlvmBcContext* context, IrFunction* function, IrInstructionId instruction)
{
    for (u32 index = 0; index < context->string_count; index += 1)
    {
        LlvmBcString* string = context->strings + index;
        if (string->function == function && string->instruction.value == instruction.value)
        {
            return string;
        }
    }
    return 0;
}

static bool llvm_bc_prepare_global_initializers(LlvmBcContext* context)
{
    for (u32 index = 0; index < context->global_count; index += 1)
    {
        LlvmBcGlobal* record = context->globals + index;
        if (record->declaration)
        {
            record->initializer_value_id = LLVM_BC_INVALID_ID;
            continue;
        }
        if (record->synthetic)
        {
            u64* bytes = arena_allocate(context->arena, u64, record->synthetic_bytes.length ? record->synthetic_bytes.length : 1);
            for (u64 byte = 0; byte < record->synthetic_bytes.length; byte += 1)
            {
                bytes[byte] = record->synthetic_bytes.pointer[byte];
            }
            record->initializer_value_id = llvm_bc_add_constant(context, record->storage_type_id, LLVM_BC_CST_STRING, bytes,
                                                                (u32)record->synthetic_bytes.length);
            continue;
        }
        IrGlobal* global = record->global;
        IrType* type = llvm_bc_ir_type(context, record->canonical_type);
        if (!global || !type)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_INVALID_ARGUMENT, llvm_bc_s8("invalid LLVM bitcode global initializer"), 0, 0, 0,
                         record->symbol ? record->symbol->id : IR_SYMBOL_ID_INVALID);
            return false;
        }
        if (global->relocation_count)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_GLOBAL_INITIALIZER,
                         llvm_bc_s8("LLVM bitcode byte initializers with embedded relocations are unsupported"), 0, 0, 0, global->symbol);
            return false;
        }
        switch (global->initializer_kind)
        {
        case IR_GLOBAL_INITIALIZER_ZERO:
            record->initializer_value_id = llvm_bc_null_constant(context, record->storage_type_id);
            break;
        case IR_GLOBAL_INITIALIZER_INTEGER:
            record->initializer_value_id = llvm_bc_scalar_integer_constant(context, type, global->initializer_bits, global->initializer_is_negative);
            break;
        case IR_GLOBAL_INITIALIZER_FLOAT:
            record->initializer_value_id = llvm_bc_float_constant(context, type, global->initializer_bits);
            break;
        case IR_GLOBAL_INITIALIZER_BYTES:
        {
            if ((global->bytes.length && !global->bytes.pointer) || global->bytes.length > UINT32_MAX)
            {
                llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("invalid LLVM bitcode global byte initializer"), 0, 0, 0,
                             global->symbol);
                return false;
            }
            record->storage_type_id = llvm_bc_array_type(context, global->bytes.length, context->i8_type_id);
            u64* operands = arena_allocate(context->arena, u64, global->bytes.length ? global->bytes.length : 1);
            for (u64 byte = 0; byte < global->bytes.length; byte += 1)
            {
                operands[byte] = global->bytes.pointer[byte];
            }
            record->initializer_value_id = llvm_bc_add_constant(context, record->storage_type_id, LLVM_BC_CST_STRING, operands,
                                                                (u32)global->bytes.length);
            break;
        }
        case IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS:
            if (global->initializer_addend || global->initializer_symbol.value >= context->program->symbols.count ||
                context->symbol_value_ids[global->initializer_symbol.value] == LLVM_BC_INVALID_ID || type->kind != IR_TYPE_POINTER)
            {
                llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_GLOBAL_INITIALIZER,
                             llvm_bc_s8("LLVM bitcode symbol-address initializers require a resolved zero-addend pointer"), 0, 0, 0, global->symbol);
                return false;
            }
            record->initializer_value_id = context->symbol_value_ids[global->initializer_symbol.value];
            break;
        case IR_GLOBAL_INITIALIZER_NONE:
        case IR_GLOBAL_INITIALIZER_COUNT:
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_GLOBAL_INITIALIZER, llvm_bc_s8("missing LLVM bitcode global initializer"), 0, 0, 0,
                         global->symbol);
            return false;
        }
        if (record->initializer_value_id == LLVM_BC_INVALID_ID || llvm_bc_failed(context))
        {
            return false;
        }
    }
    return true;
}

static bool llvm_bc_collect_instruction_constants(LlvmBcContext* context)
{
    for (u32 function_index = 0; function_index < context->function_count; function_index += 1)
    {
        LlvmBcFunction* function_record = context->functions + function_index;
        IrFunction* function = function_record->function;
        if (function_record->declaration || !function)
        {
            continue;
        }
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = function->instructions + instruction_index;
            IrType* type = llvm_bc_ir_type(context, instruction->canonical_type);
            switch (instruction->opcode)
            {
            case IR_OPCODE_CONSTANT_INTEGER:
            case IR_OPCODE_ENUM:
                if (!instruction->immediate_count)
                {
                    llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("integer constant has no value"), function, 0, instruction,
                                 IR_SYMBOL_ID_INVALID);
                    return false;
                }
                llvm_bc_scalar_integer_constant(context, type, instruction->immediates[0], instruction->immediate_is_negative);
                break;
            case IR_OPCODE_CONSTANT_FLOAT:
                if (!instruction->immediate_count)
                {
                    llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("floating constant has no value"), function, 0, instruction,
                                 IR_SYMBOL_ID_INVALID);
                    return false;
                }
                llvm_bc_float_constant(context, type, instruction->immediates[0]);
                break;
            case IR_OPCODE_UNDEFINED:
                llvm_bc_undef_constant(context, llvm_bc_value_type_id(context, function, instruction->result));
                break;
            case IR_OPCODE_LOCAL:
                llvm_bc_integer_constant_for_type_id(context, context->i32_type_id, 32, 1);
                break;
            case IR_OPCODE_LENGTH:
                if (instruction->operand_count == 1)
                {
                    IrType* iterable = llvm_bc_ir_type(context, function->values[instruction->operands[0].value].canonical_type);
                    if (iterable && (iterable->kind == IR_TYPE_ARRAY || iterable->kind == IR_TYPE_VECTOR))
                    {
                        llvm_bc_scalar_integer_constant(context, type, iterable->element_count, false);
                    }
                }
                break;
            case IR_OPCODE_UNARY:
                if (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE || instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE)
                {
                    llvm_bc_null_constant(context, context->ir_type_ids[type->id.value]);
                }
                else if (instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT ||
                         instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_BITWISE_NOT || instruction->unary_operation == IR_UNARY_BOOLEAN_NOT)
                {
                    llvm_bc_all_ones_constant(context, type);
                }
                break;
            case IR_OPCODE_ARRAY:
            case IR_OPCODE_AGGREGATE:
                llvm_bc_undef_constant(context, context->ir_type_ids[type->id.value]);
                for (u32 operand = 0; operand < instruction->operand_count; operand += 1)
                {
                    llvm_bc_integer_constant_for_type_id(context, context->i32_type_id, 32, operand);
                }
                break;
            case IR_OPCODE_INDEX:
                llvm_bc_integer_constant_for_type_id(context, context->i64_type_id, 64, 0);
                break;
            case IR_OPCODE_FIELD:
                if (instruction->operand_count == 1 && instruction->immediate_count == 1)
                {
                    IrType* aggregate = llvm_bc_ir_type(context, function->values[instruction->operands[0].value].canonical_type);
                    if (aggregate && instruction->immediates[0] < aggregate->field_count)
                    {
                        llvm_bc_integer_constant_for_type_id(context, context->i64_type_id, 64,
                                                             aggregate->fields[instruction->immediates[0]].offset);
                    }
                }
                break;
            case IR_OPCODE_SWITCH:
                if (instruction->operand_count == 1)
                {
                    IrType* condition_type = llvm_bc_ir_type(context, function->values[instruction->operands[0].value].canonical_type);
                    for (u32 case_index = 0; case_index < instruction->immediate_count; case_index += 1)
                    {
                        llvm_bc_scalar_integer_constant(context, condition_type, instruction->immediates[case_index], false);
                    }
                }
                break;
            default:
                break;
            }
            if (llvm_bc_failed(context))
            {
                return false;
            }
        }
    }
    context->stats.constant_count = context->constant_count;
    return true;
}

static u64 llvm_bc_encode_signed_relative(s64 value)
{
    if (value >= 0)
    {
        return (u64)value << 1;
    }
    u64 magnitude = (u64)(-(value + 1)) + 1;
    return (magnitude << 1) | 1;
}

static u32 llvm_bc_symbol_value_id(LlvmBcContext* context, IrSymbolId symbol)
{
    if (symbol.value >= context->program->symbols.count)
    {
        return LLVM_BC_INVALID_ID;
    }
    return context->symbol_value_ids[symbol.value];
}

static u32 llvm_bc_function_block_index(LlvmBcContext* context, LlvmBcFunction* record, IrBlockId block)
{
    if (!record || !record->function || block.value >= record->function->block_count ||
        record->block_indices[block.value] == LLVM_BC_INVALID_ID)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_VALUE_NUMBERING, llvm_bc_s8("unresolved LLVM basic block id"),
                     record ? record->function : 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return LLVM_BC_INVALID_ID;
    }
    return record->block_indices[block.value];
}

static u32 llvm_bc_function_value_id(LlvmBcContext* context, LlvmBcFunction* record, IrValueId value)
{
    if (!record || !record->function || value.value >= record->function->value_count ||
        record->value_ids[value.value] == LLVM_BC_INVALID_ID)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_VALUE_NUMBERING, llvm_bc_s8("unresolved LLVM SSA value id"),
                     record ? record->function : 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return LLVM_BC_INVALID_ID;
    }
    return record->value_ids[value.value];
}

static u32 llvm_bc_function_value_type_id(LlvmBcContext* context, LlvmBcFunction* record, IrValueId value)
{
    if (!record || !record->function || value.value >= record->function->value_count ||
        record->value_type_ids[value.value] == LLVM_BC_INVALID_ID)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_VALUE_NUMBERING, llvm_bc_s8("unresolved LLVM SSA value type"),
                     record ? record->function : 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return LLVM_BC_INVALID_ID;
    }
    return record->value_type_ids[value.value];
}

static u32 llvm_bc_instruction_constant(LlvmBcContext* context, IrFunction* function, IrInstruction* instruction)
{
    IrType* type = llvm_bc_ir_type(context, instruction->canonical_type);
    switch (instruction->opcode)
    {
    case IR_OPCODE_CONSTANT_INTEGER:
    case IR_OPCODE_ENUM:
        return instruction->immediate_count == 1
                   ? llvm_bc_scalar_integer_constant(context, type, instruction->immediates[0], instruction->immediate_is_negative)
                   : LLVM_BC_INVALID_ID;
    case IR_OPCODE_CONSTANT_FLOAT:
        return instruction->immediate_count == 1 ? llvm_bc_float_constant(context, type, instruction->immediates[0]) : LLVM_BC_INVALID_ID;
    case IR_OPCODE_UNDEFINED:
        return llvm_bc_undef_constant(context, llvm_bc_value_type_id(context, function, instruction->result));
    case IR_OPCODE_LENGTH:
        if (instruction->operand_count == 1)
        {
            IrType* iterable = llvm_bc_ir_type(context, function->values[instruction->operands[0].value].canonical_type);
            if (iterable && (iterable->kind == IR_TYPE_ARRAY || iterable->kind == IR_TYPE_VECTOR))
            {
                return llvm_bc_scalar_integer_constant(context, type, iterable->element_count, false);
            }
        }
        break;
    case IR_OPCODE_ARRAY:
    case IR_OPCODE_AGGREGATE:
        if (!instruction->operand_count)
        {
            return llvm_bc_undef_constant(context, context->ir_type_ids[instruction->canonical_type.value]);
        }
        break;
    default:
        break;
    }
    return LLVM_BC_INVALID_ID;
}

static bool llvm_bc_cast_is_alias(LlvmBcContext* context, IrFunction* function, IrInstruction* instruction)
{
    if (instruction->operand_count != 1 || instruction->operands[0].value >= function->value_count)
    {
        return false;
    }
    switch (instruction->conversion_operation)
    {
    case IR_CONVERSION_IDENTITY:
    case IR_CONVERSION_INTEGER_REINTERPRET:
    case IR_CONVERSION_POINTER_REINTERPRET:
        return true;
    default:
        break;
    }
    u32 source = llvm_bc_value_type_id(context, function, instruction->operands[0]);
    u32 destination = llvm_bc_value_type_id(context, function, instruction->result);
    return source != LLVM_BC_INVALID_ID && source == destination;
}

static u32 llvm_bc_instruction_emitted_count(LlvmBcContext* context, IrFunction* function, IrBlock* block, IrInstruction* instruction)
{
    switch (instruction->opcode)
    {
    case IR_OPCODE_ARGUMENT:
    case IR_OPCODE_GLOBAL:
    case IR_OPCODE_CONSTANT_INTEGER:
    case IR_OPCODE_CONSTANT_FLOAT:
    case IR_OPCODE_CONSTANT_STRING:
    case IR_OPCODE_UNDEFINED:
    case IR_OPCODE_FUNCTION:
    case IR_OPCODE_LENGTH:
    case IR_OPCODE_ENUM:
    case IR_OPCODE_ADDRESS_OF:
    case IR_OPCODE_DEREFERENCE:
        return 0;
    case IR_OPCODE_LOCAL:
    case IR_OPCODE_STACK_ALLOCATE:
    case IR_OPCODE_LOAD:
    case IR_OPCODE_ATOMIC_LOAD:
    case IR_OPCODE_ATOMIC_READ_MODIFY_WRITE:
    case IR_OPCODE_FIELD:
    case IR_OPCODE_INDEX:
        return 1;
    case IR_OPCODE_ATOMIC_COMPARE_EXCHANGE:
        return 2;
    case IR_OPCODE_ARRAY:
        return instruction->operand_count;
    case IR_OPCODE_AGGREGATE:
    {
        IrType* aggregate = llvm_bc_ir_type(context, instruction->canonical_type);
        if (!aggregate || aggregate->kind != IR_TYPE_STRUCT)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION,
                         llvm_bc_s8("LLVM aggregate values currently require a non-bit-field struct"), function, block, instruction,
                         IR_SYMBOL_ID_INVALID);
            return LLVM_BC_INVALID_ID;
        }
        for (u32 field_index = 0; field_index < aggregate->field_count; field_index += 1)
        {
            if (aggregate->fields[field_index].is_bit_field)
            {
                llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION,
                             llvm_bc_s8("LLVM aggregate values currently require a non-bit-field struct"), function, block, instruction,
                             IR_SYMBOL_ID_INVALID);
                return LLVM_BC_INVALID_ID;
            }
        }
        return instruction->operand_count;
    }
    case IR_OPCODE_CAST:
        return llvm_bc_cast_is_alias(context, function, instruction) ? 0 : 1;
    case IR_OPCODE_UNARY:
        switch (instruction->unary_operation)
        {
        case IR_UNARY_INTEGER_NEGATE:
        case IR_UNARY_FLOAT_NEGATE:
        case IR_UNARY_INTEGER_BITWISE_NOT:
        case IR_UNARY_BOOLEAN_NOT:
        case IR_UNARY_VECTOR_INTEGER_NEGATE:
        case IR_UNARY_VECTOR_FLOAT_NEGATE:
        case IR_UNARY_VECTOR_INTEGER_BITWISE_NOT:
            return 1;
        case IR_UNARY_INTEGER_COUNT_LEADING_ZEROS:
        case IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS:
        case IR_UNARY_INTEGER_POPULATION_COUNT:
        case IR_UNARY_COUNT:
            break;
        }
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION,
                     llvm_bc_s8("LLVM bitcode integer-count intrinsics are not implemented"), function, block, instruction, IR_SYMBOL_ID_INVALID);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_BINARY:
        if (instruction->binary_operation == IR_BINARY_RANGE || instruction->binary_operation >= IR_BINARY_COUNT)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION,
                         llvm_bc_s8("LLVM bitcode range construction is not implemented"), function, block, instruction, IR_SYMBOL_ID_INVALID);
            return LLVM_BC_INVALID_ID;
        }
        return 1;
    case IR_OPCODE_CALL:
    {
        IrType* result = llvm_bc_ir_type(context, instruction->canonical_type);
        return result && result->kind != IR_TYPE_VOID ? 1 : 0;
    }
    case IR_OPCODE_STORE:
    case IR_OPCODE_ATOMIC_STORE:
    case IR_OPCODE_ATOMIC_FENCE:
    case IR_OPCODE_BRANCH:
    case IR_OPCODE_BRANCH_IF:
    case IR_OPCODE_SWITCH:
    case IR_OPCODE_RETURN:
    case IR_OPCODE_UNREACHABLE:
        return 0;
    case IR_OPCODE_STACK_SAVE:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode stack_save is not implemented"), function, block,
                     instruction, instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_STACK_RESTORE:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode stack_restore is not implemented"), function, block,
                     instruction, instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_CLEAR_INSTRUCTION_CACHE:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode clear_instruction_cache is not implemented"),
                     function, block, instruction, instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_SLICE:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode slice construction is not implemented"), function,
                     block, instruction, instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_REVERSE:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode aggregate reverse is not implemented"), function, block,
                     instruction, instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_VA_START:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode va_start is not implemented"), function, block,
                     instruction, instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_VA_COPY:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode va_copy is not implemented"), function, block, instruction,
                     instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_VA_END:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode va_end is not implemented"), function, block, instruction,
                     instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_VA_ARG:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode va_arg is not implemented"), function, block, instruction,
                     instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_INLINE_ASSEMBLY:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode inline assembly is not implemented"), function, block,
                     instruction, instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_SIMD:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode target SIMD lowering is not implemented"), function, block,
                     instruction, instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_LABEL_ADDRESS:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode label addresses are not implemented"), function, block,
                     instruction, instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_INDIRECT_BRANCH:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode indirect branches are not implemented"), function, block,
                     instruction, instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_DEBUG_TRAP:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM bitcode debug traps are not implemented"), function, block,
                     instruction, instruction->symbol);
        return LLVM_BC_INVALID_ID;
    case IR_OPCODE_COUNT:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("invalid canonical opcode in LLVM bitcode emitter"), function, block,
                     instruction, instruction->symbol);
        return LLVM_BC_INVALID_ID;
    }
    llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION,
                 llvm_bc_s8("unknown canonical opcode in LLVM bitcode emitter"), function, block, instruction, instruction->symbol);
    return LLVM_BC_INVALID_ID;
}

static bool llvm_bc_assign_alias(LlvmBcContext* context, LlvmBcFunction* record, IrInstruction* instruction)
{
    IrFunction* function = record->function;
    if (instruction->result.value == IR_ID_UNDERLYING_INVALID || record->value_ids[instruction->result.value] != LLVM_BC_INVALID_ID)
    {
        return true;
    }
    u32 value = LLVM_BC_INVALID_ID;
    switch (instruction->opcode)
    {
    case IR_OPCODE_CONSTANT_INTEGER:
    case IR_OPCODE_CONSTANT_FLOAT:
    case IR_OPCODE_UNDEFINED:
    case IR_OPCODE_LENGTH:
    case IR_OPCODE_ENUM:
        value = llvm_bc_instruction_constant(context, function, instruction);
        break;
    case IR_OPCODE_GLOBAL:
    case IR_OPCODE_FUNCTION:
        value = llvm_bc_symbol_value_id(context, instruction->symbol);
        break;
    case IR_OPCODE_CONSTANT_STRING:
    {
        LlvmBcString* string = llvm_bc_string_for_instruction(context, function, instruction->id);
        if (string && string->global_index < context->global_count)
        {
            value = context->globals[string->global_index].value_id;
        }
        break;
    }
    case IR_OPCODE_ARRAY:
    case IR_OPCODE_AGGREGATE:
        value = llvm_bc_instruction_constant(context, function, instruction);
        break;
    case IR_OPCODE_CAST:
        if (llvm_bc_cast_is_alias(context, function, instruction) && instruction->operand_count == 1)
        {
            value = record->value_ids[instruction->operands[0].value];
        }
        break;
    case IR_OPCODE_ADDRESS_OF:
    case IR_OPCODE_DEREFERENCE:
        if (instruction->operand_count == 1)
        {
            value = record->value_ids[instruction->operands[0].value];
        }
        break;
    default:
        return true;
    }
    if (value != LLVM_BC_INVALID_ID)
    {
        record->value_ids[instruction->result.value] = value;
        return true;
    }
    return false;
}

static bool llvm_bc_plan_function(LlvmBcContext* context, LlvmBcFunction* record)
{
    IrFunction* function = record->function;
    if (!function || record->declaration)
    {
        return true;
    }
    IrType* signature = llvm_bc_ir_type(context, record->canonical_type);
    if (!signature || signature->kind != IR_TYPE_FUNCTION || function->entry.value >= function->block_count)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("invalid function shape for LLVM bitcode"), function, 0, 0,
                     function->symbol);
        return false;
    }

    u32 block_count = function->block_count;
    record->block_order = arena_allocate(context->arena, IrBlock*, block_count ? block_count : 1);
    record->block_indices = arena_allocate(context->arena, u32, block_count ? block_count : 1);
    for (u32 index = 0; index < block_count; index += 1)
    {
        record->block_indices[index] = LLVM_BC_INVALID_ID;
    }
    u32 order_count = 0;
    IrBlock* entry = function->blocks + function->entry.value;
    record->block_order[order_count++] = entry;
    for (u32 index = 0; index < block_count; index += 1)
    {
        IrBlock* block = function->blocks + index;
        if (block != entry)
        {
            record->block_order[order_count++] = block;
        }
    }
    for (u32 index = 0; index < block_count; index += 1)
    {
        IrBlock* block = record->block_order[index];
        if (block->id.value >= block_count || record->block_indices[block->id.value] != LLVM_BC_INVALID_ID)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("invalid or duplicate canonical basic block id"), function, block, 0,
                         function->symbol);
            return false;
        }
        record->block_indices[block->id.value] = index;
    }

    u32 value_count = function->value_count;
    record->value_ids = arena_allocate(context->arena, u32, value_count ? value_count : 1);
    record->value_type_ids = arena_allocate(context->arena, u32, value_count ? value_count : 1);
    for (u32 index = 0; index < value_count; index += 1)
    {
        record->value_ids[index] = LLVM_BC_INVALID_ID;
        record->value_type_ids[index] = llvm_bc_value_type_id(context, function, (IrValueId){.value = index});
        if (record->value_type_ids[index] == LLVM_BC_INVALID_ID)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("canonical value has no LLVM type"), function, 0, 0,
                         function->symbol);
            return false;
        }
    }
    record->emitted_counts = arena_allocate(context->arena, u32, function->instruction_count ? function->instruction_count : 1);
    memset(record->emitted_counts, 0, (size_t)function->instruction_count * sizeof(*record->emitted_counts));

    record->first_local_value_id = context->module_value_count + context->constant_count;
    u8* argument_seen = arena_allocate(context->arena, u8, signature->parameter_count ? signature->parameter_count : 1);
    memset(argument_seen, 0, signature->parameter_count);
    for (u32 block_index = 0; block_index < block_count; block_index += 1)
    {
        IrBlock* block = record->block_order[block_index];
        IrInstructionId instruction_id = block->first_instruction;
        u32 walked = 0;
        while (instruction_id.value != IR_ID_UNDERLYING_INVALID)
        {
            if (instruction_id.value >= function->instruction_count || walked++ >= function->instruction_count)
            {
                llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("invalid canonical instruction chain"), function, block, 0,
                             function->symbol);
                return false;
            }
            IrInstruction* instruction = function->instructions + instruction_id.value;
            if (instruction->opcode == IR_OPCODE_ARGUMENT)
            {
                if (instruction->immediate_count != 1 || instruction->immediates[0] >= signature->parameter_count ||
                    instruction->result.value >= value_count || argument_seen[instruction->immediates[0]])
                {
                    llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("invalid or duplicate LLVM function argument"), function,
                                 block, instruction, function->symbol);
                    return false;
                }
                u32 argument = (u32)instruction->immediates[0];
                argument_seen[argument] = 1;
                record->value_ids[instruction->result.value] = record->first_local_value_id + argument;
            }
            if (instruction_id.value == block->last_instruction.value)
            {
                break;
            }
            instruction_id = instruction->next;
        }
    }
    for (u32 argument = 0; argument < signature->parameter_count; argument += 1)
    {
        if (!argument_seen[argument])
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("missing canonical LLVM function argument"), function, entry, 0,
                         function->symbol);
            return false;
        }
    }

    u32 next_value_id = record->first_local_value_id + signature->parameter_count;
    for (u32 block_index = 0; block_index < block_count; block_index += 1)
    {
        IrBlock* block = record->block_order[block_index];
        for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
        {
            if (parameter->value.value >= value_count || record->value_ids[parameter->value.value] != LLVM_BC_INVALID_ID)
            {
                llvm_bc_fail(context, LLVM_BITCODE_ERROR_VALUE_NUMBERING, llvm_bc_s8("invalid LLVM phi result value"), function, block, 0,
                             function->symbol);
                return false;
            }
            record->value_ids[parameter->value.value] = next_value_id++;
        }
        IrInstructionId instruction_id = block->first_instruction;
        u32 walked = 0;
        while (instruction_id.value != IR_ID_UNDERLYING_INVALID)
        {
            if (instruction_id.value >= function->instruction_count || walked++ >= function->instruction_count)
            {
                llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("invalid canonical instruction chain"), function, block, 0,
                             function->symbol);
                return false;
            }
            IrInstruction* instruction = function->instructions + instruction_id.value;
            u32 emitted = llvm_bc_instruction_emitted_count(context, function, block, instruction);
            if (emitted == LLVM_BC_INVALID_ID || llvm_bc_failed(context))
            {
                return false;
            }
            record->emitted_counts[instruction_id.value] = emitted;
            if (emitted)
            {
                if (instruction->result.value >= value_count || record->value_ids[instruction->result.value] != LLVM_BC_INVALID_ID ||
                    next_value_id > UINT32_MAX - emitted)
                {
                    llvm_bc_fail(context, LLVM_BITCODE_ERROR_VALUE_NUMBERING, llvm_bc_s8("invalid LLVM instruction result numbering"), function,
                                 block, instruction, function->symbol);
                    return false;
                }
                record->value_ids[instruction->result.value] = next_value_id + emitted - 1;
                next_value_id += emitted;
            }
            if (instruction_id.value == block->last_instruction.value)
            {
                break;
            }
            instruction_id = instruction->next;
        }
    }

    for (u32 pass = 0; pass <= function->instruction_count; pass += 1)
    {
        bool progress = false;
        bool unresolved = false;
        for (u32 index = 0; index < function->instruction_count; index += 1)
        {
            IrInstruction* instruction = function->instructions + index;
            if (instruction->result.value == IR_ID_UNDERLYING_INVALID || record->emitted_counts[index] ||
                record->value_ids[instruction->result.value] != LLVM_BC_INVALID_ID)
            {
                continue;
            }
            unresolved = true;
            if (llvm_bc_assign_alias(context, record, instruction))
            {
                progress = true;
            }
            if (llvm_bc_failed(context))
            {
                return false;
            }
        }
        if (!unresolved)
        {
            break;
        }
        if (!progress)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_VALUE_NUMBERING, llvm_bc_s8("unresolved LLVM alias value"), function, 0, 0,
                         function->symbol);
            return false;
        }
    }
    record->final_value_id = next_value_id;
    context->stats.block_count += function->block_count;
    context->stats.instruction_count += function->instruction_count;
    return true;
}

static bool llvm_bc_plan_functions(LlvmBcContext* context)
{
    context->constants_locked = true;
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        if (!llvm_bc_plan_function(context, context->functions + index))
        {
            return false;
        }
    }
    return !llvm_bc_failed(context);
}

static void llvm_bc_push_relative(u64* operands, u32* count, u32 current_value_id, u32 value_id)
{
    operands[(*count)++] = (u32)(current_value_id - value_id);
}

static void llvm_bc_push_value_and_type(u64* operands, u32* count, u32 current_value_id, u32 value_id, u32 type_id)
{
    operands[(*count)++] = (u32)(current_value_id - value_id);
    if (value_id >= current_value_id)
    {
        operands[(*count)++] = type_id;
    }
}

static u32 llvm_bc_memory_order(IrMemoryOrder order)
{
    switch (order)
    {
    case IR_MEMORY_ORDER_RELAXED:
        return LLVM_BC_ORDERING_MONOTONIC;
    case IR_MEMORY_ORDER_CONSUME:
    case IR_MEMORY_ORDER_ACQUIRE:
        return LLVM_BC_ORDERING_ACQUIRE;
    case IR_MEMORY_ORDER_RELEASE:
        return LLVM_BC_ORDERING_RELEASE;
    case IR_MEMORY_ORDER_ACQUIRE_RELEASE:
        return LLVM_BC_ORDERING_ACQREL;
    case IR_MEMORY_ORDER_SEQUENTIAL:
        return LLVM_BC_ORDERING_SEQCST;
    case IR_MEMORY_ORDER_COUNT:
        break;
    }
    return UINT32_MAX;
}

static u32 llvm_bc_atomic_operation(IrAtomicOperation operation)
{
    switch (operation)
    {
    case IR_ATOMIC_EXCHANGE:
        return 0;
    case IR_ATOMIC_ADD:
        return 1;
    case IR_ATOMIC_SUBTRACT:
        return 2;
    case IR_ATOMIC_BITWISE_AND:
        return 3;
    case IR_ATOMIC_BITWISE_OR:
        return 5;
    case IR_ATOMIC_BITWISE_XOR:
        return 6;
    case IR_ATOMIC_OPERATION_COUNT:
        break;
    }
    return UINT32_MAX;
}

static bool llvm_bc_binary_operation(IrBinaryOperation operation, u32* code, bool* comparison)
{
    *comparison = false;
    switch (operation)
    {
    case IR_BINARY_INTEGER_ADD:
    case IR_BINARY_FLOAT_ADD:
    case IR_BINARY_VECTOR_INTEGER_ADD:
    case IR_BINARY_VECTOR_FLOAT_ADD:
        *code = LLVM_BC_BINOP_ADD;
        return true;
    case IR_BINARY_INTEGER_SUBTRACT:
    case IR_BINARY_FLOAT_SUBTRACT:
    case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
    case IR_BINARY_VECTOR_FLOAT_SUBTRACT:
        *code = LLVM_BC_BINOP_SUB;
        return true;
    case IR_BINARY_INTEGER_MULTIPLY:
    case IR_BINARY_FLOAT_MULTIPLY:
    case IR_BINARY_VECTOR_INTEGER_MULTIPLY:
    case IR_BINARY_VECTOR_FLOAT_MULTIPLY:
        *code = LLVM_BC_BINOP_MUL;
        return true;
    case IR_BINARY_UNSIGNED_DIVIDE:
    case IR_BINARY_VECTOR_UNSIGNED_DIVIDE:
        *code = LLVM_BC_BINOP_UDIV;
        return true;
    case IR_BINARY_SIGNED_DIVIDE:
    case IR_BINARY_FLOAT_DIVIDE:
    case IR_BINARY_VECTOR_SIGNED_DIVIDE:
    case IR_BINARY_VECTOR_FLOAT_DIVIDE:
        *code = LLVM_BC_BINOP_SDIV;
        return true;
    case IR_BINARY_UNSIGNED_REMAINDER:
    case IR_BINARY_VECTOR_UNSIGNED_REMAINDER:
        *code = LLVM_BC_BINOP_UREM;
        return true;
    case IR_BINARY_SIGNED_REMAINDER:
    case IR_BINARY_VECTOR_SIGNED_REMAINDER:
        *code = LLVM_BC_BINOP_SREM;
        return true;
    case IR_BINARY_SHIFT_LEFT:
    case IR_BINARY_VECTOR_SHIFT_LEFT:
        *code = LLVM_BC_BINOP_SHL;
        return true;
    case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
    case IR_BINARY_VECTOR_UNSIGNED_SHIFT_RIGHT:
        *code = LLVM_BC_BINOP_LSHR;
        return true;
    case IR_BINARY_SIGNED_SHIFT_RIGHT:
    case IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT:
        *code = LLVM_BC_BINOP_ASHR;
        return true;
    case IR_BINARY_INTEGER_BITWISE_AND:
    case IR_BINARY_BOOLEAN_AND:
    case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
        *code = LLVM_BC_BINOP_AND;
        return true;
    case IR_BINARY_INTEGER_BITWISE_OR:
    case IR_BINARY_BOOLEAN_OR:
    case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
        *code = LLVM_BC_BINOP_OR;
        return true;
    case IR_BINARY_INTEGER_BITWISE_XOR:
    case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
        *code = LLVM_BC_BINOP_XOR;
        return true;
    default:
        *comparison = true;
        break;
    }
    switch (operation)
    {
    case IR_BINARY_INTEGER_EQUAL:
    case IR_BINARY_POINTER_EQUAL:
    case IR_BINARY_BOOLEAN_EQUAL:
    case IR_BINARY_VECTOR_INTEGER_EQUAL:
        *code = 32;
        return true;
    case IR_BINARY_INTEGER_NOT_EQUAL:
    case IR_BINARY_POINTER_NOT_EQUAL:
    case IR_BINARY_BOOLEAN_NOT_EQUAL:
    case IR_BINARY_VECTOR_INTEGER_NOT_EQUAL:
        *code = 33;
        return true;
    case IR_BINARY_UNSIGNED_GREATER:
    case IR_BINARY_VECTOR_UNSIGNED_GREATER:
        *code = 34;
        return true;
    case IR_BINARY_UNSIGNED_GREATER_EQUAL:
    case IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL:
        *code = 35;
        return true;
    case IR_BINARY_UNSIGNED_LESS:
    case IR_BINARY_VECTOR_UNSIGNED_LESS:
        *code = 36;
        return true;
    case IR_BINARY_UNSIGNED_LESS_EQUAL:
    case IR_BINARY_VECTOR_UNSIGNED_LESS_EQUAL:
        *code = 37;
        return true;
    case IR_BINARY_SIGNED_GREATER:
    case IR_BINARY_VECTOR_SIGNED_GREATER:
        *code = 38;
        return true;
    case IR_BINARY_SIGNED_GREATER_EQUAL:
    case IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL:
        *code = 39;
        return true;
    case IR_BINARY_SIGNED_LESS:
    case IR_BINARY_VECTOR_SIGNED_LESS:
        *code = 40;
        return true;
    case IR_BINARY_SIGNED_LESS_EQUAL:
    case IR_BINARY_VECTOR_SIGNED_LESS_EQUAL:
        *code = 41;
        return true;
    case IR_BINARY_FLOAT_EQUAL:
    case IR_BINARY_VECTOR_FLOAT_EQUAL:
        *code = 1;
        return true;
    case IR_BINARY_FLOAT_NOT_EQUAL:
    case IR_BINARY_VECTOR_FLOAT_NOT_EQUAL:
        *code = 14;
        return true;
    case IR_BINARY_FLOAT_GREATER:
    case IR_BINARY_VECTOR_FLOAT_GREATER:
        *code = 2;
        return true;
    case IR_BINARY_FLOAT_GREATER_EQUAL:
    case IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL:
        *code = 3;
        return true;
    case IR_BINARY_FLOAT_LESS:
    case IR_BINARY_VECTOR_FLOAT_LESS:
        *code = 4;
        return true;
    case IR_BINARY_FLOAT_LESS_EQUAL:
    case IR_BINARY_VECTOR_FLOAT_LESS_EQUAL:
        *code = 5;
        return true;
    default:
        return false;
    }
}

static u32 llvm_bc_cast_operation(IrConversionOperation operation)
{
    switch (operation)
    {
    case IR_CONVERSION_INTEGER_TRUNCATE:
        return LLVM_BC_CAST_TRUNC;
    case IR_CONVERSION_INTEGER_ZERO_EXTEND:
        return LLVM_BC_CAST_ZEXT;
    case IR_CONVERSION_INTEGER_SIGN_EXTEND:
        return LLVM_BC_CAST_SEXT;
    case IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER:
        return LLVM_BC_CAST_FPTOUI;
    case IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER:
        return LLVM_BC_CAST_FPTOSI;
    case IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT:
        return LLVM_BC_CAST_UITOFP;
    case IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT:
        return LLVM_BC_CAST_SITOFP;
    case IR_CONVERSION_FLOAT_TRUNCATE:
        return LLVM_BC_CAST_FPTRUNC;
    case IR_CONVERSION_FLOAT_EXTEND:
        return LLVM_BC_CAST_FPEXT;
    case IR_CONVERSION_POINTER_TO_INTEGER:
        return LLVM_BC_CAST_PTRTOINT;
    case IR_CONVERSION_INTEGER_TO_POINTER:
        return LLVM_BC_CAST_INTTOPTR;
    case IR_CONVERSION_IDENTITY:
    case IR_CONVERSION_INTEGER_REINTERPRET:
    case IR_CONVERSION_POINTER_REINTERPRET:
    case IR_CONVERSION_COUNT:
        break;
    }
    return UINT32_MAX;
}

static bool llvm_bc_emit_aggregate_instruction(LlvmBcContext* context, LlvmBcFunction* record, IrBlock* block,
                                                IrInstruction* instruction, u32* current_value_id)
{
    IrFunction* function = record->function;
    IrType* aggregate_type = llvm_bc_ir_type(context, instruction->canonical_type);
    u32 aggregate_llvm_type = context->ir_type_ids[instruction->canonical_type.value];
    u32 aggregate_value = llvm_bc_undef_constant(context, aggregate_llvm_type);
    if (aggregate_value == LLVM_BC_INVALID_ID)
    {
        return false;
    }
    bool vector = aggregate_type && aggregate_type->kind == IR_TYPE_VECTOR;
    for (u32 index = 0; index < instruction->operand_count; index += 1)
    {
        u32 value = llvm_bc_function_value_id(context, record, instruction->operands[index]);
        u32 value_type = llvm_bc_function_value_type_id(context, record, instruction->operands[index]);
        if (value == LLVM_BC_INVALID_ID || value_type == LLVM_BC_INVALID_ID)
        {
            return false;
        }
        u64 operands[8];
        u32 count = 0;
        llvm_bc_push_value_and_type(operands, &count, *current_value_id, aggregate_value, aggregate_llvm_type);
        llvm_bc_push_relative(operands, &count, *current_value_id, value);
        if (vector)
        {
            u32 index_value = llvm_bc_integer_constant_for_type_id(context, context->i32_type_id, 32, index);
            llvm_bc_push_relative(operands, &count, *current_value_id, index_value);
            llvm_bc_record(&context->stream, LLVM_BC_FUNC_INSERTELT, operands, count);
        }
        else
        {
            u64 field = instruction->opcode == IR_OPCODE_AGGREGATE ? instruction->immediates[index] : index;
            operands[count++] = field;
            llvm_bc_record(&context->stream, LLVM_BC_FUNC_INSERTVAL, operands, count);
        }
        aggregate_value = *current_value_id;
        *current_value_id += 1;
        if (llvm_bc_failed(context))
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_ENCODING, llvm_bc_s8("failed to encode LLVM aggregate instruction"), function, block,
                         instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
    }
    return true;
}

static bool llvm_bc_emit_gep(LlvmBcContext* context, LlvmBcFunction* record, IrBlock* block, IrInstruction* instruction,
                             u32 current_value_id)
{
    IrFunction* function = record->function;
    if (instruction->operand_count < 1)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("LLVM GEP has no base operand"), function, block, instruction,
                     IR_SYMBOL_ID_INVALID);
        return false;
    }
    IrValueId base_id = instruction->operands[0];
    IrValue* base_value = function->values + base_id.value;
    if (base_value->category != IR_VALUE_PLACE && llvm_bc_function_value_type_id(context, record, base_id) != context->pointer_type_id)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("LLVM GEP base is not an address"), function, block,
                     instruction, IR_SYMBOL_ID_INVALID);
        return false;
    }
    u32 base = llvm_bc_function_value_id(context, record, base_id);
    if (base == LLVM_BC_INVALID_ID)
    {
        return false;
    }
    u64 operands[12];
    u32 count = 0;
    operands[count++] = 0; // no inbounds promise
    if (instruction->opcode == IR_OPCODE_FIELD)
    {
        if (instruction->immediate_count != 1)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("invalid LLVM field GEP"), function, block, instruction,
                         IR_SYMBOL_ID_INVALID);
            return false;
        }
        IrType* aggregate = llvm_bc_ir_type(context, base_value->canonical_type);
        if (!aggregate || (aggregate->kind != IR_TYPE_STRUCT && aggregate->kind != IR_TYPE_UNION) ||
            instruction->immediates[0] >= aggregate->field_count)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("invalid LLVM aggregate field"), function, block, instruction,
                         IR_SYMBOL_ID_INVALID);
            return false;
        }
        u64 offset = aggregate->fields[instruction->immediates[0]].offset;
        u32 offset_value = llvm_bc_integer_constant_for_type_id(context, context->i64_type_id, 64, offset);
        operands[count++] = context->i8_type_id;
        llvm_bc_push_value_and_type(operands, &count, current_value_id, base, context->pointer_type_id);
        llvm_bc_push_value_and_type(operands, &count, current_value_id, offset_value, context->i64_type_id);
    }
    else
    {
        if (instruction->operand_count != 2)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("invalid LLVM index GEP"), function, block, instruction,
                         IR_SYMBOL_ID_INVALID);
            return false;
        }
        IrType* base_type = llvm_bc_ir_type(context, base_value->canonical_type);
        if (!base_type || (base_type->kind != IR_TYPE_POINTER && base_type->kind != IR_TYPE_ARRAY && base_type->kind != IR_TYPE_VECTOR))
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("invalid LLVM indexed type"), function, block, instruction,
                         IR_SYMBOL_ID_INVALID);
            return false;
        }
        u32 index = llvm_bc_function_value_id(context, record, instruction->operands[1]);
        u32 index_type = llvm_bc_function_value_type_id(context, record, instruction->operands[1]);
        if (index == LLVM_BC_INVALID_ID || index_type == LLVM_BC_INVALID_ID)
        {
            return false;
        }
        if (base_type->kind == IR_TYPE_POINTER)
        {
            operands[count++] = context->ir_type_ids[base_type->element_type.value];
            llvm_bc_push_value_and_type(operands, &count, current_value_id, base, context->pointer_type_id);
            llvm_bc_push_value_and_type(operands, &count, current_value_id, index, index_type);
        }
        else
        {
            u32 zero = llvm_bc_integer_constant_for_type_id(context, context->i64_type_id, 64, 0);
            operands[count++] = context->ir_type_ids[base_type->id.value];
            llvm_bc_push_value_and_type(operands, &count, current_value_id, base, context->pointer_type_id);
            llvm_bc_push_value_and_type(operands, &count, current_value_id, zero, context->i64_type_id);
            llvm_bc_push_value_and_type(operands, &count, current_value_id, index, index_type);
        }
    }
    llvm_bc_record(&context->stream, LLVM_BC_FUNC_GEP, operands, count);
    return !llvm_bc_failed(context);
}

static bool llvm_bc_emit_call(LlvmBcContext* context, LlvmBcFunction* record, IrBlock* block, IrInstruction* instruction,
                              u32 current_value_id)
{
    IrFunction* function = record->function;
    if (!instruction->operand_count)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("LLVM call has no callee"), function, block, instruction,
                     instruction->symbol);
        return false;
    }
    IrValue* callee_value = function->values + instruction->operands[0].value;
    IrType* callee_type = llvm_bc_ir_type(context, callee_value->canonical_type);
    IrType* signature = callee_type;
    if (callee_type && callee_type->kind == IR_TYPE_POINTER)
    {
        signature = llvm_bc_ir_type(context, callee_type->element_type);
    }
    if (!signature || signature->kind != IR_TYPE_FUNCTION)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("LLVM call target has no function signature"), function, block,
                     instruction, instruction->symbol);
        return false;
    }
    u32 calling_convention = llvm_bc_calling_convention(signature->calling_convention);
    if (calling_convention == UINT32_MAX)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("unsupported LLVM call convention"), function, block, instruction,
                     instruction->symbol);
        return false;
    }
    u32 maximum = instruction->operand_count * 2 + 5;
    u64* operands = arena_allocate(context->arena, u64, maximum);
    u32 count = 0;
    operands[count++] = 0; // parameter attribute list id
    operands[count++] = ((u64)calling_convention << 1) | LLVM_BC_CALL_EXPLICIT_TYPE;
    operands[count++] = context->ir_type_ids[signature->id.value];
    u32 callee = llvm_bc_function_value_id(context, record, instruction->operands[0]);
    llvm_bc_push_value_and_type(operands, &count, current_value_id, callee, context->pointer_type_id);
    for (u32 argument = 1; argument < instruction->operand_count; argument += 1)
    {
        u32 value = llvm_bc_function_value_id(context, record, instruction->operands[argument]);
        u32 type = llvm_bc_function_value_type_id(context, record, instruction->operands[argument]);
        if (argument <= signature->parameter_count)
        {
            llvm_bc_push_relative(operands, &count, current_value_id, value);
        }
        else
        {
            llvm_bc_push_value_and_type(operands, &count, current_value_id, value, type);
        }
    }
    llvm_bc_record(&context->stream, LLVM_BC_FUNC_CALL, operands, count);
    return !llvm_bc_failed(context);
}

static bool llvm_bc_emit_instruction(LlvmBcContext* context, LlvmBcFunction* record, IrBlock* block, IrInstruction* instruction,
                                     u32* current_value_id)
{
    IrFunction* function = record->function;
    u32 expected_count = record->emitted_counts[instruction->id.value];
    u32 initial_value_id = *current_value_id;
    u64 operands[24];
    u32 count = 0;
    switch (instruction->opcode)
    {
    case IR_OPCODE_ARGUMENT:
    case IR_OPCODE_GLOBAL:
    case IR_OPCODE_CONSTANT_INTEGER:
    case IR_OPCODE_CONSTANT_FLOAT:
    case IR_OPCODE_CONSTANT_STRING:
    case IR_OPCODE_UNDEFINED:
    case IR_OPCODE_FUNCTION:
    case IR_OPCODE_LENGTH:
    case IR_OPCODE_ENUM:
    case IR_OPCODE_ADDRESS_OF:
    case IR_OPCODE_DEREFERENCE:
        break;
    case IR_OPCODE_LOCAL:
    {
        IrType* allocated = llvm_bc_ir_type(context, instruction->canonical_type);
        IrValue* result = function->values + instruction->result.value;
        u32 alignment = llvm_bc_alignment(result->alignment ? result->alignment : allocated->layout.alignment);
        u32 size = llvm_bc_integer_constant_for_type_id(context, context->i32_type_id, 32, 1);
        if (alignment == UINT32_MAX)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("invalid LLVM local alignment"), function, block, instruction,
                         IR_SYMBOL_ID_INVALID);
            return false;
        }
        u64 record_operands[4] = {context->ir_type_ids[instruction->canonical_type.value], context->i32_type_id, size,
                                  UINT64_C(1) << 6 | alignment};
        llvm_bc_record(&context->stream, LLVM_BC_FUNC_ALLOCA, record_operands, 4);
        *current_value_id += 1;
        break;
    }
    case IR_OPCODE_STACK_ALLOCATE:
    {
        u32 size = llvm_bc_function_value_id(context, record, instruction->operands[0]);
        u32 size_type = llvm_bc_function_value_type_id(context, record, instruction->operands[0]);
        u32 alignment = llvm_bc_alignment(instruction->immediates[0]);
        if (alignment == UINT32_MAX)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("invalid LLVM stack allocation alignment"), function, block,
                         instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
        u64 record_operands[4] = {context->i8_type_id, size_type, size, UINT64_C(1) << 6 | alignment};
        llvm_bc_record(&context->stream, LLVM_BC_FUNC_ALLOCA, record_operands, 4);
        *current_value_id += 1;
        break;
    }
    case IR_OPCODE_LOAD:
    case IR_OPCODE_ATOMIC_LOAD:
    {
        IrValueId pointer_id = instruction->operands[0];
        IrValue* pointer_value = function->values + pointer_id.value;
        u32 pointer = llvm_bc_function_value_id(context, record, pointer_id);
        u32 result_type = context->ir_type_ids[instruction->canonical_type.value];
        u32 alignment = llvm_bc_access_alignment(context, pointer_value, instruction->canonical_type);
        if (alignment == UINT32_MAX || (instruction->opcode == IR_OPCODE_ATOMIC_LOAD && !alignment))
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE,
                         llvm_bc_s8(instruction->opcode == IR_OPCODE_ATOMIC_LOAD ? "LLVM atomic load requires a valid alignment"
                                                                                 : "invalid LLVM load alignment"),
                         function, block, instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
        llvm_bc_push_value_and_type(operands, &count, *current_value_id, pointer, context->pointer_type_id);
        operands[count++] = result_type;
        operands[count++] = alignment;
        operands[count++] = instruction->volatile_access || pointer_value->is_volatile;
        if (instruction->opcode == IR_OPCODE_ATOMIC_LOAD)
        {
            u32 ordering = llvm_bc_memory_order(instruction->memory_order);
            operands[count++] = ordering;
            operands[count++] = 1; // system synchronization scope
            llvm_bc_record(&context->stream, LLVM_BC_FUNC_LOADATOMIC, operands, count);
        }
        else
        {
            llvm_bc_record(&context->stream, LLVM_BC_FUNC_LOAD, operands, count);
        }
        *current_value_id += 1;
        break;
    }
    case IR_OPCODE_STORE:
    case IR_OPCODE_ATOMIC_STORE:
    {
        IrValueId pointer_id = instruction->operands[0];
        IrValue* pointer_value = function->values + pointer_id.value;
        u32 pointer = llvm_bc_function_value_id(context, record, pointer_id);
        u32 value = llvm_bc_function_value_id(context, record, instruction->operands[1]);
        u32 value_type = llvm_bc_function_value_type_id(context, record, instruction->operands[1]);
        IrTypeId value_ir_type = function->values[instruction->operands[1].value].canonical_type;
        u32 alignment = llvm_bc_access_alignment(context, pointer_value, value_ir_type);
        if (alignment == UINT32_MAX || (instruction->opcode == IR_OPCODE_ATOMIC_STORE && !alignment))
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE,
                         llvm_bc_s8(instruction->opcode == IR_OPCODE_ATOMIC_STORE ? "LLVM atomic store requires a valid alignment"
                                                                                  : "invalid LLVM store alignment"),
                         function, block, instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
        llvm_bc_push_value_and_type(operands, &count, *current_value_id, pointer, context->pointer_type_id);
        llvm_bc_push_value_and_type(operands, &count, *current_value_id, value, value_type);
        operands[count++] = alignment;
        operands[count++] = instruction->volatile_access || pointer_value->is_volatile;
        if (instruction->opcode == IR_OPCODE_ATOMIC_STORE)
        {
            operands[count++] = llvm_bc_memory_order(instruction->memory_order);
            operands[count++] = 1;
            llvm_bc_record(&context->stream, LLVM_BC_FUNC_STOREATOMIC, operands, count);
        }
        else
        {
            llvm_bc_record(&context->stream, LLVM_BC_FUNC_STORE, operands, count);
        }
        break;
    }
    case IR_OPCODE_ATOMIC_READ_MODIFY_WRITE:
    {
        IrType* result_type = llvm_bc_ir_type(context, instruction->canonical_type);
        if (result_type && result_type->kind == IR_TYPE_POINTER && instruction->atomic_operation != IR_ATOMIC_EXCHANGE)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION,
                         llvm_bc_s8("LLVM atomic pointer arithmetic is not implemented"), function, block, instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
        IrValue* pointer_value = function->values + instruction->operands[0].value;
        u32 pointer = llvm_bc_function_value_id(context, record, instruction->operands[0]);
        u32 value = llvm_bc_function_value_id(context, record, instruction->operands[1]);
        u32 value_type = llvm_bc_function_value_type_id(context, record, instruction->operands[1]);
        IrTypeId value_ir_type = function->values[instruction->operands[1].value].canonical_type;
        u32 alignment = llvm_bc_access_alignment(context, pointer_value, value_ir_type);
        u32 operation = llvm_bc_atomic_operation(instruction->atomic_operation);
        u32 ordering = llvm_bc_memory_order(instruction->memory_order);
        if (!alignment || alignment == UINT32_MAX || operation == UINT32_MAX || ordering == UINT32_MAX)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("invalid LLVM atomic read-modify-write"), function, block,
                         instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
        llvm_bc_push_value_and_type(operands, &count, *current_value_id, pointer, context->pointer_type_id);
        llvm_bc_push_value_and_type(operands, &count, *current_value_id, value, value_type);
        operands[count++] = operation;
        operands[count++] = instruction->volatile_access || pointer_value->is_volatile;
        operands[count++] = ordering;
        operands[count++] = 1;
        operands[count++] = alignment;
        llvm_bc_record(&context->stream, LLVM_BC_FUNC_ATOMICRMW, operands, count);
        *current_value_id += 1;
        break;
    }
    case IR_OPCODE_ATOMIC_COMPARE_EXCHANGE:
    {
        IrValue* pointer_value = function->values + instruction->operands[0].value;
        u32 pointer = llvm_bc_function_value_id(context, record, instruction->operands[0]);
        u32 expected = llvm_bc_function_value_id(context, record, instruction->operands[1]);
        u32 desired = llvm_bc_function_value_id(context, record, instruction->operands[2]);
        u32 value_type = llvm_bc_function_value_type_id(context, record, instruction->operands[1]);
        IrTypeId value_ir_type = function->values[instruction->operands[1].value].canonical_type;
        u32 alignment = llvm_bc_access_alignment(context, pointer_value, value_ir_type);
        u32 success_ordering = llvm_bc_memory_order(instruction->memory_order);
        u32 failure_ordering = llvm_bc_memory_order(instruction->failure_memory_order);
        if (!alignment || alignment == UINT32_MAX || success_ordering == UINT32_MAX || failure_ordering == UINT32_MAX)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("invalid LLVM atomic compare-exchange"), function, block,
                         instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
        llvm_bc_push_value_and_type(operands, &count, *current_value_id, pointer, context->pointer_type_id);
        llvm_bc_push_value_and_type(operands, &count, *current_value_id, expected, value_type);
        llvm_bc_push_relative(operands, &count, *current_value_id, desired);
        operands[count++] = instruction->volatile_access || pointer_value->is_volatile;
        operands[count++] = success_ordering;
        operands[count++] = 1;
        operands[count++] = failure_ordering;
        operands[count++] = 0; // strong compare-exchange
        operands[count++] = alignment;
        llvm_bc_record(&context->stream, LLVM_BC_FUNC_CMPXCHG, operands, count);
        *current_value_id += 1;
        u64 extract_operands[2] = {1, 0};
        llvm_bc_record(&context->stream, LLVM_BC_FUNC_EXTRACTVAL, extract_operands, 2);
        *current_value_id += 1;
        break;
    }
    case IR_OPCODE_ATOMIC_FENCE:
        operands[0] = llvm_bc_memory_order(instruction->memory_order);
        if (operands[0] == UINT32_MAX)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("invalid LLVM atomic fence ordering"), function, block,
                         instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
        // LLVM uses sync-scope id 0 for single-thread and 1 for system.
        // A C signal fence is therefore represented by a single-thread fence.
        operands[1] = instruction->atomic_signal_fence ? 0 : 1;
        llvm_bc_record(&context->stream, LLVM_BC_FUNC_FENCE, operands, 2);
        break;
    case IR_OPCODE_ARRAY:
    case IR_OPCODE_AGGREGATE:
        if (!llvm_bc_emit_aggregate_instruction(context, record, block, instruction, current_value_id))
        {
            return false;
        }
        break;
    case IR_OPCODE_FIELD:
    case IR_OPCODE_INDEX:
        if (!llvm_bc_emit_gep(context, record, block, instruction, *current_value_id))
        {
            return false;
        }
        *current_value_id += 1;
        break;
    case IR_OPCODE_CALL:
        if (!llvm_bc_emit_call(context, record, block, instruction, *current_value_id))
        {
            return false;
        }
        *current_value_id += expected_count;
        break;
    case IR_OPCODE_CAST:
        if (!llvm_bc_cast_is_alias(context, function, instruction))
        {
            u32 source = llvm_bc_function_value_id(context, record, instruction->operands[0]);
            u32 source_type = llvm_bc_function_value_type_id(context, record, instruction->operands[0]);
            u32 destination_type = context->ir_type_ids[instruction->canonical_type.value];
            u32 operation = llvm_bc_cast_operation(instruction->conversion_operation);
            if (operation == UINT32_MAX)
            {
                llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("unsupported LLVM cast operation"), function, block,
                             instruction, IR_SYMBOL_ID_INVALID);
                return false;
            }
            llvm_bc_push_value_and_type(operands, &count, *current_value_id, source, source_type);
            operands[count++] = destination_type;
            operands[count++] = operation;
            llvm_bc_record(&context->stream, LLVM_BC_FUNC_CAST, operands, count);
            *current_value_id += 1;
        }
        break;
    case IR_OPCODE_UNARY:
    {
        u32 source = llvm_bc_function_value_id(context, record, instruction->operands[0]);
        u32 source_type = llvm_bc_function_value_type_id(context, record, instruction->operands[0]);
        IrType* result_type = llvm_bc_ir_type(context, instruction->canonical_type);
        if (instruction->unary_operation == IR_UNARY_FLOAT_NEGATE || instruction->unary_operation == IR_UNARY_VECTOR_FLOAT_NEGATE)
        {
            llvm_bc_push_value_and_type(operands, &count, *current_value_id, source, source_type);
            operands[count++] = 0;
            llvm_bc_record(&context->stream, LLVM_BC_FUNC_UNOP, operands, count);
        }
        else
        {
            u32 constant = (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE ||
                            instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE)
                               ? llvm_bc_null_constant(context, context->ir_type_ids[result_type->id.value])
                               : llvm_bc_all_ones_constant(context, result_type);
            llvm_bc_push_value_and_type(operands, &count, *current_value_id, constant, source_type);
            llvm_bc_push_relative(operands, &count, *current_value_id, source);
            operands[count++] = (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE ||
                                 instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE)
                                    ? LLVM_BC_BINOP_SUB
                                    : LLVM_BC_BINOP_XOR;
            llvm_bc_record(&context->stream, LLVM_BC_FUNC_BINOP, operands, count);
        }
        *current_value_id += 1;
        break;
    }
    case IR_OPCODE_BINARY:
    {
        u32 operation = UINT32_MAX;
        bool comparison = false;
        if (!llvm_bc_binary_operation(instruction->binary_operation, &operation, &comparison))
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION, llvm_bc_s8("unsupported LLVM binary operation"), function, block,
                         instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
        u32 left = llvm_bc_function_value_id(context, record, instruction->operands[0]);
        u32 right = llvm_bc_function_value_id(context, record, instruction->operands[1]);
        u32 left_type = llvm_bc_function_value_type_id(context, record, instruction->operands[0]);
        llvm_bc_push_value_and_type(operands, &count, *current_value_id, left, left_type);
        llvm_bc_push_relative(operands, &count, *current_value_id, right);
        operands[count++] = operation;
        llvm_bc_record(&context->stream, comparison ? LLVM_BC_FUNC_CMP2 : LLVM_BC_FUNC_BINOP, operands, count);
        *current_value_id += 1;
        break;
    }
    case IR_OPCODE_BRANCH:
        operands[0] = llvm_bc_function_block_index(context, record, instruction->targets[0]);
        llvm_bc_record(&context->stream, LLVM_BC_FUNC_BR, operands, 1);
        break;
    case IR_OPCODE_BRANCH_IF:
    {
        u32 condition = llvm_bc_function_value_id(context, record, instruction->operands[0]);
        operands[0] = llvm_bc_function_block_index(context, record, instruction->targets[0]);
        operands[1] = llvm_bc_function_block_index(context, record, instruction->targets[1]);
        operands[2] = (u32)(*current_value_id - condition);
        llvm_bc_record(&context->stream, LLVM_BC_FUNC_BR, operands, 3);
        break;
    }
    case IR_OPCODE_SWITCH:
    {
        u32 maximum = instruction->immediate_count * 2 + 3;
        u64* switch_operands = arena_allocate(context->arena, u64, maximum);
        IrValueId condition_id = instruction->operands[0];
        IrType* condition_type = llvm_bc_ir_type(context, function->values[condition_id.value].canonical_type);
        u32 switch_count = 0;
        switch_operands[switch_count++] = llvm_bc_function_value_type_id(context, record, condition_id);
        switch_operands[switch_count++] = (u32)(*current_value_id - llvm_bc_function_value_id(context, record, condition_id));
        switch_operands[switch_count++] = llvm_bc_function_block_index(context, record, instruction->targets[0]);
        for (u32 case_index = 0; case_index < instruction->immediate_count; case_index += 1)
        {
            switch_operands[switch_count++] = llvm_bc_scalar_integer_constant(context, condition_type, instruction->immediates[case_index], false);
            switch_operands[switch_count++] = llvm_bc_function_block_index(context, record, instruction->targets[case_index + 1]);
        }
        llvm_bc_record(&context->stream, LLVM_BC_FUNC_SWITCH, switch_operands, switch_count);
        break;
    }
    case IR_OPCODE_RETURN:
        if (instruction->operand_count)
        {
            u32 value = llvm_bc_function_value_id(context, record, instruction->operands[0]);
            u32 type = llvm_bc_function_value_type_id(context, record, instruction->operands[0]);
            llvm_bc_push_value_and_type(operands, &count, *current_value_id, value, type);
            llvm_bc_record(&context->stream, LLVM_BC_FUNC_RET, operands, count);
        }
        else
        {
            llvm_bc_record(&context->stream, LLVM_BC_FUNC_RET, 0, 0);
        }
        break;
    case IR_OPCODE_UNREACHABLE:
        llvm_bc_record(&context->stream, LLVM_BC_FUNC_UNREACHABLE, 0, 0);
        break;
    case IR_OPCODE_STACK_SAVE:
    case IR_OPCODE_STACK_RESTORE:
    case IR_OPCODE_CLEAR_INSTRUCTION_CACHE:
    case IR_OPCODE_SLICE:
    case IR_OPCODE_REVERSE:
    case IR_OPCODE_VA_START:
    case IR_OPCODE_VA_COPY:
    case IR_OPCODE_VA_END:
    case IR_OPCODE_VA_ARG:
    case IR_OPCODE_INLINE_ASSEMBLY:
    case IR_OPCODE_SIMD:
    case IR_OPCODE_LABEL_ADDRESS:
    case IR_OPCODE_INDIRECT_BRANCH:
    case IR_OPCODE_DEBUG_TRAP:
    case IR_OPCODE_COUNT:
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION,
                     llvm_bc_s8("unsupported canonical opcode reached LLVM emission"), function, block, instruction, instruction->symbol);
        return false;
    }
    if (*current_value_id - initial_value_id != expected_count)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_VALUE_NUMBERING, llvm_bc_s8("LLVM instruction emission count changed after planning"), function,
                     block, instruction, instruction->symbol);
        return false;
    }
    if (expected_count && record->value_ids[instruction->result.value] != *current_value_id - 1)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_VALUE_NUMBERING, llvm_bc_s8("LLVM instruction result id does not match the plan"), function,
                     block, instruction, instruction->symbol);
        return false;
    }
    if (context->stream.failed)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_ENCODING, llvm_bc_s8("failed to write LLVM instruction record"), function, block, instruction,
                     instruction->symbol);
        return false;
    }
    return !llvm_bc_failed(context);
}

static bool llvm_bc_emit_function_body(LlvmBcContext* context, LlvmBcFunction* record)
{
    IrFunction* function = record->function;
    IrType* signature = llvm_bc_ir_type(context, record->canonical_type);
    llvm_bc_enter_block(&context->stream, LLVM_BC_FUNCTION_BLOCK, 5);
    u64 block_count = function->block_count;
    llvm_bc_record(&context->stream, LLVM_BC_FUNC_DECLAREBLOCKS, &block_count, 1);
    u32 current_value_id = record->first_local_value_id + signature->parameter_count;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        IrBlock* block = record->block_order[block_index];
        for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
        {
            u32 maximum = parameter->incoming_count * 2 + 1;
            u64* operands = arena_allocate(context->arena, u64, maximum ? maximum : 1);
            u32 count = 0;
            u32 type = llvm_bc_function_value_type_id(context, record, parameter->value);
            operands[count++] = type;
            for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
            {
                u32 value = llvm_bc_function_value_id(context, record, incoming->value);
                s64 difference = (s64)current_value_id - (s64)value;
                operands[count++] = llvm_bc_encode_signed_relative(difference);
                operands[count++] = llvm_bc_function_block_index(context, record, incoming->predecessor);
            }
            llvm_bc_record(&context->stream, LLVM_BC_FUNC_PHI, operands, count);
            if (record->value_ids[parameter->value.value] != current_value_id)
            {
                llvm_bc_fail(context, LLVM_BITCODE_ERROR_VALUE_NUMBERING, llvm_bc_s8("LLVM phi id does not match the plan"), function, block, 0,
                             function->symbol);
                return false;
            }
            current_value_id += 1;
        }
        IrInstructionId instruction_id = block->first_instruction;
        u32 walked = 0;
        while (instruction_id.value != IR_ID_UNDERLYING_INVALID)
        {
            if (instruction_id.value >= function->instruction_count || walked++ >= function->instruction_count)
            {
                llvm_bc_fail(context, LLVM_BITCODE_ERROR_IR_VALIDATION, llvm_bc_s8("invalid canonical instruction chain during LLVM emission"),
                             function, block, 0, function->symbol);
                return false;
            }
            IrInstruction* instruction = function->instructions + instruction_id.value;
            if (!llvm_bc_emit_instruction(context, record, block, instruction, &current_value_id))
            {
                return false;
            }
            if (instruction_id.value == block->last_instruction.value)
            {
                break;
            }
            instruction_id = instruction->next;
        }
    }
    if (current_value_id != record->final_value_id)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_VALUE_NUMBERING, llvm_bc_s8("LLVM function final value id does not match the plan"), function, 0,
                     0, function->symbol);
        return false;
    }
    llvm_bc_exit_block(&context->stream);
    return !llvm_bc_failed(context);
}

static bool llvm_bc_emit_type_block(LlvmBcContext* context)
{
    llvm_bc_enter_block(&context->stream, LLVM_BC_TYPE_BLOCK, 4);
    u64 type_count = context->type_count;
    llvm_bc_record(&context->stream, LLVM_BC_TYPE_NUMENTRY, &type_count, 1);
    for (u32 index = 0; index < context->type_count; index += 1)
    {
        LlvmBcTypeRecord* record = context->types + index;
        llvm_bc_record(&context->stream, record->code, record->operands, record->operand_count);
    }
    llvm_bc_exit_block(&context->stream);
    return !context->stream.failed;
}

static bool llvm_bc_emit_module_entities(LlvmBcContext* context)
{
    for (u32 index = 0; index < context->global_count; index += 1)
    {
        LlvmBcGlobal* global = context->globals + index;
        u32 alignment = llvm_bc_alignment(global->alignment);
        if (alignment == UINT32_MAX)
        {
            llvm_bc_fail(context, LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE, llvm_bc_s8("invalid LLVM global alignment"), 0, 0, 0,
                         global->symbol ? global->symbol->id : IR_SYMBOL_ID_INVALID);
            return false;
        }
        u64 initializer = 0;
        if (!global->declaration)
        {
            if (global->initializer_value_id == LLVM_BC_INVALID_ID || global->initializer_value_id == UINT32_MAX)
            {
                llvm_bc_fail(context, LLVM_BITCODE_ERROR_VALUE_NUMBERING, llvm_bc_s8("invalid LLVM global initializer id"), 0, 0, 0,
                             global->symbol ? global->symbol->id : IR_SYMBOL_ID_INVALID);
                return false;
            }
            initializer = (u64)global->initializer_value_id + 1;
        }
        u64 operands[8] = {
            global->storage_type_id,
            2 | (global->read_only ? 1 : 0), // explicit storage type, optionally constant
            initializer,
            llvm_bc_linkage(global->symbol),
            alignment,
            0, // section id
            0, // visibility
            global->is_thread_local ? 1 : 0,
        };
        llvm_bc_record(&context->stream, LLVM_BC_MODULE_GLOBALVAR, operands, 8);
    }
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        LlvmBcFunction* function = context->functions + index;
        u64 operands[8] = {
            function->type_id,
            function->calling_convention,
            function->declaration,
            llvm_bc_linkage(function->symbol),
            0, // parameter attribute list id
            0, // alignment
            0, // section id
            0, // visibility
        };
        llvm_bc_record(&context->stream, LLVM_BC_MODULE_FUNCTION, operands, 8);
    }
    return !context->stream.failed;
}

static bool llvm_bc_emit_constants_block(LlvmBcContext* context)
{
    if (!context->constant_count)
    {
        return true;
    }
    llvm_bc_enter_block(&context->stream, LLVM_BC_CONSTANTS_BLOCK, 4);
    u32 current_type = LLVM_BC_INVALID_ID;
    for (u32 index = 0; index < context->constant_count; index += 1)
    {
        LlvmBcConstant* constant = context->constants + index;
        if (constant->type_id != current_type)
        {
            u64 type = constant->type_id;
            llvm_bc_record(&context->stream, LLVM_BC_CST_SETTYPE, &type, 1);
            current_type = constant->type_id;
        }
        llvm_bc_record(&context->stream, constant->code, constant->operands, constant->operand_count);
    }
    llvm_bc_exit_block(&context->stream);
    return !context->stream.failed;
}

static bool llvm_bc_emit_named_value(LlvmBcContext* context, u32 value_id, String8 name)
{
    if (name.length > UINT32_MAX - 1 || (name.length && !name.pointer))
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_ENCODING, llvm_bc_s8("LLVM symbol name is too large"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return false;
    }
    u32 count = (u32)name.length + 1;
    u64* operands = arena_allocate(context->arena, u64, count);
    operands[0] = value_id;
    for (u32 index = 0; index < name.length; index += 1)
    {
        operands[index + 1] = (u8)name.pointer[index];
    }
    llvm_bc_record(&context->stream, LLVM_BC_VST_ENTRY, operands, count);
    return !context->stream.failed;
}

static bool llvm_bc_emit_value_symbol_table(LlvmBcContext* context)
{
    if (!context->global_count && !context->function_count)
    {
        return true;
    }
    llvm_bc_enter_block(&context->stream, LLVM_BC_VALUE_SYMTAB_BLOCK, 4);
    for (u32 index = 0; index < context->global_count; index += 1)
    {
        LlvmBcGlobal* global = context->globals + index;
        if (!llvm_bc_emit_named_value(context, global->value_id, global->name))
        {
            return false;
        }
    }
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        LlvmBcFunction* function = context->functions + index;
        if (!llvm_bc_emit_named_value(context, function->value_id, function->name))
        {
            return false;
        }
    }
    llvm_bc_exit_block(&context->stream);
    return !context->stream.failed;
}

static bool llvm_bc_emit_module(LlvmBcContext* context)
{
    llvm_bc_write_bits(&context->stream, 'B', 8);
    llvm_bc_write_bits(&context->stream, 'C', 8);
    llvm_bc_write_bits(&context->stream, 0xc0, 8);
    llvm_bc_write_bits(&context->stream, 0xde, 8);
    llvm_bc_enter_block(&context->stream, LLVM_BC_MODULE_BLOCK, 3);
    u64 version = 1;
    llvm_bc_record(&context->stream, LLVM_BC_MODULE_VERSION, &version, 1);
    if (context->options.target_triple.length)
    {
        llvm_bc_string_record(&context->stream, LLVM_BC_MODULE_TRIPLE, context->options.target_triple);
    }
    if (context->options.data_layout.length)
    {
        llvm_bc_string_record(&context->stream, LLVM_BC_MODULE_DATALAYOUT, context->options.data_layout);
    }
    if (context->options.source_filename.length)
    {
        llvm_bc_string_record(&context->stream, LLVM_BC_MODULE_SOURCE_FILENAME, context->options.source_filename);
    }
    if (!llvm_bc_emit_type_block(context) || !llvm_bc_emit_module_entities(context) || !llvm_bc_emit_constants_block(context))
    {
        return false;
    }
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        LlvmBcFunction* function = context->functions + index;
        if (!function->declaration && !llvm_bc_emit_function_body(context, function))
        {
            return false;
        }
    }
    if (!llvm_bc_emit_value_symbol_table(context))
    {
        return false;
    }
    llvm_bc_exit_block(&context->stream);
    if (context->stream.depth || context->stream.failed)
    {
        llvm_bc_fail(context, LLVM_BITCODE_ERROR_ENCODING, llvm_bc_s8("failed to finalize LLVM bitstream"), 0, 0, 0,
                     IR_SYMBOL_ID_INVALID);
        return false;
    }
    return true;
}

static bool llvm_bc_string_option_valid(String8 string)
{
    return !string.length || string.pointer;
}

LlvmBitcodeArtifact llvm_bitcode_emit_with_options(Arena* arena, IrProgram* program, IrModule* modules, u32 module_count,
                                                    LlvmBitcodeOptions options)
{
    LlvmBcContext context = {
        .arena = arena,
        .program = program,
        .modules = modules,
        .module_count = module_count,
        .options = options,
        .error = {
            .function = IR_FUNCTION_ID_INVALID,
            .block = IR_BLOCK_ID_INVALID,
            .instruction = IR_INSTRUCTION_ID_INVALID,
            .symbol = IR_SYMBOL_ID_INVALID,
            .opcode = IR_OPCODE_COUNT,
        },
        .stats = {
            .deterministic = options.deterministic,
            .module_count = module_count,
        },
    };
    context.stream.buffer.arena = arena;
    context.stream.abbreviation_width = 2;

    if (!arena || !program || (module_count && !modules) || !llvm_bc_string_option_valid(options.target_triple) ||
        !llvm_bc_string_option_valid(options.data_layout) || !llvm_bc_string_option_valid(options.source_filename))
    {
        llvm_bc_fail(&context, LLVM_BITCODE_ERROR_INVALID_ARGUMENT, llvm_bc_s8("invalid LLVM bitcode emitter arguments"), 0, 0, 0,
                     IR_SYMBOL_ID_INVALID);
    }
    if (!llvm_bc_failed(&context) && options.validate_ir)
    {
        for (u32 module_index = 0; module_index < module_count; module_index += 1)
        {
            IrValidationResult validation = ir_validate_canonical_module(program, modules + module_index);
            if (validation.error != IR_VALIDATION_NONE)
            {
                context.error.code = LLVM_BITCODE_ERROR_IR_VALIDATION;
                context.error.message = llvm_bc_s8("canonical IR validation failed before LLVM bitcode emission");
                context.error.diagnostic = context.error.message;
                context.error.function = validation.function;
                context.error.block = validation.block;
                context.error.instruction = validation.instruction;
                context.error.symbol = IR_SYMBOL_ID_INVALID;
                context.error.opcode = IR_OPCODE_COUNT;
                break;
            }
        }
    }
    if (!llvm_bc_failed(&context) &&
        (!llvm_bc_build_types(&context) || !llvm_bc_collect_entities(&context) || !llvm_bc_prepare_global_initializers(&context) ||
         !llvm_bc_collect_instruction_constants(&context) || !llvm_bc_plan_functions(&context) || !llvm_bc_emit_module(&context)))
    {
        if (context.error.code == LLVM_BITCODE_ERROR_NONE)
        {
            llvm_bc_fail(&context, LLVM_BITCODE_ERROR_ENCODING, llvm_bc_s8("LLVM bitcode emission failed"), 0, 0, 0,
                         IR_SYMBOL_ID_INVALID);
        }
    }
    context.stats.type_count = context.type_count;
    context.stats.constant_count = context.constant_count;
    context.stats.binary_bytes = context.stream.buffer.length;

    LlvmBitcodeArtifact artifact = {
        .error = context.error,
        .stats = context.stats,
    };
    if (!llvm_bc_failed(&context))
    {
        artifact.bytes = (ByteSlice){.pointer = context.stream.buffer.data, .length = context.stream.buffer.length};
        artifact.bitcode = artifact.bytes;
        artifact.binary = artifact.bytes;
        artifact.success = true;
    }
    return artifact;
}

LlvmBitcodeArtifact llvm_bitcode_emit(Arena* arena, IrProgram* program, IrModule* modules, u32 module_count)
{
    return llvm_bitcode_emit_with_options(arena, program, modules, module_count, LLVM_BITCODE_OPTIONS_DEFAULT);
}

LlvmBitcodeArtifact llvm_bitcode_emit_program(Arena* arena, IrProgram* program)
{
    if (!program)
    {
        return llvm_bitcode_emit(arena, program, 0, 0);
    }
    return llvm_bitcode_emit(arena, program, program->modules, program->module_count);
}

bool llvm_bitcode_artifact_is_valid(LlvmBitcodeArtifact artifact)
{
    return artifact.success && artifact.error.code == LLVM_BITCODE_ERROR_NONE && artifact.bytes.pointer && artifact.bytes.length >= 4 &&
           artifact.bytes.pointer[0] == 'B' && artifact.bytes.pointer[1] == 'C' && artifact.bytes.pointer[2] == 0xc0 &&
           artifact.bytes.pointer[3] == 0xde;
}

String8 llvm_bitcode_error_code_name(LlvmBitcodeErrorCode code)
{
    switch (code)
    {
    case LLVM_BITCODE_ERROR_NONE:
        return llvm_bc_s8("none");
    case LLVM_BITCODE_ERROR_INVALID_ARGUMENT:
        return llvm_bc_s8("invalid_argument");
    case LLVM_BITCODE_ERROR_IR_VALIDATION:
        return llvm_bc_s8("ir_validation");
    case LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE:
        return llvm_bc_s8("unsupported_type");
    case LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION:
        return llvm_bc_s8("unsupported_instruction");
    case LLVM_BITCODE_ERROR_UNSUPPORTED_GLOBAL_INITIALIZER:
        return llvm_bc_s8("unsupported_global_initializer");
    case LLVM_BITCODE_ERROR_UNRESOLVED_SYMBOL:
        return llvm_bc_s8("unresolved_symbol");
    case LLVM_BITCODE_ERROR_DUPLICATE_SYMBOL:
        return llvm_bc_s8("duplicate_symbol");
    case LLVM_BITCODE_ERROR_VALUE_NUMBERING:
        return llvm_bc_s8("value_numbering");
    case LLVM_BITCODE_ERROR_ENCODING:
        return llvm_bc_s8("encoding");
    case LLVM_BITCODE_ERROR_COUNT:
        return llvm_bc_s8("count");
    }
    return llvm_bc_s8("unknown");
}
