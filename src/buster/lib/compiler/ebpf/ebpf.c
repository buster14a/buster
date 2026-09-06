#include <buster/lib/compiler/ebpf/ebpf.h>

// Linux eBPF direct backend. The emitter deliberately owns its instruction,
// ELF, relocation, and BTF encoders: canonical IR remains target-neutral and
// the native Machine IR does not acquire verifier-specific constraints.

typedef struct EbpfBuffer EbpfBuffer;
struct EbpfBuffer
{
    Arena* arena;
    u8* data;
    u64 length;
    u64 capacity;
};

typedef struct EbpfRelocation EbpfRelocation;
struct EbpfRelocation
{
    u64 offset;
    u32 symbol_key;
    u32 type;
};

typedef struct EbpfSection EbpfSection;
struct EbpfSection
{
    String8 name;
    EbpfBuffer data;
    EbpfRelocation* relocations;
    u64 logical_size;
    u64 alignment;
    u64 entry_size;
    u64 flags;
    u32 type;
    u32 relocation_count;
    u32 relocation_capacity;
    u32 elf_index;
    u32 link;
    u32 info;
};

typedef struct EbpfSymbolRecord EbpfSymbolRecord;
struct EbpfSymbolRecord
{
    String8 name;
    IrSymbol* symbol;
    EbpfSection* section;
    u64 value;
    u64 size;
    u32 key;
    u32 section_index;
    u32 elf_index;
    u8 binding;
    u8 type;
    bool defined;
    bool synthetic;
};

typedef struct EbpfFunctionRecord EbpfFunctionRecord;
struct EbpfFunctionRecord
{
    IrFunction* function;
    IrSymbol* symbol;
    EbpfSection* section;
    u64 offset;
    u64 size;
    bool is_program;
};

typedef struct EbpfGlobalRecord EbpfGlobalRecord;
struct EbpfGlobalRecord
{
    IrGlobal* global;
    IrSymbol* symbol;
    EbpfSection* section;
    u64 offset;
    u64 size;
};

typedef struct EbpfStringRecord EbpfStringRecord;
struct EbpfStringRecord
{
    IrFunction* function;
    IrInstructionId instruction;
    EbpfSection* section;
    String8 literal;
    u64 offset;
    u32 symbol_key;
};

typedef struct EbpfBtfRecord EbpfBtfRecord;
struct EbpfBtfRecord
{
    u32 name_offset;
    u32 info;
    u32 size_or_type;
    EbpfBuffer extra;
};

typedef struct EbpfContext EbpfContext;
struct EbpfContext
{
    Arena* arena;
    IrProgram* program;
    IrModule* modules;
    u32 module_count;
    EbpfOptions options;
    EbpfError error;
    EbpfStats stats;

    EbpfSection* sections;
    u32 section_count;
    u32 section_capacity;

    EbpfFunctionRecord* functions;
    u32 function_count;
    u32 function_capacity;
    EbpfGlobalRecord* globals;
    u32 global_count;
    u32 global_capacity;
    EbpfStringRecord* strings;
    u32 string_count;
    u32 string_capacity;

    EbpfSymbolRecord* symbols;
    u32 symbol_count;
    u32 symbol_capacity;
    u32* ir_symbol_keys;
    u32 next_symbol_key;

    EbpfBtfRecord* btf_records;
    u32 btf_record_count;
    u32 btf_record_capacity;
    u32* btf_type_ids;
    EbpfBuffer btf_strings;
    u32 btf_u32_type;
};

typedef struct EbpfBranchPatch EbpfBranchPatch;
struct EbpfBranchPatch
{
    u32 instruction_index;
    IrBlockId target;
};

typedef struct EbpfFunctionEmitter EbpfFunctionEmitter;
struct EbpfFunctionEmitter
{
    EbpfContext* context;
    EbpfFunctionRecord* record;
    IrFunction* function;
    EbpfSection* section;
    u64 section_start;
    s16* value_slots;
    s16* local_offsets;
    u32* use_counts;
    u32* block_starts;
    EbpfBranchPatch* patches;
    u32 patch_count;
    u32 patch_capacity;
    s16 temporary_base;
    u32 temporary_count;
    u32 frame_size;
};

enum
{
    EBPF_ELFCLASS64 = 2,
    EBPF_ELFDATA2LSB = 1,
    EBPF_ET_REL = 1,
    EBPF_EM_BPF = 247,
    EBPF_EV_CURRENT = 1,

    EBPF_SHT_NULL = 0,
    EBPF_SHT_PROGBITS = 1,
    EBPF_SHT_SYMTAB = 2,
    EBPF_SHT_STRTAB = 3,
    EBPF_SHT_NOBITS = 8,
    EBPF_SHT_REL = 9,

    EBPF_SHF_WRITE = 1,
    EBPF_SHF_ALLOC = 2,
    EBPF_SHF_EXECINSTR = 4,

    EBPF_STB_LOCAL = 0,
    EBPF_STB_GLOBAL = 1,
    EBPF_STT_NOTYPE = 0,
    EBPF_STT_OBJECT = 1,
    EBPF_STT_FUNC = 2,
    EBPF_STT_SECTION = 3,

    EBPF_R_BPF_64_64 = 1,
    EBPF_R_BPF_64_ABS64 = 2,
    EBPF_R_BPF_64_32 = 10,

    EBPF_BTF_KIND_INT = 1,
    EBPF_BTF_KIND_PTR = 2,
    EBPF_BTF_KIND_ARRAY = 3,
    EBPF_BTF_KIND_STRUCT = 4,
    EBPF_BTF_KIND_UNION = 5,
    EBPF_BTF_KIND_ENUM = 6,
    EBPF_BTF_KIND_VAR = 14,
    EBPF_BTF_KIND_DATASEC = 15,
    EBPF_BTF_KIND_FLOAT = 16,
    EBPF_BTF_KIND_ENUM64 = 19,
    EBPF_BTF_VAR_GLOBAL_ALLOCATED = 1,
    EBPF_BTF_INT_SIGNED = 1,
    EBPF_BTF_INT_CHAR = 2,
    EBPF_BTF_INT_BOOL = 4,
};

enum
{
    EBPF_CLASS_LD = 0x00,
    EBPF_CLASS_LDX = 0x01,
    EBPF_CLASS_ST = 0x02,
    EBPF_CLASS_STX = 0x03,
    EBPF_CLASS_ALU = 0x04,
    EBPF_CLASS_JMP = 0x05,
    EBPF_CLASS_JMP32 = 0x06,
    EBPF_CLASS_ALU64 = 0x07,
    EBPF_SIZE_W = 0x00,
    EBPF_SIZE_H = 0x08,
    EBPF_SIZE_B = 0x10,
    EBPF_SIZE_DW = 0x18,
    EBPF_MODE_IMM = 0x00,
    EBPF_MODE_MEM = 0x60,
    EBPF_SRC_K = 0x00,
    EBPF_SRC_X = 0x08,
    EBPF_OP_ADD = 0x00,
    EBPF_OP_SUB = 0x10,
    EBPF_OP_MUL = 0x20,
    EBPF_OP_DIV = 0x30,
    EBPF_OP_OR = 0x40,
    EBPF_OP_AND = 0x50,
    EBPF_OP_LSH = 0x60,
    EBPF_OP_RSH = 0x70,
    EBPF_OP_NEG = 0x80,
    EBPF_OP_MOD = 0x90,
    EBPF_OP_XOR = 0xa0,
    EBPF_OP_MOV = 0xb0,
    EBPF_OP_ARSH = 0xc0,
    EBPF_JA = 0x00,
    EBPF_JEQ = 0x10,
    EBPF_JGT = 0x20,
    EBPF_JGE = 0x30,
    EBPF_JSET = 0x40,
    EBPF_JNE = 0x50,
    EBPF_JSGT = 0x60,
    EBPF_JSGE = 0x70,
    EBPF_CALL = 0x80,
    EBPF_EXIT = 0x90,
    EBPF_JLT = 0xa0,
    EBPF_JLE = 0xb0,
    EBPF_JSLT = 0xc0,
    EBPF_JSLE = 0xd0,
    EBPF_REG_0 = 0,
    EBPF_REG_1 = 1,
    EBPF_REG_2 = 2,
    EBPF_REG_3 = 3,
    EBPF_REG_4 = 4,
    EBPF_REG_5 = 5,
    EBPF_REG_6 = 6,
    EBPF_REG_7 = 7,
    EBPF_REG_8 = 8,
    EBPF_REG_9 = 9,
    EBPF_REG_FP = 10,
    EBPF_PSEUDO_CALL = 1,
    EBPF_PSEUDO_MAP_FD = 1,
};

#define EBPF_SLOT_NONE INT16_MIN

static String8 ebpf_s8(char8 const* pointer)
{
    String8 result = {.pointer = (char8*)pointer};
    while (pointer[result.length])
    {
        result.length += 1;
    }
    return result;
}

static bool ebpf_string_equal(String8 a, String8 b)
{
    return a.length == b.length && (!a.length || memcmp(a.pointer, b.pointer, (size_t)a.length) == 0);
}

static bool ebpf_string_starts_with(String8 value, String8 prefix)
{
    return value.length >= prefix.length && (!prefix.length || memcmp(value.pointer, prefix.pointer, (size_t)prefix.length) == 0);
}

static String8 ebpf_symbol_name(IrSymbol* symbol)
{
    return symbol && symbol->link_name.length ? symbol->link_name : symbol ? symbol->name : (String8){0};
}

static u64 ebpf_align_up(u64 value, u64 alignment)
{
    u64 result;
    if (alignment <= 1)
    {
        result = value;
    }
    else
    {
        u64 mask = alignment - 1;
        result = (value + mask) & ~mask;
    }

    return result;
}

static void ebpf_buffer_init(EbpfBuffer* buffer, Arena* arena)
{
    *buffer = (EbpfBuffer){.arena = arena};
}

static bool ebpf_buffer_reserve(EbpfBuffer* buffer, u64 additional)
{
    if (additional > buffer->capacity - buffer->length)
    {
        if (additional > ARENA_MAX_RESERVATION - buffer->length)
        {
            return false;
        }
        u64 required = buffer->length + additional;
        u64 capacity = buffer->capacity ? buffer->capacity : 256;
        while (capacity < required)
        {
            if (capacity > ARENA_MAX_RESERVATION / 2)
            {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        u8* data = arena_allocate(buffer->arena, u8, capacity);
        if (buffer->length)
        {
            memcpy(data, buffer->data, (size_t)buffer->length);
        }
        buffer->data = data;
        buffer->capacity = capacity;
    }

    return true;
}

static bool ebpf_buffer_bytes(EbpfBuffer* buffer, void const* bytes, u64 length)
{
    if (length)
    {
        if (!ebpf_buffer_reserve(buffer, length))
        {
            return false;
        }
        memcpy(buffer->data + buffer->length, bytes, (size_t)length);
        buffer->length += length;
    }

    return true;
}

static bool ebpf_buffer_zeros(EbpfBuffer* buffer, u64 length)
{
    bool result;
    if (!ebpf_buffer_reserve(buffer, length))
    {
        result = false;
    }
    else
    {
        memset(buffer->data + buffer->length, 0, (size_t)length);
        buffer->length += length;
        result = true;
    }

    return result;
}

static bool ebpf_buffer_u8(EbpfBuffer* buffer, u8 value)
{
    return ebpf_buffer_bytes(buffer, &value, 1);
}

static bool ebpf_buffer_u16(EbpfBuffer* buffer, u16 value)
{
    u8 bytes[2] = {(u8)value, (u8)(value >> 8)};
    return ebpf_buffer_bytes(buffer, bytes, sizeof(bytes));
}

static bool ebpf_buffer_s16(EbpfBuffer* buffer, s16 value)
{
    return ebpf_buffer_u16(buffer, (u16)value);
}

static bool ebpf_buffer_u32(EbpfBuffer* buffer, u32 value)
{
    u8 bytes[4] = {(u8)value, (u8)(value >> 8), (u8)(value >> 16), (u8)(value >> 24)};
    return ebpf_buffer_bytes(buffer, bytes, sizeof(bytes));
}

static bool ebpf_buffer_s32(EbpfBuffer* buffer, s32 value)
{
    return ebpf_buffer_u32(buffer, (u32)value);
}

static bool ebpf_buffer_u64(EbpfBuffer* buffer, u64 value)
{
    return ebpf_buffer_u32(buffer, (u32)value) && ebpf_buffer_u32(buffer, (u32)(value >> 32));
}

static bool ebpf_buffer_align(EbpfBuffer* buffer, u64 alignment)
{
    u64 aligned = ebpf_align_up(buffer->length, alignment);
    return ebpf_buffer_zeros(buffer, aligned - buffer->length);
}

static void ebpf_vector_reserve(Arena* arena, void** data, u32* capacity, u32 required, u64 element_size)
{
    if (required <= *capacity)
    {
        return;
    }
    u32 new_capacity = *capacity ? *capacity : 8;
    while (new_capacity < required)
    {
        new_capacity = new_capacity > UINT32_MAX / 2 ? required : new_capacity * 2;
    }
    void* replacement = arena_allocate_bytes(arena, element_size * new_capacity, element_size >= 8 ? 8 : element_size);
    memset(replacement, 0, (size_t)(element_size * new_capacity));
    if (*data && *capacity)
    {
        memcpy(replacement, *data, (size_t)(element_size * *capacity));
    }
    *data = replacement;
    *capacity = new_capacity;
}

static bool ebpf_failed(EbpfContext* context)
{
    return context->error.code != EBPF_ERROR_NONE;
}

static void ebpf_fail(EbpfContext* context, EbpfErrorCode code, String8 message, IrFunction* function, IrBlock* block,
                      IrInstruction* instruction, IrSymbolId symbol)
{
    if (ebpf_failed(context))
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

static IrType* ebpf_type(EbpfContext* context, IrTypeId id)
{
    return ir_type_from_id(&context->program->types, id);
}

static IrSymbol* ebpf_ir_symbol(EbpfContext* context, IrSymbolId id)
{
    return ir_symbol_from_id(&context->program->symbols, id);
}

static bool ebpf_type_is_integer(IrType* type)
{
    return type && (type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_ENUM);
}

static bool ebpf_type_is_scalar(IrType* type)
{
    return type && (ebpf_type_is_integer(type) || type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_FUNCTION);
}

static u32 ebpf_type_bits(IrType* type)
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
    else if (type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_FUNCTION)
    {
        result = 64;
    }
    else if (type->kind == IR_TYPE_ENUM && !type->bit_width)
    {
        result = (u32)(type->layout.size * 8);
    }
    else
    {
        result = type->bit_width ? type->bit_width : (u32)(type->layout.size * 8);
    }

    return result;
}

static u32 ebpf_type_size(IrType* type)
{
    u32 result;
    if (!type || !type->layout.resolved || !type->layout.size || type->layout.size > UINT32_MAX)
    {
        result = 0;
    }
    else
    {
        result = (u32)type->layout.size;
    }

    return result;
}

static EbpfSection* ebpf_section_find(EbpfContext* context, String8 name)
{
    for (u32 index = 0; index < context->section_count; index += 1)
    {
        if (ebpf_string_equal(context->sections[index].name, name))
        {
            return context->sections + index;
        }
    }
    return 0;
}

static EbpfSection* ebpf_section_get(EbpfContext* context, String8 name, u32 type, u64 flags, u64 alignment, u64 entry_size)
{
    EbpfSection* existing = ebpf_section_find(context, name);
    if (existing)
    {
        if (existing->type != type || existing->flags != flags)
        {
            ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("conflicting eBPF ELF section definitions"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
            return 0;
        }
        if (alignment > existing->alignment)
        {
            existing->alignment = alignment;
        }
        return existing;
    }
    ebpf_vector_reserve(context->arena, (void**)&context->sections, &context->section_capacity, context->section_count + 1,
                        sizeof(*context->sections));
    EbpfSection* section = context->sections + context->section_count++;
    *section = (EbpfSection){.name = name, .type = type, .flags = flags, .alignment = alignment ? alignment : 1, .entry_size = entry_size};
    ebpf_buffer_init(&section->data, context->arena);
    return section;
}

static void ebpf_section_add_relocation(EbpfContext* context, EbpfSection* section, u64 offset, u32 symbol_key, u32 type)
{
    ebpf_vector_reserve(context->arena, (void**)&section->relocations, &section->relocation_capacity, section->relocation_count + 1,
                        sizeof(*section->relocations));
    section->relocations[section->relocation_count++] = (EbpfRelocation){.offset = offset, .symbol_key = symbol_key, .type = type};
    context->stats.relocation_count += 1;
}

static EbpfSymbolRecord* ebpf_symbol_by_key(EbpfContext* context, u32 key)
{
    for (u32 index = 0; index < context->symbol_count; index += 1)
    {
        if (context->symbols[index].key == key)
        {
            return context->symbols + index;
        }
    }
    return 0;
}

static EbpfSymbolRecord* ebpf_add_symbol_record(EbpfContext* context, u32 key, String8 name, IrSymbol* symbol, u8 binding, u8 type, bool synthetic)
{
    EbpfSymbolRecord* existing = ebpf_symbol_by_key(context, key);
    if (existing)
    {
        return existing;
    }
    ebpf_vector_reserve(context->arena, (void**)&context->symbols, &context->symbol_capacity, context->symbol_count + 1,
                        sizeof(*context->symbols));
    EbpfSymbolRecord* result = context->symbols + context->symbol_count++;
    *result = (EbpfSymbolRecord){.name = name, .symbol = symbol, .key = key, .binding = binding, .type = type, .synthetic = synthetic};
    return result;
}

static u32 ebpf_ir_symbol_key(EbpfContext* context, IrSymbolId id)
{
    u32 result;
    if (id.value >= context->program->symbols.count)
    {
        result = UINT32_MAX;
    }
    else
    {
        result = context->ir_symbol_keys[id.value];
    }

    return result;
}

static bool ebpf_parse_helper_id(String8 name, u32* id)
{
    String8 prefix = S8("bpf_helper#");
    if (!ebpf_string_starts_with(name, prefix) || name.length == prefix.length)
    {
        return false;
    }
    u64 value = 0;
    for (u64 index = prefix.length; index < name.length; index += 1)
    {
        u8 digit = (u8)(name.pointer[index] - '0');
        if (digit > 9 || value > (UINT32_MAX - digit) / 10)
        {
            return false;
        }
        value = value * 10 + digit;
    }
    *id = (u32)value;
    return true;
}

static u32 ebpf_section_instruction_count(EbpfSection* section)
{
    return (u32)(section->data.length / 8);
}

static bool ebpf_emit_insn(EbpfContext* context, EbpfSection* section, u8 code, u8 destination, u8 source, s16 offset, s32 immediate)
{
    if (section->data.length & 7)
    {
        ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("unaligned eBPF instruction stream"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return false;
    }
    return ebpf_buffer_u8(&section->data, code) && ebpf_buffer_u8(&section->data, (u8)(destination | (source << 4))) &&
           ebpf_buffer_s16(&section->data, offset) && ebpf_buffer_s32(&section->data, immediate);
}

static bool ebpf_patch_jump(EbpfContext* context, EbpfSection* section, u32 instruction_index, u32 target_index)
{
    s64 distance = (s64)target_index - (s64)instruction_index - 1;
    if (distance < INT16_MIN || distance > INT16_MAX || (u64)instruction_index * 8 + 4 > section->data.length)
    {
        ebpf_fail(context, EBPF_ERROR_JUMP_RANGE, ebpf_s8("eBPF branch exceeds signed 16-bit instruction range"), 0, 0, 0,
                  IR_SYMBOL_ID_INVALID);
        return false;
    }
    u64 byte_offset = (u64)instruction_index * 8 + 2;
    section->data.data[byte_offset] = (u8)(u16)(s16)distance;
    section->data.data[byte_offset + 1] = (u8)((u16)(s16)distance >> 8);
    return true;
}

static void ebpf_fe_insn(EbpfFunctionEmitter* emitter, u8 code, u8 destination, u8 source, s16 offset, s32 immediate)
{
    if (!ebpf_failed(emitter->context) && !ebpf_emit_insn(emitter->context, emitter->section, code, destination, source, offset, immediate))
    {
        ebpf_fail(emitter->context, EBPF_ERROR_ENCODING, ebpf_s8("failed to append eBPF instruction"), emitter->function, 0, 0,
                  IR_SYMBOL_ID_INVALID);
    }
}

static void ebpf_fe_mov_reg(EbpfFunctionEmitter* emitter, u8 destination, u8 source)
{
    ebpf_fe_insn(emitter, EBPF_CLASS_ALU64 | EBPF_OP_MOV | EBPF_SRC_X, destination, source, 0, 0);
}

static void ebpf_fe_mov_imm(EbpfFunctionEmitter* emitter, u8 destination, s64 value)
{
    if (value >= INT32_MIN && value <= INT32_MAX)
    {
        ebpf_fe_insn(emitter, EBPF_CLASS_ALU64 | EBPF_OP_MOV | EBPF_SRC_K, destination, 0, 0, (s32)value);
        return;
    }
    ebpf_fe_insn(emitter, EBPF_CLASS_LD | EBPF_SIZE_DW | EBPF_MODE_IMM, destination, 0, 0, (s32)(u32)(u64)value);
    ebpf_fe_insn(emitter, 0, 0, 0, 0, (s32)(u32)((u64)value >> 32));
}

static void ebpf_fe_alu_imm(EbpfFunctionEmitter* emitter, u8 operation, u8 destination, s32 value)
{
    ebpf_fe_insn(emitter, EBPF_CLASS_ALU64 | operation | EBPF_SRC_K, destination, 0, 0, value);
}

static void ebpf_fe_alu_reg(EbpfFunctionEmitter* emitter, u8 operation, u8 destination, u8 source)
{
    ebpf_fe_insn(emitter, EBPF_CLASS_ALU64 | operation | EBPF_SRC_X, destination, source, 0, 0);
}

static u8 ebpf_memory_size_code(u32 size)
{
    switch (size)
    {
    case 1: return EBPF_SIZE_B;
    case 2: return EBPF_SIZE_H;
    case 4: return EBPF_SIZE_W;
    case 8: return EBPF_SIZE_DW;
    default: return UINT8_MAX;
    }
}

static void ebpf_fe_load_memory(EbpfFunctionEmitter* emitter, u8 destination, u8 base, s16 offset, u32 size)
{
    u8 size_code = ebpf_memory_size_code(size);
    if (size_code == UINT8_MAX)
    {
        ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_TYPE, ebpf_s8("eBPF scalar load width must be 1, 2, 4, or 8 bytes"), emitter->function,
                  0, 0, IR_SYMBOL_ID_INVALID);
        return;
    }
    ebpf_fe_insn(emitter, EBPF_CLASS_LDX | size_code | EBPF_MODE_MEM, destination, base, offset, 0);
}

static void ebpf_fe_store_memory(EbpfFunctionEmitter* emitter, u8 base, s16 offset, u8 source, u32 size)
{
    u8 size_code = ebpf_memory_size_code(size);
    if (size_code == UINT8_MAX)
    {
        ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_TYPE, ebpf_s8("eBPF scalar store width must be 1, 2, 4, or 8 bytes"), emitter->function,
                  0, 0, IR_SYMBOL_ID_INVALID);
        return;
    }
    ebpf_fe_insn(emitter, EBPF_CLASS_STX | size_code | EBPF_MODE_MEM, base, source, offset, 0);
}

static void ebpf_fe_load_stack(EbpfFunctionEmitter* emitter, u8 destination, s16 offset)
{
    ebpf_fe_load_memory(emitter, destination, EBPF_REG_FP, offset, 8);
}

static void ebpf_fe_store_stack(EbpfFunctionEmitter* emitter, s16 offset, u8 source)
{
    ebpf_fe_store_memory(emitter, EBPF_REG_FP, offset, source, 8);
}

static void ebpf_fe_lddw_symbol(EbpfFunctionEmitter* emitter, u8 destination, u8 pseudo_source, u32 symbol_key, s64 addend)
{
    u64 relocation_offset = emitter->section->data.length;
    ebpf_fe_insn(emitter, EBPF_CLASS_LD | EBPF_SIZE_DW | EBPF_MODE_IMM, destination, pseudo_source, 0, (s32)(u32)(u64)addend);
    ebpf_fe_insn(emitter, 0, 0, 0, 0, (s32)(u32)((u64)addend >> 32));
    if (!ebpf_failed(emitter->context))
    {
        ebpf_section_add_relocation(emitter->context, emitter->section, relocation_offset, symbol_key, EBPF_R_BPF_64_64);
    }
}

static IrInstruction* ebpf_fe_definition(EbpfFunctionEmitter* emitter, IrValueId value)
{
    if (value.value >= emitter->function->value_count)
    {
        return 0;
    }
    IrInstructionId definition = emitter->function->values[value.value].definition;
    IrInstruction* result;
    if (definition.value >= emitter->function->instruction_count)
    {
        result = 0;
    }
    else
    {
        result = emitter->function->instructions + definition.value;
    }

    return result;
}

static IrType* ebpf_fe_value_type(EbpfFunctionEmitter* emitter, IrValueId value)
{
    IrType* result;
    if (value.value >= emitter->function->value_count)
    {
        result = 0;
    }
    else
    {
        result = ebpf_type(emitter->context, emitter->function->values[value.value].canonical_type);
    }

    return result;
}

static EbpfStringRecord* ebpf_string_find(EbpfContext* context, IrFunction* function, IrInstructionId instruction)
{
    for (u32 index = 0; index < context->string_count; index += 1)
    {
        EbpfStringRecord* record = context->strings + index;
        if (record->function == function && record->instruction.value == instruction.value)
        {
            return record;
        }
    }
    return 0;
}

static EbpfFunctionRecord* ebpf_function_for_symbol(EbpfContext* context, IrSymbolId symbol)
{
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        if (context->functions[index].function->symbol.value == symbol.value)
        {
            return context->functions + index;
        }
    }
    return 0;
}

static EbpfGlobalRecord* ebpf_global_for_symbol(EbpfContext* context, IrSymbolId symbol)
{
    for (u32 index = 0; index < context->global_count; index += 1)
    {
        if (context->globals[index].global->symbol.value == symbol.value)
        {
            return context->globals + index;
        }
    }
    return 0;
}

static void ebpf_fe_normalize(EbpfFunctionEmitter* emitter, u8 reg, IrType* type, bool signed_value)
{
    if (ebpf_type_is_integer(type))
    {
        u32 bits = ebpf_type_bits(type);
        if (type->kind == IR_TYPE_BOOLEAN)
        {
            ebpf_fe_insn(emitter, EBPF_CLASS_JMP | EBPF_JNE | EBPF_SRC_K, reg, 0, 2, 0);
            ebpf_fe_mov_imm(emitter, reg, 0);
            ebpf_fe_insn(emitter, EBPF_CLASS_JMP | EBPF_JA, 0, 0, 1, 0);
            ebpf_fe_mov_imm(emitter, reg, 1);
            return;
        }
        if (bits && bits < 64)
        {
            if (signed_value)
            {
                ebpf_fe_alu_imm(emitter, EBPF_OP_LSH, reg, (s32)(64 - bits));
                ebpf_fe_alu_imm(emitter, EBPF_OP_ARSH, reg, (s32)(64 - bits));
            }
            else
            {
                u64 mask = (UINT64_C(1) << bits) - 1;
                if (mask <= INT32_MAX)
                {
                    ebpf_fe_alu_imm(emitter, EBPF_OP_AND, reg, (s32)mask);
                }
                else
                {
                    u8 temporary = reg == EBPF_REG_9 ? EBPF_REG_8 : EBPF_REG_9;
                    ebpf_fe_mov_imm(emitter, temporary, (s64)mask);
                    ebpf_fe_alu_reg(emitter, EBPF_OP_AND, reg, temporary);
                }
            }
        }
    }
}

static void ebpf_fe_emit_value(EbpfFunctionEmitter* emitter, u8 destination, IrValueId value);

static void ebpf_fe_emit_index(EbpfFunctionEmitter* emitter, u8 destination, IrInstruction* instruction)
{
    IrType* base_type = ebpf_fe_value_type(emitter, instruction->operands[0]);
    IrType* element_type = base_type ? ebpf_type(emitter->context, base_type->element_type) : 0;
    u32 element_size = ebpf_type_size(element_type);
    if (!element_size)
    {
        ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_TYPE, ebpf_s8("invalid eBPF indexed element layout"), emitter->function, 0,
                  instruction, IR_SYMBOL_ID_INVALID);
        return;
    }
    ebpf_fe_emit_value(emitter, destination, instruction->operands[0]);
    u8 index_register = destination == EBPF_REG_9 ? EBPF_REG_8 : EBPF_REG_9;
    ebpf_fe_emit_value(emitter, index_register, instruction->operands[1]);
    if (element_size != 1)
    {
        if (element_size <= INT32_MAX)
        {
            ebpf_fe_alu_imm(emitter, EBPF_OP_MUL, index_register, (s32)element_size);
        }
        else
        {
            ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_TYPE, ebpf_s8("eBPF indexed element size exceeds immediate range"),
                      emitter->function, 0, instruction, IR_SYMBOL_ID_INVALID);
            return;
        }
    }
    ebpf_fe_alu_reg(emitter, EBPF_OP_ADD, destination, index_register);
}

static void ebpf_fe_emit_value(EbpfFunctionEmitter* emitter, u8 destination, IrValueId value)
{
    if (value.value >= emitter->function->value_count)
    {
        ebpf_fail(emitter->context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("eBPF value ID is out of range"), emitter->function, 0, 0,
                  IR_SYMBOL_ID_INVALID);
        return;
    }
    IrInstruction* definition = ebpf_fe_definition(emitter, value);
    if (!definition)
    {
        s16 slot = emitter->value_slots[value.value];
        if (slot == EBPF_SLOT_NONE)
        {
            ebpf_fail(emitter->context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("eBPF block parameter has no stack slot"), emitter->function, 0, 0,
                      IR_SYMBOL_ID_INVALID);
            return;
        }
        ebpf_fe_load_stack(emitter, destination, slot);
        return;
    }
    switch (definition->opcode)
    {
    case IR_OPCODE_CONSTANT_INTEGER:
    case IR_OPCODE_ENUM:
    {
        u64 bits = definition->immediate_count ? definition->immediates[0] : 0;
        if (definition->immediate_is_negative)
        {
            bits = 0 - bits;
        }
        ebpf_fe_mov_imm(emitter, destination, (s64)bits);
        IrType* type = ebpf_type(emitter->context, definition->canonical_type);
        ebpf_fe_normalize(emitter, destination, type, type && type->kind == IR_TYPE_INTEGER && type->is_signed);
    }
    break;
    case IR_OPCODE_UNDEFINED:
        ebpf_fe_mov_imm(emitter, destination, 0);
        break;
    case IR_OPCODE_LOCAL:
    {
        s16 offset = emitter->local_offsets[value.value];
        if (offset == EBPF_SLOT_NONE)
        {
            ebpf_fail(emitter->context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("eBPF local has no frame allocation"), emitter->function, 0,
                      definition, IR_SYMBOL_ID_INVALID);
            return;
        }
        ebpf_fe_mov_reg(emitter, destination, EBPF_REG_FP);
        ebpf_fe_alu_imm(emitter, EBPF_OP_ADD, destination, offset);
    }
    break;
    case IR_OPCODE_GLOBAL:
    {
        u32 key = ebpf_ir_symbol_key(emitter->context, definition->symbol);
        EbpfGlobalRecord* global = ebpf_global_for_symbol(emitter->context, definition->symbol);
        if (key == UINT32_MAX || !global)
        {
            ebpf_fail(emitter->context, EBPF_ERROR_UNRESOLVED_SYMBOL, ebpf_s8("unresolved eBPF global reference"), emitter->function, 0,
                      definition, definition->symbol);
            return;
        }
        u8 pseudo = ebpf_string_equal(global->section->name, S8(".maps")) ? EBPF_PSEUDO_MAP_FD : 0;
        ebpf_fe_lddw_symbol(emitter, destination, pseudo, key, 0);
    }
    break;
    case IR_OPCODE_CONSTANT_STRING:
    {
        EbpfStringRecord* string = ebpf_string_find(emitter->context, emitter->function, ir_instruction_self_id(emitter->function, definition));
        if (!string)
        {
            ebpf_fail(emitter->context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("missing eBPF string literal record"), emitter->function, 0,
                      definition, IR_SYMBOL_ID_INVALID);
            return;
        }
        ebpf_fe_lddw_symbol(emitter, destination, 0, string->symbol_key, 0);
    }
    break;
    case IR_OPCODE_LENGTH:
    {
        IrType* iterable = definition->operand_count ? ebpf_fe_value_type(emitter, definition->operands[0]) : 0;
        if (!iterable || (iterable->kind != IR_TYPE_ARRAY && iterable->kind != IR_TYPE_VECTOR))
        {
            ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_AGGREGATE, ebpf_s8("dynamic slice/range length is unsupported by eBPF"),
                      emitter->function, 0, definition, IR_SYMBOL_ID_INVALID);
            return;
        }
        ebpf_fe_mov_imm(emitter, destination, (s64)iterable->element_count);
    }
    break;
    case IR_OPCODE_INDEX:
        ebpf_fe_emit_index(emitter, destination, definition);
        break;
    case IR_OPCODE_FIELD:
    {
        IrType* base = definition->operand_count ? ebpf_fe_value_type(emitter, definition->operands[0]) : 0;
        u64 field = definition->immediate_count ? definition->immediates[0] : UINT64_MAX;
        if (!base || (base->kind != IR_TYPE_STRUCT && base->kind != IR_TYPE_UNION) || field >= base->field_count ||
            base->fields[field].offset > INT32_MAX)
        {
            ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_TYPE, ebpf_s8("invalid eBPF field address"), emitter->function, 0, definition,
                      IR_SYMBOL_ID_INVALID);
            return;
        }
        ebpf_fe_emit_value(emitter, destination, definition->operands[0]);
        if (base->fields[field].offset)
        {
            ebpf_fe_alu_imm(emitter, EBPF_OP_ADD, destination, (s32)base->fields[field].offset);
        }
    }
    break;
    case IR_OPCODE_ADDRESS_OF:
    case IR_OPCODE_DEREFERENCE:
        ebpf_fe_emit_value(emitter, destination, definition->operands[0]);
        break;
    case IR_OPCODE_FUNCTION:
        // Direct calls consume the symbol carried by IR_OPCODE_CALL. A
        // standalone function value has no verifier-safe runtime encoding.
        ebpf_fe_mov_imm(emitter, destination, 0);
        break;
    default:
    {
        s16 slot = emitter->value_slots[value.value];
        if (slot == EBPF_SLOT_NONE)
        {
            ebpf_fail(emitter->context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("eBPF SSA value has no materialization or stack slot"),
                      emitter->function, 0, definition, IR_SYMBOL_ID_INVALID);
            return;
        }
        ebpf_fe_load_stack(emitter, destination, slot);
    }
    break;
    }
}

static void ebpf_fe_store_result(EbpfFunctionEmitter* emitter, IrInstruction* instruction, u8 source, bool normalize, bool signed_value)
{
    if (instruction->result.value == IR_ID_UNDERLYING_INVALID || instruction->result.value >= emitter->function->value_count)
    {
        return;
    }
    if (normalize)
    {
        ebpf_fe_normalize(emitter, source, ebpf_type(emitter->context, instruction->canonical_type), signed_value);
    }
    s16 slot = emitter->value_slots[instruction->result.value];
    if (slot != EBPF_SLOT_NONE)
    {
        ebpf_fe_store_stack(emitter, slot, source);
    }
}

static bool ebpf_instruction_is_rematerialized(IrInstruction* instruction)
{
    switch (instruction->opcode)
    {
    case IR_OPCODE_LOCAL:
    case IR_OPCODE_GLOBAL:
    case IR_OPCODE_CONSTANT_INTEGER:
    case IR_OPCODE_CONSTANT_STRING:
    case IR_OPCODE_UNDEFINED:
    case IR_OPCODE_FUNCTION:
    case IR_OPCODE_LENGTH:
    case IR_OPCODE_INDEX:
    case IR_OPCODE_FIELD:
    case IR_OPCODE_ENUM:
    case IR_OPCODE_ADDRESS_OF:
    case IR_OPCODE_DEREFERENCE: return true;
    default: return false;
    }
}

static bool ebpf_fe_allocate(EbpfFunctionEmitter* emitter)
{
    EbpfContext* context = emitter->context;
    IrFunction* function = emitter->function;
    u32 value_count = function->value_count ? function->value_count : 1;
    emitter->value_slots = arena_allocate(context->arena, s16, value_count);
    emitter->local_offsets = arena_allocate(context->arena, s16, value_count);
    emitter->use_counts = arena_allocate(context->arena, u32, value_count);
    memset(emitter->use_counts, 0, sizeof(u32) * value_count);
    for (u32 index = 0; index < value_count; index += 1)
    {
        emitter->value_slots[index] = EBPF_SLOT_NONE;
        emitter->local_offsets[index] = EBPF_SLOT_NONE;
    }
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
        {
            if (instruction->operands[operand_index].value < function->value_count)
            {
                emitter->use_counts[instruction->operands[operand_index].value] += 1;
            }
        }
    }
    u32 maximum_parameters = 0;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        if (block->parameter_count > maximum_parameters)
        {
            maximum_parameters = block->parameter_count;
        }
        for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
        {
            for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
            {
                if (incoming->value.value < function->value_count)
                {
                    emitter->use_counts[incoming->value.value] += 1;
                }
            }
        }
    }

    u64 cursor = 0;
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->opcode != IR_OPCODE_LOCAL || instruction->result.value >= function->value_count)
        {
            continue;
        }
        IrType* type = ebpf_type(context, instruction->canonical_type);
        u32 size = ebpf_type_size(type);
        u32 alignment = type && type->layout.alignment ? type->layout.alignment : 1;
        if (!size || alignment > 8 || (alignment & (alignment - 1)))
        {
            ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_TYPE, ebpf_s8("eBPF local requires a resolved layout with at most 8-byte alignment"),
                      function, 0, instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
        cursor = ebpf_align_up(cursor, alignment);
        cursor += size;
        if (cursor > 512)
        {
            ebpf_fail(context, EBPF_ERROR_STACK_LIMIT, ebpf_s8("eBPF function exceeds the 512-byte verifier stack limit"), function, 0,
                      instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
        emitter->local_offsets[instruction->result.value] = (s16)(-(s32)cursor);
    }

    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        for (IrBlockParameter* parameter = function->blocks[block_index].first_parameter; parameter; parameter = parameter->next)
        {
            if (parameter->value.value >= function->value_count || !ebpf_type_is_scalar(ebpf_type(context, parameter->canonical_type)))
            {
                ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_AGGREGATE, ebpf_s8("aggregate eBPF block parameters are unsupported"), function,
                          function->blocks + block_index, 0, IR_SYMBOL_ID_INVALID);
                return false;
            }
            cursor = ebpf_align_up(cursor, 8) + 8;
            if (cursor > 512)
            {
                ebpf_fail(context, EBPF_ERROR_STACK_LIMIT, ebpf_s8("eBPF function exceeds the 512-byte verifier stack limit"), function,
                          function->blocks + block_index, 0, IR_SYMBOL_ID_INVALID);
                return false;
            }
            emitter->value_slots[parameter->value.value] = (s16)(-(s32)cursor);
        }
    }

    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->result.value == IR_ID_UNDERLYING_INVALID || instruction->result.value >= function->value_count ||
            (!emitter->use_counts[instruction->result.value] && instruction->opcode != IR_OPCODE_ARGUMENT) ||
            ebpf_instruction_is_rematerialized(instruction))
        {
            continue;
        }
        IrType* type = ebpf_type(context, instruction->canonical_type);
        if (!ebpf_type_is_scalar(type))
        {
            if (instruction->opcode == IR_OPCODE_LOAD && !emitter->use_counts[instruction->result.value] && !instruction->volatile_access)
            {
                continue;
            }
            ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_AGGREGATE, ebpf_s8("aggregate eBPF SSA values are unsupported"), function, 0,
                      instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
        cursor = ebpf_align_up(cursor, 8) + 8;
        if (cursor > 512)
        {
            ebpf_fail(context, EBPF_ERROR_STACK_LIMIT, ebpf_s8("eBPF function exceeds the 512-byte verifier stack limit"), function, 0,
                      instruction, IR_SYMBOL_ID_INVALID);
            return false;
        }
        emitter->value_slots[instruction->result.value] = (s16)(-(s32)cursor);
    }

    emitter->temporary_count = maximum_parameters;
    if (maximum_parameters)
    {
        cursor = ebpf_align_up(cursor, 8);
        u64 base_cursor = cursor + 8;
        cursor += (u64)maximum_parameters * 8;
        if (cursor > 512)
        {
            ebpf_fail(context, EBPF_ERROR_STACK_LIMIT, ebpf_s8("eBPF block-parameter copies exceed the 512-byte verifier stack limit"),
                      function, 0, 0, IR_SYMBOL_ID_INVALID);
            return false;
        }
        emitter->temporary_base = (s16)(-(s32)base_cursor);
    }
    else
    {
        emitter->temporary_base = EBPF_SLOT_NONE;
    }
    emitter->frame_size = (u32)ebpf_align_up(cursor, 8);
    if (emitter->frame_size > context->stats.max_stack_bytes)
    {
        context->stats.max_stack_bytes = emitter->frame_size;
    }
    return true;
}

static bool ebpf_fe_validate_abi(EbpfFunctionEmitter* emitter)
{
    IrType* function_type = ebpf_type(emitter->context, emitter->function->canonical_type);
    if (!function_type || function_type->kind != IR_TYPE_FUNCTION)
    {
        ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_ABI, ebpf_s8("eBPF function lacks a canonical function type"), emitter->function,
                  0, 0, emitter->function->symbol);
        return false;
    }
    if (function_type->is_variadic)
    {
        ebpf_fail(emitter->context, EBPF_ERROR_VARIADIC, ebpf_s8("variadic functions are unsupported by eBPF"), emitter->function, 0, 0,
                  emitter->function->symbol);
        return false;
    }
    u32 maximum_parameters = emitter->record->is_program ? 1 : 5;
    if (function_type->parameter_count > maximum_parameters)
    {
        ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_ABI,
                  emitter->record->is_program ? ebpf_s8("eBPF program entry points accept at most one context pointer")
                                               : ebpf_s8("eBPF subprograms accept at most five scalar arguments"),
                  emitter->function, 0, 0, emitter->function->symbol);
        return false;
    }
    for (u32 index = 0; index < function_type->parameter_count; index += 1)
    {
        IrType* parameter = ebpf_type(emitter->context, function_type->parameter_types[index]);
        if (!ebpf_type_is_scalar(parameter) || (emitter->record->is_program && parameter->kind != IR_TYPE_POINTER))
        {
            ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_ABI,
                      emitter->record->is_program ? ebpf_s8("eBPF entry-point argument must be a pointer")
                                                   : ebpf_s8("aggregate eBPF subprogram arguments are unsupported"),
                      emitter->function, 0, 0, emitter->function->symbol);
            return false;
        }
    }
    IrType* result = ebpf_type(emitter->context, function_type->return_type);
    if (!result || (result->kind != IR_TYPE_VOID && !ebpf_type_is_scalar(result)))
    {
        ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_ABI, ebpf_s8("aggregate eBPF function results are unsupported"), emitter->function,
                  0, 0, emitter->function->symbol);
        return false;
    }
    if (emitter->record->is_program && result->kind != IR_TYPE_BOOLEAN && result->kind != IR_TYPE_INTEGER && result->kind != IR_TYPE_ENUM)
    {
        ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_ABI, ebpf_s8("eBPF program entry points must return an integer"),
                  emitter->function, 0, 0, emitter->function->symbol);
        return false;
    }
    return true;
}

static void ebpf_fe_emit_prologue(EbpfFunctionEmitter* emitter)
{
    if (emitter->frame_size)
    {
        ebpf_fe_mov_imm(emitter, EBPF_REG_0, 0);
        for (u32 offset = 8; offset <= emitter->frame_size; offset += 8)
        {
            ebpf_fe_store_stack(emitter, (s16)(-(s32)offset), EBPF_REG_0);
        }
    }
    // Capture all incoming registers before source-order lowering. Canonical
    // IR does not require ARGUMENT instructions to precede calls, while the
    // eBPF ABI makes r1-r5 caller-saved.
    for (u32 instruction_index = 0; instruction_index < emitter->function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = emitter->function->instructions + instruction_index;
        if (instruction->opcode != IR_OPCODE_ARGUMENT)
        {
            continue;
        }
        if (instruction->immediate_count != 1 || instruction->immediates[0] >= 5 ||
            instruction->result.value >= emitter->function->value_count)
        {
            ebpf_fail(emitter->context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("invalid eBPF argument index"), emitter->function, 0,
                      instruction, IR_SYMBOL_ID_INVALID);
            return;
        }
        s16 slot = emitter->value_slots[instruction->result.value];
        if (slot == EBPF_SLOT_NONE)
        {
            ebpf_fail(emitter->context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("eBPF argument has no preservation slot"), emitter->function, 0,
                      instruction, IR_SYMBOL_ID_INVALID);
            return;
        }
        ebpf_fe_store_stack(emitter, slot, (u8)(EBPF_REG_1 + instruction->immediates[0]));
    }
}

static IrIncoming* ebpf_fe_incoming(IrBlockParameter* parameter, IrBlockId predecessor)
{
    for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
    {
        if (incoming->predecessor.value == predecessor.value)
        {
            return incoming;
        }
    }
    return 0;
}

static void ebpf_fe_parallel_copy(EbpfFunctionEmitter* emitter, IrBlock* predecessor, IrBlock* target)
{
    u32 index = 0;
    for (IrBlockParameter* parameter = target->first_parameter; parameter; parameter = parameter->next, index += 1)
    {
        IrIncoming* incoming = ebpf_fe_incoming(parameter, predecessor->id);
        if (!incoming || index >= emitter->temporary_count)
        {
            ebpf_fail(emitter->context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("missing eBPF block-parameter incoming value"), emitter->function,
                      predecessor, 0, IR_SYMBOL_ID_INVALID);
            return;
        }
        ebpf_fe_emit_value(emitter, EBPF_REG_0, incoming->value);
        ebpf_fe_store_stack(emitter, (s16)(emitter->temporary_base - (s16)(index * 8)), EBPF_REG_0);
    }
    index = 0;
    for (IrBlockParameter* parameter = target->first_parameter; parameter; parameter = parameter->next, index += 1)
    {
        if (parameter->value.value >= emitter->function->value_count || emitter->value_slots[parameter->value.value] == EBPF_SLOT_NONE)
        {
            ebpf_fail(emitter->context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("eBPF block parameter has no destination slot"), emitter->function,
                      target, 0, IR_SYMBOL_ID_INVALID);
            return;
        }
        ebpf_fe_load_stack(emitter, EBPF_REG_0, (s16)(emitter->temporary_base - (s16)(index * 8)));
        ebpf_fe_store_stack(emitter, emitter->value_slots[parameter->value.value], EBPF_REG_0);
    }
}

static void ebpf_fe_add_patch(EbpfFunctionEmitter* emitter, u32 instruction_index, IrBlockId target)
{
    ebpf_vector_reserve(emitter->context->arena, (void**)&emitter->patches, &emitter->patch_capacity, emitter->patch_count + 1,
                        sizeof(*emitter->patches));
    emitter->patches[emitter->patch_count++] = (EbpfBranchPatch){.instruction_index = instruction_index, .target = target};
}

static void ebpf_fe_jump_to_block(EbpfFunctionEmitter* emitter, IrBlockId target)
{
    u32 instruction_index = ebpf_section_instruction_count(emitter->section);
    ebpf_fe_insn(emitter, EBPF_CLASS_JMP | EBPF_JA, 0, 0, 0, 0);
    ebpf_fe_add_patch(emitter, instruction_index, target);
}

static void ebpf_fe_emit_comparison(EbpfFunctionEmitter* emitter, IrBinaryOperation operation, u8 left, u8 right, u8 result)
{
    u8 jump_operation = 0;
    switch (operation)
    {
    case IR_BINARY_INTEGER_EQUAL:
    case IR_BINARY_POINTER_EQUAL:
    case IR_BINARY_BOOLEAN_EQUAL: jump_operation = EBPF_JEQ; break;
    case IR_BINARY_INTEGER_NOT_EQUAL:
    case IR_BINARY_POINTER_NOT_EQUAL:
    case IR_BINARY_BOOLEAN_NOT_EQUAL: jump_operation = EBPF_JNE; break;
    case IR_BINARY_SIGNED_LESS: jump_operation = EBPF_JSLT; break;
    case IR_BINARY_SIGNED_LESS_EQUAL: jump_operation = EBPF_JSLE; break;
    case IR_BINARY_SIGNED_GREATER: jump_operation = EBPF_JSGT; break;
    case IR_BINARY_SIGNED_GREATER_EQUAL: jump_operation = EBPF_JSGE; break;
    case IR_BINARY_UNSIGNED_LESS: jump_operation = EBPF_JLT; break;
    case IR_BINARY_UNSIGNED_LESS_EQUAL: jump_operation = EBPF_JLE; break;
    case IR_BINARY_UNSIGNED_GREATER: jump_operation = EBPF_JGT; break;
    case IR_BINARY_UNSIGNED_GREATER_EQUAL: jump_operation = EBPF_JGE; break;
    default:
        ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_INSTRUCTION, ebpf_s8("unsupported eBPF comparison"), emitter->function, 0, 0,
                  IR_SYMBOL_ID_INVALID);
        return;
    }
    ebpf_fe_mov_imm(emitter, result, 0);
    // Offsets are relative to the next instruction: skip JA, not MOV 1.
    ebpf_fe_insn(emitter, EBPF_CLASS_JMP | jump_operation | EBPF_SRC_X, left, right, 1, 0);
    ebpf_fe_insn(emitter, EBPF_CLASS_JMP | EBPF_JA, 0, 0, 1, 0);
    ebpf_fe_mov_imm(emitter, result, 1);
}

static bool ebpf_binary_is_comparison(IrBinaryOperation operation)
{
    return operation == IR_BINARY_INTEGER_EQUAL || operation == IR_BINARY_INTEGER_NOT_EQUAL || operation == IR_BINARY_POINTER_EQUAL ||
           operation == IR_BINARY_POINTER_NOT_EQUAL || operation == IR_BINARY_BOOLEAN_EQUAL || operation == IR_BINARY_BOOLEAN_NOT_EQUAL ||
           (operation >= IR_BINARY_SIGNED_LESS && operation <= IR_BINARY_UNSIGNED_GREATER_EQUAL);
}

static void ebpf_fe_emit_binary(EbpfFunctionEmitter* emitter, IrInstruction* instruction)
{
    IrBinaryOperation operation = instruction->binary_operation;
    IrType* operand_type = ebpf_fe_value_type(emitter, instruction->operands[0]);
    if (!operand_type || operand_type->kind == IR_TYPE_FLOAT || operation == IR_BINARY_RANGE || operation >= IR_BINARY_VECTOR_INTEGER_ADD)
    {
        ebpf_fail(emitter->context, operation >= IR_BINARY_VECTOR_INTEGER_ADD ? EBPF_ERROR_SIMD : EBPF_ERROR_UNSUPPORTED_INSTRUCTION,
                  ebpf_s8("floating-point, range, and vector binary operations are unsupported by eBPF"), emitter->function, 0, instruction,
                  IR_SYMBOL_ID_INVALID);
        return;
    }
    ebpf_fe_emit_value(emitter, EBPF_REG_0, instruction->operands[0]);
    ebpf_fe_emit_value(emitter, EBPF_REG_9, instruction->operands[1]);
    if (ebpf_binary_is_comparison(operation))
    {
        ebpf_fe_emit_comparison(emitter, operation, EBPF_REG_0, EBPF_REG_9, EBPF_REG_8);
        ebpf_fe_store_result(emitter, instruction, EBPF_REG_8, false, false);
        return;
    }
    u8 opcode = 0;
    bool signed_result = operand_type->kind == IR_TYPE_INTEGER && operand_type->is_signed;
    switch (operation)
    {
    case IR_BINARY_INTEGER_ADD: opcode = EBPF_OP_ADD; break;
    case IR_BINARY_INTEGER_SUBTRACT: opcode = EBPF_OP_SUB; break;
    case IR_BINARY_INTEGER_MULTIPLY: opcode = EBPF_OP_MUL; break;
    case IR_BINARY_UNSIGNED_DIVIDE: opcode = EBPF_OP_DIV; break;
    case IR_BINARY_UNSIGNED_REMAINDER: opcode = EBPF_OP_MOD; break;
    case IR_BINARY_SHIFT_LEFT: opcode = EBPF_OP_LSH; break;
    case IR_BINARY_SIGNED_SHIFT_RIGHT: opcode = EBPF_OP_ARSH; break;
    case IR_BINARY_UNSIGNED_SHIFT_RIGHT: opcode = EBPF_OP_RSH; break;
    case IR_BINARY_INTEGER_BITWISE_AND:
    case IR_BINARY_BOOLEAN_AND: opcode = EBPF_OP_AND; break;
    case IR_BINARY_INTEGER_BITWISE_OR:
    case IR_BINARY_BOOLEAN_OR: opcode = EBPF_OP_OR; break;
    case IR_BINARY_INTEGER_BITWISE_XOR: opcode = EBPF_OP_XOR; break;
    case IR_BINARY_SIGNED_DIVIDE:
    case IR_BINARY_SIGNED_REMAINDER:
        ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_INSTRUCTION,
                  ebpf_s8("portable eBPF has no signed divide/remainder instruction; use an unsigned operation or helper"), emitter->function,
                  0, instruction, IR_SYMBOL_ID_INVALID);
        return;
    default:
        ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_INSTRUCTION, ebpf_s8("unsupported eBPF binary operation"), emitter->function, 0,
                  instruction, IR_SYMBOL_ID_INVALID);
        return;
    }
    ebpf_fe_alu_reg(emitter, opcode, EBPF_REG_0, EBPF_REG_9);
    ebpf_fe_store_result(emitter, instruction, EBPF_REG_0, true, signed_result);
}

static void ebpf_fe_emit_unary(EbpfFunctionEmitter* emitter, IrInstruction* instruction)
{
    IrType* type = ebpf_type(emitter->context, instruction->canonical_type);
    ebpf_fe_emit_value(emitter, EBPF_REG_0, instruction->operands[0]);
    switch (instruction->unary_operation)
    {
    case IR_UNARY_INTEGER_NEGATE:
        ebpf_fe_insn(emitter, EBPF_CLASS_ALU64 | EBPF_OP_NEG, EBPF_REG_0, 0, 0, 0);
        ebpf_fe_store_result(emitter, instruction, EBPF_REG_0, true, type && type->kind == IR_TYPE_INTEGER && type->is_signed);
        break;
    case IR_UNARY_INTEGER_BITWISE_NOT:
        ebpf_fe_alu_imm(emitter, EBPF_OP_XOR, EBPF_REG_0, -1);
        ebpf_fe_store_result(emitter, instruction, EBPF_REG_0, true, type && type->kind == IR_TYPE_INTEGER && type->is_signed);
        break;
    case IR_UNARY_BOOLEAN_NOT:
        ebpf_fe_mov_imm(emitter, EBPF_REG_8, 0);
        ebpf_fe_insn(emitter, EBPF_CLASS_JMP | EBPF_JEQ | EBPF_SRC_K, EBPF_REG_0, 0, 1, 0);
        ebpf_fe_insn(emitter, EBPF_CLASS_JMP | EBPF_JA, 0, 0, 1, 0);
        ebpf_fe_mov_imm(emitter, EBPF_REG_8, 1);
        ebpf_fe_store_result(emitter, instruction, EBPF_REG_8, false, false);
        break;
    case IR_UNARY_INTEGER_COUNT_LEADING_ZEROS:
    case IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS:
    case IR_UNARY_INTEGER_POPULATION_COUNT:
        ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_INSTRUCTION,
                  ebpf_s8("eBPF bit-count unary operations require a target helper and are not lowered implicitly"), emitter->function, 0,
                  instruction, IR_SYMBOL_ID_INVALID);
        break;
    default:
        ebpf_fail(emitter->context, instruction->unary_operation >= IR_UNARY_VECTOR_INTEGER_NEGATE ? EBPF_ERROR_SIMD
                                                                                                  : EBPF_ERROR_UNSUPPORTED_INSTRUCTION,
                  ebpf_s8("floating-point and vector unary operations are unsupported by eBPF"), emitter->function, 0, instruction,
                  IR_SYMBOL_ID_INVALID);
        break;
    }
}

static void ebpf_fe_emit_cast(EbpfFunctionEmitter* emitter, IrInstruction* instruction)
{
    IrType* source = ebpf_fe_value_type(emitter, instruction->operands[0]);
    IrType* destination = ebpf_type(emitter->context, instruction->canonical_type);
    if (!ebpf_type_is_scalar(source) || !ebpf_type_is_scalar(destination))
    {
        ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_TYPE, ebpf_s8("eBPF casts require scalar integer or pointer types"), emitter->function,
                  0, instruction, IR_SYMBOL_ID_INVALID);
        return;
    }
    ebpf_fe_emit_value(emitter, EBPF_REG_0, instruction->operands[0]);
    bool signed_value = destination->kind == IR_TYPE_INTEGER && destination->is_signed &&
                        instruction->conversion_operation != IR_CONVERSION_INTEGER_ZERO_EXTEND;
    if (destination->kind != IR_TYPE_POINTER && destination->kind != IR_TYPE_FUNCTION)
    {
        ebpf_fe_normalize(emitter, EBPF_REG_0, destination, signed_value);
    }
    ebpf_fe_store_result(emitter, instruction, EBPF_REG_0, false, false);
}

static void ebpf_fe_emit_call(EbpfFunctionEmitter* emitter, IrInstruction* instruction)
{
    if (!instruction->operand_count || instruction->operand_count > 6)
    {
        ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_ABI, ebpf_s8("eBPF calls require a callee and at most five arguments"),
                  emitter->function, 0, instruction, instruction->symbol);
        return;
    }
    IrValue* callee = instruction->operands[0].value < emitter->function->value_count
                          ? emitter->function->values + instruction->operands[0].value
                          : 0;
    IrType* callee_type = callee ? ebpf_type(emitter->context, callee->canonical_type) : 0;
    if (callee_type && callee_type->kind == IR_TYPE_POINTER)
    {
        ebpf_fail(emitter->context, EBPF_ERROR_INDIRECT_CALL, ebpf_s8("indirect calls are unsupported by the eBPF backend"), emitter->function,
                  0, instruction, instruction->symbol);
        return;
    }
    for (u32 argument_index = 1; argument_index < instruction->operand_count; argument_index += 1)
    {
        IrType* argument_type = ebpf_fe_value_type(emitter, instruction->operands[argument_index]);
        if (!ebpf_type_is_scalar(argument_type))
        {
            ebpf_fail(emitter->context, EBPF_ERROR_UNSUPPORTED_ABI, ebpf_s8("aggregate eBPF call arguments are unsupported"),
                      emitter->function, 0, instruction, instruction->symbol);
            return;
        }
        ebpf_fe_emit_value(emitter, (u8)(EBPF_REG_1 + argument_index - 1), instruction->operands[argument_index]);
    }
    IrSymbol* symbol = ebpf_ir_symbol(emitter->context, instruction->symbol);
    u32 helper_id = 0;
    if (symbol && ebpf_parse_helper_id(ebpf_symbol_name(symbol), &helper_id))
    {
        ebpf_fe_insn(emitter, EBPF_CLASS_JMP | EBPF_CALL, 0, 0, 0, (s32)helper_id);
    }
    else
    {
        EbpfFunctionRecord* target = ebpf_function_for_symbol(emitter->context, instruction->symbol);
        u32 key = ebpf_ir_symbol_key(emitter->context, instruction->symbol);
        if (!target || key == UINT32_MAX)
        {
            ebpf_fail(emitter->context, EBPF_ERROR_UNRESOLVED_SYMBOL,
                      ebpf_s8("external eBPF calls must use __asm__(\"bpf_helper#ID\") or name a defined subprogram"), emitter->function, 0,
                      instruction, instruction->symbol);
            return;
        }
        u64 relocation_offset = emitter->section->data.length;
        ebpf_fe_insn(emitter, EBPF_CLASS_JMP | EBPF_CALL, 0, EBPF_PSEUDO_CALL, 0, -1);
        ebpf_section_add_relocation(emitter->context, emitter->section, relocation_offset, key, EBPF_R_BPF_64_32);
    }
    ebpf_fe_store_result(emitter, instruction, EBPF_REG_0, true,
                         ebpf_type(emitter->context, instruction->canonical_type) &&
                             ebpf_type(emitter->context, instruction->canonical_type)->kind == IR_TYPE_INTEGER &&
                             ebpf_type(emitter->context, instruction->canonical_type)->is_signed);
}

static void ebpf_fe_emit_switch(EbpfFunctionEmitter* emitter, IrBlock* predecessor, IrInstruction* instruction)
{
    if (!instruction->operand_count || instruction->target_count != instruction->immediate_count + 1)
    {
        ebpf_fail(emitter->context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("invalid eBPF switch shape"), emitter->function, predecessor,
                  instruction, IR_SYMBOL_ID_INVALID);
        return;
    }
    ebpf_fe_emit_value(emitter, EBPF_REG_8, instruction->operands[0]);
    u32* case_jumps = arena_allocate(emitter->context->arena, u32, instruction->immediate_count ? instruction->immediate_count : 1);
    for (u32 case_index = 0; case_index < instruction->immediate_count; case_index += 1)
    {
        ebpf_fe_mov_imm(emitter, EBPF_REG_9, (s64)instruction->immediates[case_index]);
        case_jumps[case_index] = ebpf_section_instruction_count(emitter->section);
        ebpf_fe_insn(emitter, EBPF_CLASS_JMP | EBPF_JEQ | EBPF_SRC_X, EBPF_REG_8, EBPF_REG_9, 0, 0);
    }
    IrBlock* default_target = instruction->targets[instruction->target_count - 1].value < emitter->function->block_count
                                  ? emitter->function->blocks + instruction->targets[instruction->target_count - 1].value
                                  : 0;
    if (!default_target)
    {
        ebpf_fail(emitter->context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("invalid eBPF switch default target"), emitter->function, predecessor,
                  instruction, IR_SYMBOL_ID_INVALID);
        return;
    }
    ebpf_fe_parallel_copy(emitter, predecessor, default_target);
    ebpf_fe_jump_to_block(emitter, default_target->id);
    for (u32 case_index = 0; case_index < instruction->immediate_count && !ebpf_failed(emitter->context); case_index += 1)
    {
        u32 stub = ebpf_section_instruction_count(emitter->section);
        ebpf_patch_jump(emitter->context, emitter->section, case_jumps[case_index], stub);
        IrBlockId target_id = instruction->targets[case_index];
        if (target_id.value >= emitter->function->block_count)
        {
            ebpf_fail(emitter->context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("invalid eBPF switch case target"), emitter->function,
                      predecessor, instruction, IR_SYMBOL_ID_INVALID);
            return;
        }
        IrBlock* target = emitter->function->blocks + target_id.value;
        ebpf_fe_parallel_copy(emitter, predecessor, target);
        ebpf_fe_jump_to_block(emitter, target->id);
    }
}

static void ebpf_fe_emit_instruction(EbpfFunctionEmitter* emitter, IrBlock* block, IrInstruction* instruction)
{
    EbpfContext* context = emitter->context;
    IrType* type = ebpf_type(context, instruction->canonical_type);
    switch (instruction->opcode)
    {
    case IR_OPCODE_ARGUMENT:
    case IR_OPCODE_LOCAL:
    case IR_OPCODE_GLOBAL:
    case IR_OPCODE_CONSTANT_INTEGER:
    case IR_OPCODE_CONSTANT_STRING:
    case IR_OPCODE_UNDEFINED:
    case IR_OPCODE_FUNCTION:
    case IR_OPCODE_LENGTH:
    case IR_OPCODE_INDEX:
    case IR_OPCODE_FIELD:
    case IR_OPCODE_ENUM:
    case IR_OPCODE_ADDRESS_OF:
    case IR_OPCODE_DEREFERENCE:
        // These values are captured in the prologue or rematerialized at use.
        break;
    case IR_OPCODE_LOAD:
    {
        if (!type || !ebpf_type_is_scalar(type))
        {
            bool unused = instruction->result.value == IR_ID_UNDERLYING_INVALID ||
                          instruction->result.value >= emitter->function->value_count ||
                          emitter->use_counts[instruction->result.value] == 0;
            if (unused && !instruction->volatile_access)
            {
                // The C frontend may form an unused aggregate load while
                // reducing &object. Native optimizers discard it later; the
                // direct backend must do so before scalar legality checks.
                break;
            }
            ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_AGGREGATE, ebpf_s8("aggregate eBPF loads are unsupported"), emitter->function, block,
                      instruction, IR_SYMBOL_ID_INVALID);
            break;
        }
        u32 size = ebpf_type_size(type);
        if (!size || size > 8)
        {
            ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_TYPE, ebpf_s8("invalid eBPF load type width"), emitter->function, block, instruction,
                      IR_SYMBOL_ID_INVALID);
            break;
        }
        ebpf_fe_emit_value(emitter, EBPF_REG_1, instruction->operands[0]);
        ebpf_fe_load_memory(emitter, EBPF_REG_0, EBPF_REG_1, 0, size);
        ebpf_fe_store_result(emitter, instruction, EBPF_REG_0, ebpf_type_is_integer(type),
                             type->kind == IR_TYPE_INTEGER && type->is_signed);
    }
    break;
    case IR_OPCODE_STORE:
    {
        if (instruction->operand_count != 2)
        {
            ebpf_fail(context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("invalid eBPF store shape"), emitter->function, block, instruction,
                      IR_SYMBOL_ID_INVALID);
            break;
        }
        IrType* stored_type = ebpf_fe_value_type(emitter, instruction->operands[1]);
        u32 size = ebpf_type_size(stored_type);
        if (!ebpf_type_is_scalar(stored_type) || !size || size > 8)
        {
            ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_AGGREGATE, ebpf_s8("aggregate eBPF stores are unsupported"), emitter->function, block,
                      instruction, IR_SYMBOL_ID_INVALID);
            break;
        }
        ebpf_fe_emit_value(emitter, EBPF_REG_1, instruction->operands[0]);
        ebpf_fe_emit_value(emitter, EBPF_REG_0, instruction->operands[1]);
        ebpf_fe_store_memory(emitter, EBPF_REG_1, 0, EBPF_REG_0, size);
    }
    break;
    case IR_OPCODE_CONSTANT_FLOAT:
        ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_TYPE, ebpf_s8("floating-point values are unsupported by eBPF"), emitter->function, block,
                  instruction, IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_CAST:
        ebpf_fe_emit_cast(emitter, instruction);
        break;
    case IR_OPCODE_UNARY:
        ebpf_fe_emit_unary(emitter, instruction);
        break;
    case IR_OPCODE_BINARY:
        ebpf_fe_emit_binary(emitter, instruction);
        break;
    case IR_OPCODE_CALL:
        ebpf_fe_emit_call(emitter, instruction);
        break;
    case IR_OPCODE_BRANCH:
    {
        if (instruction->target_count != 1 || instruction->targets[0].value >= emitter->function->block_count)
        {
            ebpf_fail(context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("invalid eBPF branch target"), emitter->function, block, instruction,
                      IR_SYMBOL_ID_INVALID);
            break;
        }
        IrBlock* target = emitter->function->blocks + instruction->targets[0].value;
        ebpf_fe_parallel_copy(emitter, block, target);
        ebpf_fe_jump_to_block(emitter, target->id);
    }
    break;
    case IR_OPCODE_BRANCH_IF:
    {
        if (instruction->operand_count != 1 || instruction->target_count != 2 ||
            instruction->targets[0].value >= emitter->function->block_count || instruction->targets[1].value >= emitter->function->block_count)
        {
            ebpf_fail(context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("invalid eBPF conditional branch"), emitter->function, block, instruction,
                      IR_SYMBOL_ID_INVALID);
            break;
        }
        ebpf_fe_emit_value(emitter, EBPF_REG_0, instruction->operands[0]);
        u32 true_branch = ebpf_section_instruction_count(emitter->section);
        ebpf_fe_insn(emitter, EBPF_CLASS_JMP | EBPF_JNE | EBPF_SRC_K, EBPF_REG_0, 0, 0, 0);
        IrBlock* false_target = emitter->function->blocks + instruction->targets[1].value;
        ebpf_fe_parallel_copy(emitter, block, false_target);
        ebpf_fe_jump_to_block(emitter, false_target->id);
        u32 true_stub = ebpf_section_instruction_count(emitter->section);
        ebpf_patch_jump(context, emitter->section, true_branch, true_stub);
        IrBlock* true_target = emitter->function->blocks + instruction->targets[0].value;
        ebpf_fe_parallel_copy(emitter, block, true_target);
        ebpf_fe_jump_to_block(emitter, true_target->id);
    }
    break;
    case IR_OPCODE_SWITCH:
        ebpf_fe_emit_switch(emitter, block, instruction);
        break;
    case IR_OPCODE_RETURN:
        if (instruction->operand_count == 1)
        {
            ebpf_fe_emit_value(emitter, EBPF_REG_0, instruction->operands[0]);
        }
        else
        {
            ebpf_fe_mov_imm(emitter, EBPF_REG_0, 0);
        }
        ebpf_fe_insn(emitter, EBPF_CLASS_JMP | EBPF_EXIT, 0, 0, 0, 0);
        break;
    case IR_OPCODE_UNREACHABLE:
        // Verifier-valid terminal sequence. Reaching it is still undefined at
        // the source level, but emitting EXIT avoids manufacturing an invalid
        // fallthrough path in the object.
        ebpf_fe_mov_imm(emitter, EBPF_REG_0, 0);
        ebpf_fe_insn(emitter, EBPF_CLASS_JMP | EBPF_EXIT, 0, 0, 0, 0);
        break;
    case IR_OPCODE_STACK_ALLOCATE:
    case IR_OPCODE_STACK_SAVE:
    case IR_OPCODE_STACK_RESTORE:
        ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_INSTRUCTION, ebpf_s8("dynamic stack allocation is unsupported by eBPF"), emitter->function,
                  block, instruction, IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_ARRAY:
    case IR_OPCODE_AGGREGATE:
    case IR_OPCODE_SLICE:
    case IR_OPCODE_REVERSE:
        ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_AGGREGATE, ebpf_s8("aggregate eBPF SSA construction is unsupported"), emitter->function,
                  block, instruction, IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_VA_START:
    case IR_OPCODE_VA_COPY:
    case IR_OPCODE_VA_END:
    case IR_OPCODE_VA_ARG:
        ebpf_fail(context, EBPF_ERROR_VARIADIC, ebpf_s8("variadic operations are unsupported by eBPF"), emitter->function, block, instruction,
                  IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_ATOMIC_LOAD:
    case IR_OPCODE_ATOMIC_STORE:
    case IR_OPCODE_ATOMIC_READ_MODIFY_WRITE:
    case IR_OPCODE_ATOMIC_COMPARE_EXCHANGE:
    case IR_OPCODE_ATOMIC_FENCE:
        ebpf_fail(context, EBPF_ERROR_ATOMIC, ebpf_s8("atomic IR operations are not yet lowered by the eBPF backend"), emitter->function,
                  block, instruction, IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_SIMD:
        ebpf_fail(context, EBPF_ERROR_SIMD, ebpf_s8("SIMD operations are unsupported by eBPF"), emitter->function, block, instruction,
                  IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_INLINE_ASSEMBLY:
        ebpf_fail(context, EBPF_ERROR_INLINE_ASSEMBLY, ebpf_s8("inline assembly is unsupported by the eBPF backend"), emitter->function,
                  block, instruction, IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_LABEL_ADDRESS:
    case IR_OPCODE_INDIRECT_BRANCH:
        ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_INSTRUCTION, ebpf_s8("computed label control flow is unsupported by eBPF"),
                  emitter->function, block, instruction, IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_CLEAR_INSTRUCTION_CACHE:
    case IR_OPCODE_DEBUG_TRAP:
        ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_INSTRUCTION, ebpf_s8("instruction-cache and debug-trap operations are unsupported by eBPF"),
                  emitter->function, block, instruction, IR_SYMBOL_ID_INVALID);
        break;
    case IR_OPCODE_COUNT:
        ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_INSTRUCTION, ebpf_s8("invalid eBPF IR opcode"), emitter->function, block, instruction,
                  IR_SYMBOL_ID_INVALID);
        break;
    }
}

static bool ebpf_emit_function(EbpfContext* context, EbpfFunctionRecord* record)
{
    EbpfFunctionEmitter emitter = {.context = context, .record = record, .function = record->function, .section = record->section};
    if (!ebpf_fe_validate_abi(&emitter) || !ebpf_fe_allocate(&emitter))
    {
        return false;
    }
    emitter.block_starts = arena_allocate(context->arena, u32, emitter.function->block_count ? emitter.function->block_count : 1);
    for (u32 index = 0; index < emitter.function->block_count; index += 1)
    {
        emitter.block_starts[index] = UINT32_MAX;
    }
    record->offset = emitter.section->data.length;
    emitter.section_start = record->offset;
    ebpf_fe_emit_prologue(&emitter);
    for (u32 block_index = 0; block_index < emitter.function->block_count && !ebpf_failed(context); block_index += 1)
    {
        IrBlock* block = emitter.function->blocks + block_index;
        if (block->id.value >= emitter.function->block_count)
        {
            ebpf_fail(context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("eBPF block ID is out of range"), emitter.function, block, 0,
                      IR_SYMBOL_ID_INVALID);
            break;
        }
        emitter.block_starts[block->id.value] = ebpf_section_instruction_count(emitter.section);
        IrInstructionId instruction_id = block->first_instruction;
        while (instruction_id.value != IR_ID_UNDERLYING_INVALID && !ebpf_failed(context))
        {
            if (instruction_id.value >= emitter.function->instruction_count)
            {
                ebpf_fail(context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("eBPF block instruction is out of range"), emitter.function, block, 0,
                          IR_SYMBOL_ID_INVALID);
                break;
            }
            IrInstruction* instruction = emitter.function->instructions + instruction_id.value;
            ebpf_fe_emit_instruction(&emitter, block, instruction);
            instruction_id = instruction->next;
        }
    }
    for (u32 patch_index = 0; patch_index < emitter.patch_count && !ebpf_failed(context); patch_index += 1)
    {
        EbpfBranchPatch* patch = emitter.patches + patch_index;
        if (patch->target.value >= emitter.function->block_count || emitter.block_starts[patch->target.value] == UINT32_MAX)
        {
            ebpf_fail(context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("unresolved eBPF branch target"), emitter.function, 0, 0,
                      IR_SYMBOL_ID_INVALID);
            break;
        }
        ebpf_patch_jump(context, emitter.section, patch->instruction_index, emitter.block_starts[patch->target.value]);
    }
    if (ebpf_failed(context))
    {
        return false;
    }
    record->size = emitter.section->data.length - record->offset;
    context->stats.instruction_count += record->size / 8;
    context->stats.code_bytes += record->size;
    return true;
}

static bool ebpf_initialize_symbols(EbpfContext* context)
{
    u32 count = context->program->symbols.count;
    context->ir_symbol_keys = arena_allocate(context->arena, u32, count ? count : 1);
    context->next_symbol_key = count;
    for (u32 index = 0; index < count; index += 1)
    {
        context->ir_symbol_keys[index] = index;
        IrSymbol* symbol = context->program->symbols.symbols + index;
        u32 helper_id = 0;
        if (symbol->kind != IR_SYMBOL_FUNCTION && symbol->kind != IR_SYMBOL_DATA)
        {
            continue;
        }
        if (symbol->kind == IR_SYMBOL_FUNCTION && ebpf_parse_helper_id(ebpf_symbol_name(symbol), &helper_id))
        {
            continue;
        }
        u8 binding = symbol->linkage == IR_LINKAGE_INTERNAL ? EBPF_STB_LOCAL : EBPF_STB_GLOBAL;
        u8 type = symbol->kind == IR_SYMBOL_FUNCTION ? EBPF_STT_FUNC : EBPF_STT_OBJECT;
        ebpf_add_symbol_record(context, index, ebpf_symbol_name(symbol), symbol, binding, type, false);
    }
    return true;
}

static bool ebpf_reserve_sections(EbpfContext* context)
{
    // Function/global records and symbol records retain EbpfSection pointers.
    // Reserve the complete upper bound before creating the first section so
    // arena-backed vector growth can never invalidate those pointers.
    u64 definition_count = 0;
    for (u32 module_index = 0; module_index < context->module_count; module_index += 1)
    {
        IrModule* module = context->modules + module_index;
        definition_count += (u64)module->function_count + module->global_count;
    }

    // In the worst case every definition has a unique content section and a
    // corresponding relocation section.  The remaining entries cover
    // .rodata strings, .BTF, .symtab, .strtab, and .shstrtab with margin.
    u64 required = definition_count * 2 + 16;
    if (required > UINT16_MAX - 1)
    {
        ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("eBPF object exceeds the supported ELF section count"), 0, 0, 0,
                  IR_SYMBOL_ID_INVALID);
        return false;
    }
    ebpf_vector_reserve(context->arena, (void**)&context->sections, &context->section_capacity, (u32)required,
                        sizeof(*context->sections));
    return true;
}

static bool ebpf_collect_functions(EbpfContext* context)
{
    for (u32 module_index = 0; module_index < context->module_count; module_index += 1)
    {
        IrModule* module = context->modules + module_index;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            if (function->state == IR_FUNCTION_DECLARATION || function->state == IR_FUNCTION_NOT_LOWERED)
            {
                continue;
            }
            if (function->state != IR_FUNCTION_LOWERED)
            {
                ebpf_fail(context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("cannot emit a rejected eBPF function"), function, 0, 0,
                          function->symbol);
                return false;
            }
            IrSymbol* symbol = ebpf_ir_symbol(context, function->symbol);
            if (!symbol || symbol->kind != IR_SYMBOL_FUNCTION)
            {
                ebpf_fail(context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("eBPF function has no function symbol"), function, 0, 0,
                          function->symbol);
                return false;
            }
            if (symbol->is_thread_local)
            {
                ebpf_fail(context, EBPF_ERROR_TLS, ebpf_s8("thread-local functions are invalid for eBPF"), function, 0, 0, function->symbol);
                return false;
            }
            String8 requested = symbol->section_name;
            bool is_program = requested.length && !ebpf_string_equal(requested, S8(".text"));
            String8 section_name = is_program ? requested : S8(".text");
            EbpfSection* section = ebpf_section_get(context, section_name, EBPF_SHT_PROGBITS,
                                                    EBPF_SHF_ALLOC | EBPF_SHF_EXECINSTR, 8, 0);
            if (!section)
            {
                return false;
            }
            u32 key = ebpf_ir_symbol_key(context, function->symbol);
            EbpfSymbolRecord* symbol_record = ebpf_symbol_by_key(context, key);
            if (!symbol_record)
            {
                symbol_record = ebpf_add_symbol_record(context, key, ebpf_symbol_name(symbol), symbol,
                                                       symbol->linkage == IR_LINKAGE_INTERNAL ? EBPF_STB_LOCAL : EBPF_STB_GLOBAL,
                                                       EBPF_STT_FUNC, false);
            }
            if (symbol_record->defined)
            {
                ebpf_fail(context, EBPF_ERROR_DUPLICATE_SYMBOL, ebpf_s8("duplicate eBPF function definition"), function, 0, 0,
                          function->symbol);
                return false;
            }
            symbol_record->defined = true;
            symbol_record->section = section;
            ebpf_vector_reserve(context->arena, (void**)&context->functions, &context->function_capacity, context->function_count + 1,
                                sizeof(*context->functions));
            context->functions[context->function_count++] =
                (EbpfFunctionRecord){.function = function, .symbol = symbol, .section = section, .is_program = is_program};
            context->stats.function_count += 1;
            if (is_program)
            {
                context->stats.program_count += 1;
            }
            else
            {
                context->stats.subprogram_count += 1;
            }
        }
    }
    return true;
}

static void ebpf_store_u64_at(u8* bytes, u64 value)
{
    for (u32 index = 0; index < 8; index += 1)
    {
        bytes[index] = (u8)(value >> (index * 8));
    }
}

static bool ebpf_global_has_storage_bytes(IrGlobal* global)
{
    return global->initializer_kind != IR_GLOBAL_INITIALIZER_NONE && global->initializer_kind != IR_GLOBAL_INITIALIZER_ZERO;
}

static String8 ebpf_global_section_name(IrGlobal* global, IrSymbol* symbol)
{
    String8 result;
    if (symbol->section_name.length)
    {
        result = symbol->section_name;
    }
    else if (!ebpf_global_has_storage_bytes(global) && !global->relocation_count)
    {
        result = S8(".bss");
    }
    else
    {
        result = global->is_read_only ? S8(".rodata") : S8(".data");
    }

    return result;
}

static bool ebpf_collect_global(EbpfContext* context, IrGlobal* global)
{
    IrSymbol* symbol = ebpf_ir_symbol(context, global->symbol);
    IrType* type = ebpf_type(context, global->type);
    if (!symbol || symbol->kind != IR_SYMBOL_DATA || !type || !type->layout.resolved || type->layout.size > UINT32_MAX)
    {
        ebpf_fail(context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("invalid eBPF global symbol or layout"), 0, 0, 0, global->symbol);
        return false;
    }
    if (global->is_thread_local || symbol->is_thread_local)
    {
        ebpf_fail(context, EBPF_ERROR_TLS, ebpf_s8("thread-local storage is unsupported by eBPF"), 0, 0, 0, global->symbol);
        return false;
    }
    String8 section_name = ebpf_global_section_name(global, symbol);
    if (ebpf_string_equal(section_name, S8("maps")))
    {
        ebpf_fail(context, EBPF_ERROR_BTF,
                  ebpf_s8("legacy SEC(\"maps\") definitions are unsupported by modern libbpf; use a BTF-style SEC(\".maps\") definition"),
                  0, 0, 0, global->symbol);
        return false;
    }
    bool nobits = ebpf_string_equal(section_name, S8(".bss"));
    u64 flags = EBPF_SHF_ALLOC;
    if (!global->is_read_only || ebpf_string_equal(section_name, S8(".maps")) || nobits)
    {
        flags |= EBPF_SHF_WRITE;
    }
    u32 section_type = nobits ? EBPF_SHT_NOBITS : EBPF_SHT_PROGBITS;
    u64 alignment = global->alignment ? global->alignment : type->layout.alignment;
    if (!alignment || (alignment & (alignment - 1)) || alignment > UINT32_MAX)
    {
        ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_TYPE, ebpf_s8("invalid eBPF global alignment"), 0, 0, 0, global->symbol);
        return false;
    }
    EbpfSection* section = ebpf_section_get(context, section_name, section_type, flags, alignment, 0);
    if (!section)
    {
        return false;
    }
    u64 size = type->layout.size;
    u64 offset = 0;
    if (nobits)
    {
        offset = ebpf_align_up(section->logical_size, alignment);
        section->logical_size = offset + size;
        if (ebpf_global_has_storage_bytes(global) || global->relocation_count)
        {
            ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("initialized eBPF global cannot be placed in .bss"), 0, 0, 0,
                      global->symbol);
            return false;
        }
    }
    else
    {
        if (!ebpf_buffer_align(&section->data, alignment))
        {
            ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("eBPF global section size overflow"), 0, 0, 0, global->symbol);
            return false;
        }
        offset = section->data.length;
        if (!ebpf_buffer_zeros(&section->data, size))
        {
            ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("eBPF global data size overflow"), 0, 0, 0, global->symbol);
            return false;
        }
        u8* destination = section->data.data + offset;
        switch (global->initializer_kind)
        {
        case IR_GLOBAL_INITIALIZER_NONE:
        case IR_GLOBAL_INITIALIZER_ZERO: break;
        case IR_GLOBAL_INITIALIZER_INTEGER:
        case IR_GLOBAL_INITIALIZER_FLOAT:
            if (size > 8)
            {
                ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("scalar eBPF global initializer exceeds 64 bits"), 0, 0, 0,
                          global->symbol);
                return false;
            }
            for (u32 byte = 0; byte < (u32)size; byte += 1)
            {
                destination[byte] = (u8)(global->initializer_bits >> (byte * 8));
            }
            break;
        case IR_GLOBAL_INITIALIZER_BYTES:
            if (global->bytes.length > size)
            {
                ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("eBPF global initializer is larger than its object"), 0, 0, 0,
                          global->symbol);
                return false;
            }
            if (global->bytes.length)
            {
                memcpy(destination, global->bytes.pointer, (size_t)global->bytes.length);
            }
            break;
        case IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS:
        {
            if (size < 8)
            {
                ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("eBPF symbol-address initializer requires eight bytes"), 0, 0, 0,
                          global->symbol);
                return false;
            }
            u32 target_key = ebpf_ir_symbol_key(context, global->initializer_symbol);
            if (target_key == UINT32_MAX)
            {
                ebpf_fail(context, EBPF_ERROR_UNRESOLVED_SYMBOL, ebpf_s8("unresolved eBPF data initializer symbol"), 0, 0, 0,
                          global->initializer_symbol);
                return false;
            }
            ebpf_store_u64_at(destination, (u64)global->initializer_addend);
            ebpf_section_add_relocation(context, section, offset, target_key, EBPF_R_BPF_64_ABS64);
        }
        break;
        case IR_GLOBAL_INITIALIZER_COUNT:
            ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("invalid eBPF global initializer kind"), 0, 0, 0, global->symbol);
            return false;
        }
        for (u32 relocation_index = 0; relocation_index < global->relocation_count; relocation_index += 1)
        {
            IrGlobalRelocation* relocation = global->relocations + relocation_index;
            if (relocation->is_label_address || relocation->offset > size || size - relocation->offset < 8)
            {
                ebpf_fail(context, EBPF_ERROR_UNSUPPORTED_INSTRUCTION,
                          ebpf_s8("label or out-of-bounds eBPF data relocation is unsupported"), 0, 0, 0, relocation->symbol);
                return false;
            }
            u32 target_key = ebpf_ir_symbol_key(context, relocation->symbol);
            if (target_key == UINT32_MAX)
            {
                ebpf_fail(context, EBPF_ERROR_UNRESOLVED_SYMBOL, ebpf_s8("unresolved eBPF data relocation symbol"), 0, 0, 0,
                          relocation->symbol);
                return false;
            }
            ebpf_store_u64_at(destination + relocation->offset, (u64)relocation->addend);
            ebpf_section_add_relocation(context, section, offset + relocation->offset, target_key, EBPF_R_BPF_64_ABS64);
        }
        section->logical_size = section->data.length;
    }

    ebpf_vector_reserve(context->arena, (void**)&context->globals, &context->global_capacity, context->global_count + 1,
                        sizeof(*context->globals));
    context->globals[context->global_count++] =
        (EbpfGlobalRecord){.global = global, .symbol = symbol, .section = section, .offset = offset, .size = size};
    u32 key = ebpf_ir_symbol_key(context, global->symbol);
    EbpfSymbolRecord* symbol_record = ebpf_symbol_by_key(context, key);
    if (!symbol_record)
    {
        symbol_record = ebpf_add_symbol_record(context, key, ebpf_symbol_name(symbol), symbol,
                                               symbol->linkage == IR_LINKAGE_INTERNAL ? EBPF_STB_LOCAL : EBPF_STB_GLOBAL,
                                               EBPF_STT_OBJECT, false);
    }
    if (symbol_record->defined)
    {
        ebpf_fail(context, EBPF_ERROR_DUPLICATE_SYMBOL, ebpf_s8("duplicate eBPF global definition"), 0, 0, 0, global->symbol);
        return false;
    }
    symbol_record->defined = true;
    symbol_record->section = section;
    symbol_record->value = offset;
    symbol_record->size = size;
    context->stats.global_count += 1;
    if (ebpf_string_equal(section_name, S8(".maps")))
    {
        context->stats.map_count += 1;
    }
    context->stats.data_bytes += size;
    return true;
}

static bool ebpf_collect_globals(EbpfContext* context)
{
    for (u32 module_index = 0; module_index < context->module_count; module_index += 1)
    {
        IrModule* module = context->modules + module_index;
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            if (!ebpf_collect_global(context, module->globals + global_index))
            {
                return false;
            }
        }
    }
    return true;
}

static bool ebpf_collect_strings(EbpfContext* context)
{
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
                String8 literal = ir_instruction_extra(function, ir_instruction_self_id(function, instruction)).literal;
                EbpfSection* section = ebpf_section_get(context, S8(".rodata"), EBPF_SHT_PROGBITS, EBPF_SHF_ALLOC, 1, 0);
                if (!section)
                {
                    return false;
                }
                u64 offset = section->data.length;
                if (!ebpf_buffer_bytes(&section->data, literal.pointer, literal.length) || !ebpf_buffer_u8(&section->data, 0))
                {
                    ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("eBPF string section size overflow"), function, 0, instruction,
                              IR_SYMBOL_ID_INVALID);
                    return false;
                }
                section->logical_size = section->data.length;
                u32 key = context->next_symbol_key++;
                EbpfSymbolRecord* symbol = ebpf_add_symbol_record(context, key, S8(".L.str"), 0, EBPF_STB_LOCAL, EBPF_STT_OBJECT, true);
                symbol->defined = true;
                symbol->section = section;
                symbol->value = offset;
                symbol->size = literal.length + 1;
                ebpf_vector_reserve(context->arena, (void**)&context->strings, &context->string_capacity, context->string_count + 1,
                                    sizeof(*context->strings));
                context->strings[context->string_count++] =
                    (EbpfStringRecord){.function = function, .instruction = ir_instruction_self_id(function, instruction), .section = section, .literal = literal,
                                       .offset = offset, .symbol_key = key};
                context->stats.data_bytes += literal.length + 1;
            }
        }
    }
    return true;
}

static bool ebpf_emit_functions(EbpfContext* context)
{
    for (u32 index = 0; index < context->function_count; index += 1)
    {
        EbpfFunctionRecord* function = context->functions + index;
        if (!ebpf_emit_function(context, function))
        {
            return false;
        }
        u32 key = ebpf_ir_symbol_key(context, function->function->symbol);
        EbpfSymbolRecord* symbol = ebpf_symbol_by_key(context, key);
        if (!symbol)
        {
            ebpf_fail(context, EBPF_ERROR_IR_VALIDATION, ebpf_s8("missing eBPF function symbol record"), function->function, 0, 0,
                      function->function->symbol);
            return false;
        }
        symbol->value = function->offset;
        symbol->size = function->size;
        function->section->logical_size = function->section->data.length;
    }
    return true;
}

static u32 ebpf_btf_string(EbpfContext* context, String8 string)
{
    if (!string.length)
    {
        return 0;
    }
    if (context->btf_strings.length > UINT32_MAX || string.length + 1 > UINT32_MAX - context->btf_strings.length)
    {
        ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("eBPF BTF string table exceeds 32-bit offsets"), 0, 0, 0,
                  IR_SYMBOL_ID_INVALID);
        return 0;
    }
    u32 offset = (u32)context->btf_strings.length;
    if (!ebpf_buffer_bytes(&context->btf_strings, string.pointer, string.length) || !ebpf_buffer_u8(&context->btf_strings, 0))
    {
        ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("failed to grow eBPF BTF string table"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return 0;
    }
    return offset;
}

static u32 ebpf_btf_record_reserve(EbpfContext* context)
{
    ebpf_vector_reserve(context->arena, (void**)&context->btf_records, &context->btf_record_capacity,
                        context->btf_record_count + 1, sizeof(*context->btf_records));
    u32 id = context->btf_record_count + 1;
    context->btf_records[context->btf_record_count++] = (EbpfBtfRecord){0};
    return id;
}

static u32 ebpf_btf_info(u32 kind, u32 vlen, bool kind_flag)
{
    return (kind_flag ? UINT32_C(1) << 31 : 0) | (kind << 24) | (vlen & 0xffff);
}

static u32 ebpf_btf_type(EbpfContext* context, IrTypeId type_id);

static u32 ebpf_btf_u32(EbpfContext* context)
{
    u32 result;
    if (context->btf_u32_type)
    {
        result = context->btf_u32_type;
    }
    else
    {
        u32 id = ebpf_btf_record_reserve(context);
        EbpfBtfRecord record = {.name_offset = ebpf_btf_string(context, S8("unsigned int")),
                                .info = ebpf_btf_info(EBPF_BTF_KIND_INT, 0, false), .size_or_type = 4};
        ebpf_buffer_init(&record.extra, context->arena);
        ebpf_buffer_u32(&record.extra, 32);
        context->btf_records[id - 1] = record;
        context->btf_u32_type = id;
        result = id;
    }

    return result;
}

static bool ebpf_btf_type_supported(IrType* type)
{
    if (!type)
    {
        return false;
    }
    switch (type->kind)
    {
    case IR_TYPE_VOID:
    case IR_TYPE_BOOLEAN:
    case IR_TYPE_INTEGER:
    case IR_TYPE_FLOAT:
    case IR_TYPE_POINTER:
    case IR_TYPE_ARRAY:
    case IR_TYPE_STRUCT:
    case IR_TYPE_UNION:
    case IR_TYPE_ENUM: return true;
    default: return false;
    }
}

static u32 ebpf_btf_type(EbpfContext* context, IrTypeId type_id)
{
    if (type_id.value >= context->program->types.count)
    {
        ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("eBPF BTF type ID is out of range"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return 0;
    }
    IrType* type = ebpf_type(context, type_id);
    if (!type)
    {
        ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("missing eBPF BTF type"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return 0;
    }
    if (type->kind == IR_TYPE_VOID)
    {
        context->btf_type_ids[type_id.value] = 0;
        return 0;
    }
    if (context->btf_type_ids[type_id.value] != UINT32_MAX)
    {
        return context->btf_type_ids[type_id.value];
    }
    if (!ebpf_btf_type_supported(type) || !type->layout.resolved || type->layout.size > UINT32_MAX)
    {
        ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("map type cannot be represented in base BTF"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return 0;
    }

    u32 id = ebpf_btf_record_reserve(context);
    context->btf_type_ids[type_id.value] = id;
    EbpfBtfRecord record = {.name_offset = ebpf_btf_string(context, type->name)};
    ebpf_buffer_init(&record.extra, context->arena);
    switch (type->kind)
    {
    case IR_TYPE_BOOLEAN:
    case IR_TYPE_INTEGER:
    {
        u32 bits = ebpf_type_bits(type);
        if (!bits || bits > 255 || type->layout.size > UINT32_MAX)
        {
            ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("invalid integer width in eBPF BTF"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
            return 0;
        }
        u32 encoding = type->kind == IR_TYPE_BOOLEAN ? EBPF_BTF_INT_BOOL : type->is_signed ? EBPF_BTF_INT_SIGNED : 0;
        if (type->name.length == 4 && memcmp(type->name.pointer, "char", 4) == 0)
        {
            encoding |= EBPF_BTF_INT_CHAR;
        }
        record.info = ebpf_btf_info(EBPF_BTF_KIND_INT, 0, false);
        record.size_or_type = (u32)type->layout.size;
        ebpf_buffer_u32(&record.extra, (encoding << 24) | bits);
    }
    break;
    case IR_TYPE_FLOAT:
        if (type->layout.size != 4 && type->layout.size != 8 && type->layout.size != 16)
        {
            ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("unsupported floating-point width in eBPF BTF"), 0, 0, 0,
                      IR_SYMBOL_ID_INVALID);
            return 0;
        }
        record.info = ebpf_btf_info(EBPF_BTF_KIND_FLOAT, 0, false);
        record.size_or_type = (u32)type->layout.size;
        break;
    case IR_TYPE_POINTER:
        record.info = ebpf_btf_info(EBPF_BTF_KIND_PTR, 0, false);
        record.size_or_type = ebpf_btf_type(context, type->element_type);
        break;
    case IR_TYPE_ARRAY:
    {
        if (type->element_count > UINT32_MAX)
        {
            ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("eBPF BTF array length exceeds 32 bits"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
            return 0;
        }
        record.info = ebpf_btf_info(EBPF_BTF_KIND_ARRAY, 0, false);
        record.size_or_type = 0;
        u32 element = ebpf_btf_type(context, type->element_type);
        u32 index = ebpf_btf_u32(context);
        ebpf_buffer_u32(&record.extra, element);
        ebpf_buffer_u32(&record.extra, index);
        ebpf_buffer_u32(&record.extra, (u32)type->element_count);
    }
    break;
    case IR_TYPE_STRUCT:
    case IR_TYPE_UNION:
    {
        if (type->field_count > UINT16_MAX)
        {
            ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("eBPF BTF aggregate has too many fields"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
            return 0;
        }
        bool has_bit_fields = false;
        for (u32 field_index = 0; field_index < type->field_count; field_index += 1)
        {
            has_bit_fields |= type->fields[field_index].is_bit_field;
        }
        record.info = ebpf_btf_info(type->kind == IR_TYPE_STRUCT ? EBPF_BTF_KIND_STRUCT : EBPF_BTF_KIND_UNION,
                                    type->field_count, has_bit_fields);
        record.size_or_type = (u32)type->layout.size;
        for (u32 field_index = 0; field_index < type->field_count; field_index += 1)
        {
            IrField* field = type->fields + field_index;
            u64 bit_offset = field->offset * 8 + field->bit_offset;
            if (bit_offset > UINT32_MAX || (has_bit_fields && (bit_offset > 0x00ffffff || field->bit_width > 255)))
            {
                ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("eBPF BTF field offset is out of range"), 0, 0, 0,
                          IR_SYMBOL_ID_INVALID);
                return 0;
            }
            u32 encoded_offset = (u32)bit_offset;
            if (has_bit_fields)
            {
                encoded_offset = (field->is_bit_field ? field->bit_width : 0) << 24 | (u32)bit_offset;
            }
            ebpf_buffer_u32(&record.extra, ebpf_btf_string(context, field->name));
            ebpf_buffer_u32(&record.extra, ebpf_btf_type(context, field->type));
            ebpf_buffer_u32(&record.extra, encoded_offset);
        }
    }
    break;
    case IR_TYPE_ENUM:
    {
        if (type->enum_member_count > UINT16_MAX)
        {
            ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("eBPF BTF enum has too many members"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
            return 0;
        }
        bool enum64 = type->layout.size > 4;
        record.info = ebpf_btf_info(enum64 ? EBPF_BTF_KIND_ENUM64 : EBPF_BTF_KIND_ENUM, type->enum_member_count, type->is_signed);
        record.size_or_type = (u32)type->layout.size;
        for (u32 member_index = 0; member_index < type->enum_member_count; member_index += 1)
        {
            IrEnumMember* member = type->enum_members + member_index;
            ebpf_buffer_u32(&record.extra, ebpf_btf_string(context, member->name));
            ebpf_buffer_u32(&record.extra, (u32)member->value);
            if (enum64)
            {
                ebpf_buffer_u32(&record.extra, (u32)(member->value >> 32));
            }
        }
    }
    break;
    case IR_TYPE_VOID:
    case IR_TYPE_VA_LIST:
    case IR_TYPE_SLICE:
    case IR_TYPE_VECTOR:
    case IR_TYPE_FUNCTION:
    case IR_TYPE_RANGE:
    case IR_TYPE_COUNT:
        ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("unsupported eBPF BTF type kind"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return 0;
    }
    if (ebpf_failed(context))
    {
        return 0;
    }
    context->btf_records[id - 1] = record;
    return id;
}

static bool ebpf_build_btf(EbpfContext* context)
{
    if (!context->options.emit_btf || !context->stats.map_count)
    {
        return true;
    }
    ebpf_buffer_init(&context->btf_strings, context->arena);
    if (!ebpf_buffer_u8(&context->btf_strings, 0))
    {
        return false;
    }
    u32 type_count = context->program->types.count;
    context->btf_type_ids = arena_allocate(context->arena, u32, type_count ? type_count : 1);
    for (u32 index = 0; index < type_count; index += 1)
    {
        context->btf_type_ids[index] = UINT32_MAX;
    }

    u32 map_count = context->stats.map_count;
    u32* variable_ids = arena_allocate(context->arena, u32, map_count);
    EbpfGlobalRecord** maps = arena_allocate(context->arena, EbpfGlobalRecord*, map_count);
    u32 map_index = 0;
    EbpfSection* map_section = ebpf_section_find(context, S8(".maps"));
    for (u32 global_index = 0; global_index < context->global_count; global_index += 1)
    {
        EbpfGlobalRecord* global = context->globals + global_index;
        if (!ebpf_string_equal(global->section->name, S8(".maps")))
        {
            continue;
        }
        u32 map_type = ebpf_btf_type(context, global->global->type);
        if (ebpf_failed(context) || !map_type)
        {
            return false;
        }
        u32 variable_id = ebpf_btf_record_reserve(context);
        EbpfBtfRecord variable = {.name_offset = ebpf_btf_string(context, ebpf_symbol_name(global->symbol)),
                                  .info = ebpf_btf_info(EBPF_BTF_KIND_VAR, 0, false), .size_or_type = map_type};
        ebpf_buffer_init(&variable.extra, context->arena);
        ebpf_buffer_u32(&variable.extra, EBPF_BTF_VAR_GLOBAL_ALLOCATED);
        context->btf_records[variable_id - 1] = variable;
        variable_ids[map_index] = variable_id;
        maps[map_index] = global;
        map_index += 1;
    }
    if (map_index != map_count || !map_section || map_section->logical_size > UINT32_MAX)
    {
        ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("inconsistent eBPF .maps data section"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return false;
    }
    u32 datasec_id = ebpf_btf_record_reserve(context);
    EbpfBtfRecord datasec = {.name_offset = ebpf_btf_string(context, S8(".maps")),
                             .info = ebpf_btf_info(EBPF_BTF_KIND_DATASEC, map_count, false),
                             .size_or_type = (u32)map_section->logical_size};
    ebpf_buffer_init(&datasec.extra, context->arena);
    for (u32 index = 0; index < map_count; index += 1)
    {
        if (maps[index]->offset > UINT32_MAX || maps[index]->size > UINT32_MAX)
        {
            ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("eBPF map offset or size exceeds BTF limits"), 0, 0, 0,
                      maps[index]->global->symbol);
            return false;
        }
        ebpf_buffer_u32(&datasec.extra, variable_ids[index]);
        ebpf_buffer_u32(&datasec.extra, (u32)maps[index]->offset);
        ebpf_buffer_u32(&datasec.extra, (u32)maps[index]->size);
    }
    context->btf_records[datasec_id - 1] = datasec;

    EbpfBuffer types = {0};
    ebpf_buffer_init(&types, context->arena);
    for (u32 index = 0; index < context->btf_record_count; index += 1)
    {
        EbpfBtfRecord* record = context->btf_records + index;
        if (!record->info)
        {
            ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("incomplete recursive eBPF BTF record"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
            return false;
        }
        ebpf_buffer_u32(&types, record->name_offset);
        ebpf_buffer_u32(&types, record->info);
        ebpf_buffer_u32(&types, record->size_or_type);
        ebpf_buffer_bytes(&types, record->extra.data, record->extra.length);
    }
    if (types.length > UINT32_MAX || context->btf_strings.length > UINT32_MAX || types.length > UINT32_MAX - context->btf_strings.length)
    {
        ebpf_fail(context, EBPF_ERROR_BTF, ebpf_s8("eBPF BTF payload exceeds 32-bit format limits"), 0, 0, 0,
                  IR_SYMBOL_ID_INVALID);
        return false;
    }
    EbpfSection* section = ebpf_section_get(context, S8(".BTF"), EBPF_SHT_PROGBITS, 0, 4, 0);
    if (!section)
    {
        return false;
    }
    ebpf_buffer_u16(&section->data, 0xeb9f);
    ebpf_buffer_u8(&section->data, 1);
    ebpf_buffer_u8(&section->data, 0);
    ebpf_buffer_u32(&section->data, 24);
    ebpf_buffer_u32(&section->data, 0);
    ebpf_buffer_u32(&section->data, (u32)types.length);
    ebpf_buffer_u32(&section->data, (u32)types.length);
    ebpf_buffer_u32(&section->data, (u32)context->btf_strings.length);
    ebpf_buffer_bytes(&section->data, types.data, types.length);
    ebpf_buffer_bytes(&section->data, context->btf_strings.data, context->btf_strings.length);
    section->logical_size = section->data.length;
    context->stats.has_btf = true;
    context->stats.btf_bytes = section->data.length;
    return !ebpf_failed(context);
}

static String8 ebpf_concat_section_name(EbpfContext* context, String8 prefix, String8 suffix)
{
    String8 result;
    if (prefix.length > UINT64_MAX - suffix.length)
    {
        result = (String8){0};
    }
    else
    {
        u64 length = prefix.length + suffix.length;
        char8* bytes = arena_allocate(context->arena, char8, length ? length : 1);
        if (prefix.length)
        {
            memcpy(bytes, prefix.pointer, (size_t)prefix.length);
        }
        if (suffix.length)
        {
            memcpy(bytes + prefix.length, suffix.pointer, (size_t)suffix.length);
        }
        result = (String8){.pointer = bytes, .length = length};
    }

    return result;
}

static u32 ebpf_elf_string(EbpfBuffer* table, String8 string)
{
    if (table->length > UINT32_MAX || string.length + 1 > UINT32_MAX - table->length)
    {
        return UINT32_MAX;
    }
    u32 offset = (u32)table->length;
    u32 result;
    if (!ebpf_buffer_bytes(table, string.pointer, string.length) || !ebpf_buffer_u8(table, 0))
    {
        result = UINT32_MAX;
    }
    else
    {
        result = offset;
    }

    return result;
}

static bool ebpf_elf_symbol(EbpfBuffer* table, u32 name, u8 binding, u8 type, u16 section, u64 value, u64 size)
{
    return ebpf_buffer_u32(table, name) && ebpf_buffer_u8(table, (u8)((binding << 4) | (type & 0xf))) &&
           ebpf_buffer_u8(table, 0) && ebpf_buffer_u16(table, section) && ebpf_buffer_u64(table, value) &&
           ebpf_buffer_u64(table, size);
}

static bool ebpf_prepare_elf_sections(EbpfContext* context, EbpfSection** symtab_out, EbpfSection** strtab_out,
                                      EbpfSection** shstrtab_out)
{
    u32 content_count = context->section_count;
    for (u32 index = 0; index < content_count; index += 1)
    {
        context->sections[index].elf_index = index + 1;
    }
    for (u32 index = 0; index < content_count; index += 1)
    {
        EbpfSection* target = context->sections + index;
        if (!target->relocation_count)
        {
            continue;
        }
        String8 name = ebpf_concat_section_name(context, S8(".rel"), target->name);
        EbpfSection* relocation = ebpf_section_get(context, name, EBPF_SHT_REL, 0, 8, 16);
        if (!relocation)
        {
            return false;
        }
        relocation->info = target->elf_index;
    }
    EbpfSection* symtab = ebpf_section_get(context, S8(".symtab"), EBPF_SHT_SYMTAB, 0, 8, 24);
    EbpfSection* strtab = ebpf_section_get(context, S8(".strtab"), EBPF_SHT_STRTAB, 0, 1, 0);
    EbpfSection* shstrtab = ebpf_section_get(context, S8(".shstrtab"), EBPF_SHT_STRTAB, 0, 1, 0);
    if (!symtab || !strtab || !shstrtab || context->section_count >= UINT16_MAX)
    {
        ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("too many eBPF ELF sections"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return false;
    }
    for (u32 index = 0; index < context->section_count; index += 1)
    {
        context->sections[index].elf_index = index + 1;
    }
    symtab->link = strtab->elf_index;
    for (u32 index = content_count; index < context->section_count; index += 1)
    {
        EbpfSection* section = context->sections + index;
        if (section->type == EBPF_SHT_REL)
        {
            section->link = symtab->elf_index;
        }
    }
    *symtab_out = symtab;
    *strtab_out = strtab;
    *shstrtab_out = shstrtab;
    return true;
}

static bool ebpf_build_symbol_table(EbpfContext* context, EbpfSection* symtab, EbpfSection* strtab)
{
    ebpf_buffer_u8(&strtab->data, 0);
    ebpf_elf_symbol(&symtab->data, 0, EBPF_STB_LOCAL, EBPF_STT_NOTYPE, 0, 0, 0);
    u32 elf_symbol_index = 1;
    for (u32 section_index = 0; section_index < context->section_count; section_index += 1)
    {
        EbpfSection* section = context->sections + section_index;
        ebpf_elf_symbol(&symtab->data, 0, EBPF_STB_LOCAL, EBPF_STT_SECTION, (u16)section->elf_index, 0, 0);
        elf_symbol_index += 1;
    }
    for (u32 pass = 0; pass < 2; pass += 1)
    {
        u8 binding = pass == 0 ? EBPF_STB_LOCAL : EBPF_STB_GLOBAL;
        if (pass == 1)
        {
            symtab->info = elf_symbol_index;
        }
        for (u32 index = 0; index < context->symbol_count; index += 1)
        {
            EbpfSymbolRecord* symbol = context->symbols + index;
            if (symbol->binding != binding)
            {
                continue;
            }
            u32 name = ebpf_elf_string(&strtab->data, symbol->name);
            if (name == UINT32_MAX)
            {
                ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("eBPF ELF symbol string table exceeds 32 bits"), 0, 0, 0,
                          symbol->symbol ? symbol->symbol->id : IR_SYMBOL_ID_INVALID);
                return false;
            }
            u16 section = symbol->defined && symbol->section ? (u16)symbol->section->elf_index : 0;
            if (!ebpf_elf_symbol(&symtab->data, name, symbol->binding, symbol->type, section, symbol->value, symbol->size))
            {
                ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("failed to encode eBPF ELF symbol"), 0, 0, 0,
                          symbol->symbol ? symbol->symbol->id : IR_SYMBOL_ID_INVALID);
                return false;
            }
            symbol->elf_index = elf_symbol_index++;
        }
    }
    if (!symtab->info)
    {
        symtab->info = elf_symbol_index;
    }
    symtab->logical_size = symtab->data.length;
    strtab->logical_size = strtab->data.length;
    context->stats.symbol_count = elf_symbol_index;
    return true;
}

static bool ebpf_build_relocation_sections(EbpfContext* context)
{
    for (u32 target_index = 0; target_index < context->section_count; target_index += 1)
    {
        EbpfSection* target = context->sections + target_index;
        if (!target->relocation_count)
        {
            continue;
        }
        String8 relocation_name = ebpf_concat_section_name(context, S8(".rel"), target->name);
        EbpfSection* relocation_section = ebpf_section_find(context, relocation_name);
        if (!relocation_section)
        {
            ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("missing eBPF relocation section"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
            return false;
        }
        for (u32 relocation_index = 0; relocation_index < target->relocation_count; relocation_index += 1)
        {
            EbpfRelocation* relocation = target->relocations + relocation_index;
            EbpfSymbolRecord* symbol = ebpf_symbol_by_key(context, relocation->symbol_key);
            if (!symbol || !symbol->elf_index)
            {
                ebpf_fail(context, EBPF_ERROR_UNRESOLVED_SYMBOL, ebpf_s8("eBPF relocation references a missing ELF symbol"), 0, 0, 0,
                          symbol && symbol->symbol ? symbol->symbol->id : IR_SYMBOL_ID_INVALID);
                return false;
            }
            u64 info = ((u64)symbol->elf_index << 32) | relocation->type;
            if (!ebpf_buffer_u64(&relocation_section->data, relocation->offset) ||
                !ebpf_buffer_u64(&relocation_section->data, info))
            {
                ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("failed to encode eBPF relocation"), 0, 0, 0,
                          symbol->symbol ? symbol->symbol->id : IR_SYMBOL_ID_INVALID);
                return false;
            }
        }
        relocation_section->logical_size = relocation_section->data.length;
    }
    return true;
}

static bool ebpf_build_section_name_table(EbpfContext* context, EbpfSection* shstrtab, u32* name_offsets)
{
    ebpf_buffer_u8(&shstrtab->data, 0);
    for (u32 index = 0; index < context->section_count; index += 1)
    {
        u32 offset = ebpf_elf_string(&shstrtab->data, context->sections[index].name);
        if (offset == UINT32_MAX)
        {
            ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("eBPF ELF section-name table exceeds 32 bits"), 0, 0, 0,
                      IR_SYMBOL_ID_INVALID);
            return false;
        }
        name_offsets[index] = offset;
    }
    shstrtab->logical_size = shstrtab->data.length;
    return true;
}

static void ebpf_write_u16(u8* data, u64 offset, u16 value)
{
    data[offset] = (u8)value;
    data[offset + 1] = (u8)(value >> 8);
}

static void ebpf_write_u32(u8* data, u64 offset, u32 value)
{
    for (u32 index = 0; index < 4; index += 1)
    {
        data[offset + index] = (u8)(value >> (index * 8));
    }
}

static void ebpf_write_u64(u8* data, u64 offset, u64 value)
{
    for (u32 index = 0; index < 8; index += 1)
    {
        data[offset + index] = (u8)(value >> (index * 8));
    }
}

static bool ebpf_build_elf(EbpfContext* context, ByteSlice* object)
{
    EbpfSection* symtab = 0;
    EbpfSection* strtab = 0;
    EbpfSection* shstrtab = 0;
    if (!ebpf_prepare_elf_sections(context, &symtab, &strtab, &shstrtab) || !ebpf_build_symbol_table(context, symtab, strtab) ||
        !ebpf_build_relocation_sections(context))
    {
        return false;
    }
    u32* name_offsets = arena_allocate(context->arena, u32, context->section_count ? context->section_count : 1);
    if (!ebpf_build_section_name_table(context, shstrtab, name_offsets))
    {
        return false;
    }

    EbpfBuffer file = {0};
    ebpf_buffer_init(&file, context->arena);
    if (!ebpf_buffer_zeros(&file, 64))
    {
        return false;
    }
    u64* offsets = arena_allocate(context->arena, u64, context->section_count ? context->section_count : 1);
    u64* sizes = arena_allocate(context->arena, u64, context->section_count ? context->section_count : 1);
    for (u32 index = 0; index < context->section_count; index += 1)
    {
        EbpfSection* section = context->sections + index;
        if (!ebpf_buffer_align(&file, section->alignment ? section->alignment : 1))
        {
            ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("eBPF ELF file layout overflow"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
            return false;
        }
        offsets[index] = file.length;
        sizes[index] = section->type == EBPF_SHT_NOBITS ? section->logical_size : section->data.length;
        if (section->type != EBPF_SHT_NOBITS && !ebpf_buffer_bytes(&file, section->data.data, section->data.length))
        {
            ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("eBPF ELF section data overflow"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
            return false;
        }
    }
    if (!ebpf_buffer_align(&file, 8))
    {
        return false;
    }
    u64 section_header_offset = file.length;
    if (!ebpf_buffer_zeros(&file, 64))
    {
        return false;
    }
    for (u32 index = 0; index < context->section_count; index += 1)
    {
        EbpfSection* section = context->sections + index;
        ebpf_buffer_u32(&file, name_offsets[index]);
        ebpf_buffer_u32(&file, section->type);
        ebpf_buffer_u64(&file, section->flags);
        ebpf_buffer_u64(&file, 0);
        ebpf_buffer_u64(&file, offsets[index]);
        ebpf_buffer_u64(&file, sizes[index]);
        ebpf_buffer_u32(&file, section->link);
        ebpf_buffer_u32(&file, section->info);
        ebpf_buffer_u64(&file, section->alignment ? section->alignment : 1);
        ebpf_buffer_u64(&file, section->entry_size);
    }
    if (file.length > ARENA_MAX_RESERVATION)
    {
        ebpf_fail(context, EBPF_ERROR_ENCODING, ebpf_s8("eBPF ELF object exceeds arena limits"), 0, 0, 0, IR_SYMBOL_ID_INVALID);
        return false;
    }
    u8* header = file.data;
    header[0] = 0x7f;
    header[1] = 'E';
    header[2] = 'L';
    header[3] = 'F';
    header[4] = EBPF_ELFCLASS64;
    header[5] = EBPF_ELFDATA2LSB;
    header[6] = EBPF_EV_CURRENT;
    ebpf_write_u16(header, 16, EBPF_ET_REL);
    ebpf_write_u16(header, 18, EBPF_EM_BPF);
    ebpf_write_u32(header, 20, EBPF_EV_CURRENT);
    ebpf_write_u64(header, 24, 0);
    ebpf_write_u64(header, 32, 0);
    ebpf_write_u64(header, 40, section_header_offset);
    ebpf_write_u32(header, 48, 0);
    ebpf_write_u16(header, 52, 64);
    ebpf_write_u16(header, 54, 0);
    ebpf_write_u16(header, 56, 0);
    ebpf_write_u16(header, 58, 64);
    ebpf_write_u16(header, 60, (u16)(context->section_count + 1));
    ebpf_write_u16(header, 62, (u16)shstrtab->elf_index);
    *object = (ByteSlice){.pointer = file.data, .length = file.length};
    context->stats.section_count = context->section_count + 1;
    context->stats.object_bytes = file.length;
    return true;
}

String8 ebpf_error_code_name(EbpfErrorCode code)
{
    switch (code)
    {
    case EBPF_ERROR_NONE: return S8("none");
    case EBPF_ERROR_INVALID_ARGUMENT: return S8("invalid argument");
    case EBPF_ERROR_IR_VALIDATION: return S8("IR validation");
    case EBPF_ERROR_UNSUPPORTED_TYPE: return S8("unsupported type");
    case EBPF_ERROR_UNSUPPORTED_AGGREGATE: return S8("unsupported aggregate");
    case EBPF_ERROR_UNSUPPORTED_INSTRUCTION: return S8("unsupported instruction");
    case EBPF_ERROR_UNSUPPORTED_ABI: return S8("unsupported ABI");
    case EBPF_ERROR_VARIADIC: return S8("variadic operation");
    case EBPF_ERROR_INDIRECT_CALL: return S8("indirect call");
    case EBPF_ERROR_ATOMIC: return S8("atomic operation");
    case EBPF_ERROR_SIMD: return S8("SIMD operation");
    case EBPF_ERROR_INLINE_ASSEMBLY: return S8("inline assembly");
    case EBPF_ERROR_TLS: return S8("thread-local storage");
    case EBPF_ERROR_STACK_LIMIT: return S8("stack limit");
    case EBPF_ERROR_JUMP_RANGE: return S8("jump range");
    case EBPF_ERROR_UNRESOLVED_SYMBOL: return S8("unresolved symbol");
    case EBPF_ERROR_DUPLICATE_SYMBOL: return S8("duplicate symbol");
    case EBPF_ERROR_BTF: return S8("BTF encoding");
    case EBPF_ERROR_ENCODING: return S8("object encoding");
    case EBPF_ERROR_COUNT: return S8("invalid error code");
    }
    return S8("invalid error code");
}

bool ebpf_artifact_is_valid(EbpfArtifact artifact)
{
    return artifact.success && artifact.error.code == EBPF_ERROR_NONE && artifact.bytes.pointer && artifact.bytes.length &&
           artifact.object.pointer == artifact.bytes.pointer && artifact.object.length == artifact.bytes.length &&
           artifact.elf.pointer == artifact.bytes.pointer && artifact.elf.length == artifact.bytes.length;
}

EbpfArtifact ebpf_emit_with_options(Arena* arena, IrProgram* program, IrModule* modules, u32 module_count, EbpfOptions options)
{
    EbpfArtifact artifact = {0};
    artifact.error.function = IR_FUNCTION_ID_INVALID;
    artifact.error.block = IR_BLOCK_ID_INVALID;
    artifact.error.instruction = IR_INSTRUCTION_ID_INVALID;
    artifact.error.symbol = IR_SYMBOL_ID_INVALID;
    artifact.error.opcode = IR_OPCODE_COUNT;
    if (!arena || !program)
    {
        artifact.error.code = EBPF_ERROR_INVALID_ARGUMENT;
        artifact.error.message = S8("eBPF emitter requires an arena and canonical IR program");
        artifact.error.diagnostic = artifact.error.message;
        return artifact;
    }
    if (!modules)
    {
        modules = program->modules;
        module_count = program->module_count;
    }
    if ((module_count && !modules) || (!module_count && program->module_count))
    {
        artifact.error.code = EBPF_ERROR_INVALID_ARGUMENT;
        artifact.error.message = S8("eBPF emitter received an invalid module slice");
        artifact.error.diagnostic = artifact.error.message;
        return artifact;
    }
    EbpfContext context = {.arena = arena, .program = program, .modules = modules, .module_count = module_count, .options = options};
    context.error.function = IR_FUNCTION_ID_INVALID;
    context.error.block = IR_BLOCK_ID_INVALID;
    context.error.instruction = IR_INSTRUCTION_ID_INVALID;
    context.error.symbol = IR_SYMBOL_ID_INVALID;
    context.error.opcode = IR_OPCODE_COUNT;
    context.stats.little_endian = true;
    context.stats.deterministic = options.deterministic;
    context.stats.module_count = module_count;

    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        IrValidationResult validation = ir_validate_canonical_module(program, modules + module_index);
        if (validation.error != IR_VALIDATION_NONE)
        {
            context.error.code = EBPF_ERROR_IR_VALIDATION;
            context.error.message = S8("canonical IR validation failed before eBPF emission");
            context.error.diagnostic = context.error.message;
            context.error.function = validation.function;
            context.error.block = validation.block;
            context.error.instruction = validation.instruction;
            break;
        }
    }

    ByteSlice object = {0};
    if (!ebpf_failed(&context) && ebpf_reserve_sections(&context) && ebpf_initialize_symbols(&context) && ebpf_collect_functions(&context) &&
        ebpf_collect_globals(&context) && ebpf_collect_strings(&context) && ebpf_build_btf(&context) && ebpf_emit_functions(&context) &&
        ebpf_build_elf(&context, &object))
    {
        artifact.bytes = object;
        artifact.object = object;
        artifact.elf = object;
        artifact.success = true;
    }
    artifact.error = context.error;
    artifact.stats = context.stats;
    return artifact;
}

EbpfArtifact ebpf_emit(Arena* arena, IrProgram* program, IrModule* modules, u32 module_count)
{
    return ebpf_emit_with_options(arena, program, modules, module_count, EBPF_OPTIONS_DEFAULT);
}

EbpfArtifact ebpf_emit_program(Arena* arena, IrProgram* program)
{
    return ebpf_emit(arena, program, program ? program->modules : 0, program ? program->module_count : 0);
}
