// Direct SPIR-V compute emitter. spirv_emit owns canonical validation and the
// deliberately narrow kernel ABI; spirv_check_rows proves buffer/index origin;
// spirv_write_module emits the binary and its runtime array-length guard.
// The numeric contracts are SPIR-V 1.5 under Vulkan 1.2; see
// docs/spirv-compute.md for pinned specification/tool provenance and limits.

#include <buster/lib/compiler/spirv/spirv.h>

enum
{
    SPIRV_COMPUTE_MAX_ROWS = 65536,
    SPIRV_WORDS_PER_ROW = 8,
    SPIRV_FIXED_WORD_CAPACITY = 192,
    SPIRV_ID_VOID = 1,
    SPIRV_ID_BOOL,
    SPIRV_ID_UINT,
    SPIRV_ID_UINT3,
    SPIRV_ID_ARRAY,
    SPIRV_ID_BUFFER_TYPE,
    SPIRV_ID_BUFFER_POINTER,
    SPIRV_ID_ELEMENT_POINTER,
    SPIRV_ID_INPUT_POINTER,
    SPIRV_ID_FUNCTION_TYPE,
    SPIRV_ID_BUFFER,
    SPIRV_ID_INVOCATION,
    SPIRV_ID_ZERO,
    SPIRV_ID_FUNCTION,
    SPIRV_ID_ENTRY,
    SPIRV_ID_BODY,
    SPIRV_ID_MERGE,
    SPIRV_ID_INVOCATION_VECTOR,
    SPIRV_ID_INDEX,
    SPIRV_ID_LENGTH,
    SPIRV_ID_IN_BOUNDS,
    SPIRV_ID_FIRST_DYNAMIC,
};

enum
{
    SPIRV_ORIGIN_NONE,
    SPIRV_ORIGIN_BUFFER,
    SPIRV_ORIGIN_INDEX,
    SPIRV_ORIGIN_ELEMENT,
    SPIRV_ORIGIN_UINT,
};

typedef struct SpirvEmitter SpirvEmitter;
struct SpirvEmitter
{
    Arena* arena;
    IrProgram* program;
    IrFunction* function;
    SpirvArtifact artifact;
    u32* ids;
    u8* origins;
    u32* words;
    u32 word_count;
    u32 next_id;
};

BUSTER_GLOBAL_LOCAL void spirv_fail(SpirvEmitter* emitter, String8 diagnostic, IrInstructionId instruction)
{
    if (!emitter->artifact.diagnostic.length)
    {
        emitter->artifact.diagnostic = diagnostic;
        emitter->artifact.instruction = instruction;
    }
}

BUSTER_GLOBAL_LOCAL bool spirv_uint_type(IrType* type)
{
    return type && type->kind == IR_TYPE_INTEGER && type->bit_width == 32 && !type->is_signed && !type->is_atomic &&
           !type->is_volatile && type->layout.resolved && type->layout.size == 4 && type->layout.alignment == 4;
}

BUSTER_GLOBAL_LOCAL bool spirv_buffer_type(SpirvEmitter* emitter, IrType* type)
{
    return type && type->kind == IR_TYPE_POINTER && type->bit_width == 32 && type->layout.resolved && type->layout.size == 4 &&
           type->layout.alignment == 4 && !type->is_atomic && !type->is_volatile &&
           spirv_uint_type(ir_type_from_id(&emitter->program->types, type->element_type));
}

BUSTER_GLOBAL_LOCAL u32 spirv_binary_opcode(u8 operation)
{
    u32 opcode;
    switch (operation)
    {
    case IR_BINARY_INTEGER_ADD: opcode = 128; break;
    case IR_BINARY_INTEGER_SUBTRACT: opcode = 130; break;
    case IR_BINARY_INTEGER_MULTIPLY: opcode = 132; break;
    case IR_BINARY_INTEGER_BITWISE_OR: opcode = 197; break;
    case IR_BINARY_INTEGER_BITWISE_XOR: opcode = 198; break;
    case IR_BINARY_INTEGER_BITWISE_AND: opcode = 199; break;
    default: opcode = 0; break;
    }
    return opcode;
}

BUSTER_GLOBAL_LOCAL bool spirv_check_signature(SpirvEmitter* emitter)
{
    IrFunction* function = emitter->function;
    IrSymbol* symbol = ir_symbol_from_id(&emitter->program->symbols, function->symbol);
    IrType* signature = ir_type_from_id(&emitter->program->types, function->canonical_type);
    IrType* result = signature ? ir_type_from_id(&emitter->program->types, signature->return_type) : 0;
    bool valid = symbol && !symbol->is_link_once && signature && signature->kind == IR_TYPE_FUNCTION &&
                 signature->calling_convention == IR_CALLING_CONVENTION_C && signature->parameter_count == 2 &&
                 !signature->is_variadic && !signature->is_noreturn && !signature->is_unprototyped &&
                 result && result->kind == IR_TYPE_VOID && function->name.length == 6 &&
                 memcmp(function->name.pointer, "kernel", 6) == 0;
    if (symbol && symbol->is_link_once)
    {
        spirv_fail(emitter, S8("SPIR-V compute does not support link-once external kernel definitions"),
                   IR_INSTRUCTION_ID_INVALID);
    }
    if (valid)
    {
        valid = spirv_buffer_type(emitter, ir_type_from_id(&emitter->program->types, signature->parameter_types[0])) &&
                spirv_uint_type(ir_type_from_id(&emitter->program->types, signature->parameter_types[1]));
    }
    bool plain_symbol = symbol && symbol->kind == IR_SYMBOL_FUNCTION && symbol->is_definition && !symbol->section_name.length &&
                        !symbol->is_weak && !symbol->is_hidden && !symbol->is_thread_local &&
                        (!symbol->link_name.length || (symbol->link_name.length == 6 && memcmp(symbol->link_name.pointer, "kernel", 6) == 0));
    if (valid && !plain_symbol)
    {
        valid = false;
        spirv_fail(emitter, S8("SPIR-V compute does not support kernel section, linkage attributes, TLS or symbol-name remapping"),
                   IR_INSTRUCTION_ID_INVALID);
    }
    if (!valid)
    {
        spirv_fail(emitter, S8("SPIR-V compute requires void kernel(unsigned *buffer, unsigned index) with 32-bit unsigned integers and pointers"),
                   IR_INSTRUCTION_ID_INVALID);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool spirv_check_rows(SpirvEmitter* emitter)
{
    IrFunction* function = emitter->function;
    bool valid = function->block_count == 1 && function->entry.value == 0 && function->published_cfg &&
                 !function->published_cfg->parameter_count && function->instruction_count <= SPIRV_COMPUTE_MAX_ROWS &&
                 function->value_count <= SPIRV_COMPUTE_MAX_ROWS;
    if (!valid)
    {
        spirv_fail(emitter, S8("SPIR-V compute supports one straight-line source block without block parameters and at most 65536 rows/values"),
                   IR_INSTRUCTION_ID_INVALID);
    }
    if (valid)
    {
        emitter->origins = arena_allocate_zeroed(emitter->arena, u8, function->value_count);
        emitter->ids = arena_allocate_zeroed(emitter->arena, u32, function->value_count);
        u32 argument_counts[2] = {0};
        for (u32 index = 0; valid && index < function->instruction_count; index += 1)
        {
            IrInstruction* row = function->instructions + index;
            if (row->opcode == IR_OPCODE_ARGUMENT)
            {
                valid = row->immediate_count == 1 && row->immediates && row->immediates[0] < BUSTER_ARRAY_LENGTH(argument_counts) &&
                        row->result.value < function->value_count && function->values[row->result.value].category == IR_VALUE_VALUE;
                if (valid)
                {
                    u32 argument = (u32)row->immediates[0];
                    argument_counts[argument] += 1;
                    emitter->origins[row->result.value] = (u8)(argument ? SPIRV_ORIGIN_INDEX : SPIRV_ORIGIN_BUFFER);
                    emitter->ids[row->result.value] = argument ? SPIRV_ID_INDEX : SPIRV_ID_BUFFER;
                }
                else
                {
                    spirv_fail(emitter, S8("SPIR-V compute received an invalid canonical argument row"), (IrInstructionId){.value = index});
                }
            }
        }
        valid = valid && argument_counts[0] == 1 && argument_counts[1] == 1;
        if (!valid)
        {
            spirv_fail(emitter, S8("SPIR-V compute requires exactly one canonical ARGUMENT for each kernel parameter"), IR_INSTRUCTION_ID_INVALID);
        }
        u32 stores = 0;
        for (u32 index = 0; valid && index < function->instruction_count; index += 1)
        {
            IrInstruction* row = function->instructions + index;
            IrInstructionId instruction = {.value = index};
            IrType* type = ir_type_from_id(&emitter->program->types, row->canonical_type);
            IrValue* value = row->result.value < function->value_count ? function->values + row->result.value : 0;
            valid = !row->volatile_access && (!value || (!value->is_volatile && !value->is_read_only));
            if (!valid)
            {
                spirv_fail(emitter, S8("SPIR-V compute does not support volatile or qualified storage accesses"), instruction);
            }
            else
            {
                u8 first = row->operand_count ? emitter->origins[row->operands[0].value] : SPIRV_ORIGIN_NONE;
                u8 second = row->operand_count > 1 ? emitter->origins[row->operands[1].value] : SPIRV_ORIGIN_NONE;
                bool first_uint = first == SPIRV_ORIGIN_UINT || first == SPIRV_ORIGIN_INDEX;
                bool second_uint = second == SPIRV_ORIGIN_UINT || second == SPIRV_ORIGIN_INDEX;
                switch (row->opcode)
                {
                case IR_OPCODE_ARGUMENT: break;
                case IR_OPCODE_CONSTANT_INTEGER:
                    valid = spirv_uint_type(type) && value && value->category == IR_VALUE_VALUE;
                    if (valid) emitter->origins[row->result.value] = SPIRV_ORIGIN_UINT;
                    break;
                case IR_OPCODE_INDEX:
                    valid = first == SPIRV_ORIGIN_BUFFER && second == SPIRV_ORIGIN_INDEX && spirv_uint_type(type) &&
                            value && value->category == IR_VALUE_PLACE;
                    if (valid) emitter->origins[row->result.value] = SPIRV_ORIGIN_ELEMENT;
                    break;
                case IR_OPCODE_LOAD:
                    valid = first == SPIRV_ORIGIN_ELEMENT && spirv_uint_type(type) && value && value->category == IR_VALUE_VALUE;
                    if (valid) emitter->origins[row->result.value] = SPIRV_ORIGIN_UINT;
                    break;
                case IR_OPCODE_STORE:
                    valid = first == SPIRV_ORIGIN_ELEMENT && second_uint;
                    if (valid) stores += 1;
                    break;
                case IR_OPCODE_BINARY:
                    valid = first_uint && second_uint && spirv_uint_type(type) && spirv_binary_opcode(row->binary_operation) &&
                            value && value->category == IR_VALUE_VALUE;
                    if (valid) emitter->origins[row->result.value] = SPIRV_ORIGIN_UINT;
                    break;
                case IR_OPCODE_CAST:
                    valid = value && value->category == IR_VALUE_VALUE &&
                            ((first_uint && spirv_uint_type(type) &&
                              (row->conversion_operation == IR_CONVERSION_IDENTITY || row->conversion_operation == IR_CONVERSION_INTEGER_REINTERPRET)) ||
                             (first == SPIRV_ORIGIN_BUFFER && spirv_buffer_type(emitter, type) &&
                              (row->conversion_operation == IR_CONVERSION_IDENTITY || row->conversion_operation == IR_CONVERSION_POINTER_REINTERPRET)));
                    if (valid) emitter->origins[row->result.value] = first;
                    break;
                case IR_OPCODE_RETURN:
                    valid = index + 1 == function->instruction_count && row->operand_count == 0;
                    break;
                default:
                    valid = false;
                    break;
                }
                if (!valid)
                {
                    spirv_fail(emitter, S8("SPIR-V compute rejects this instruction/type or buffer/index provenance; calls, source control flow, arbitrary pointers, locals, atomics, barriers and floating point are unsupported"), instruction);
                }
            }
        }
        if (valid && !stores)
        {
            valid = false;
            spirv_fail(emitter, S8("SPIR-V compute requires at least one store to buffer[index]"), IR_INSTRUCTION_ID_INVALID);
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL void spirv_word(SpirvEmitter* emitter, u32 word)
{
    emitter->words[emitter->word_count++] = word;
}

#define BUSTER_SPIRV_WRITE(emitter, opcode, ...)                                                                                              \
    do                                                                                                                               \
    {                                                                                                                                \
        u32 spirv_operands[] = {__VA_ARGS__};                                                                                          \
        u32 spirv_operand_count = (u32)BUSTER_ARRAY_LENGTH(spirv_operands);                                                             \
        spirv_word(emitter, ((spirv_operand_count + 1) << 16) | (opcode));                                                             \
        for (u32 spirv_operand_index = 0; spirv_operand_index < spirv_operand_count; spirv_operand_index += 1)                         \
        {                                                                                                                            \
            spirv_word(emitter, spirv_operands[spirv_operand_index]);                                                                 \
        }                                                                                                                            \
    } while (0)

BUSTER_GLOBAL_LOCAL void spirv_write_module(SpirvEmitter* emitter)
{
    IrFunction* function = emitter->function;
    u64 capacity = SPIRV_FIXED_WORD_CAPACITY + (u64)SPIRV_WORDS_PER_ROW * function->instruction_count;
    emitter->words = arena_allocate(emitter->arena, u32, capacity);
    emitter->next_id = SPIRV_ID_FIRST_DYNAMIC;
    spirv_word(emitter, UINT32_C(0x07230203));
    spirv_word(emitter, UINT32_C(0x00010500));
    spirv_word(emitter, 0); // Unregistered generator; no external compiler.
    spirv_word(emitter, 0); // Patched to the exclusive ID bound below.
    spirv_word(emitter, 0);
    BUSTER_SPIRV_WRITE(emitter, 17, 1); // OpCapability Shader.
    BUSTER_SPIRV_WRITE(emitter, 14, 0, 1); // OpMemoryModel Logical GLSL450.
    // Literal "kernel" occupies two little-endian words including NUL.
    BUSTER_SPIRV_WRITE(emitter, 15, 5, SPIRV_ID_FUNCTION, UINT32_C(0x6e72656b), UINT32_C(0x00006c65), SPIRV_ID_BUFFER, SPIRV_ID_INVOCATION);
    BUSTER_SPIRV_WRITE(emitter, 16, SPIRV_ID_FUNCTION, 17, 1, 1, 1); // LocalSize.
    BUSTER_SPIRV_WRITE(emitter, 71, SPIRV_ID_ARRAY, 6, 4); // ArrayStride.
    BUSTER_SPIRV_WRITE(emitter, 71, SPIRV_ID_BUFFER_TYPE, 2); // Block.
    BUSTER_SPIRV_WRITE(emitter, 72, SPIRV_ID_BUFFER_TYPE, 0, 35, 0); // Member Offset.
    BUSTER_SPIRV_WRITE(emitter, 71, SPIRV_ID_BUFFER, 34, 0); // DescriptorSet.
    BUSTER_SPIRV_WRITE(emitter, 71, SPIRV_ID_BUFFER, 33, 0); // Binding.
    BUSTER_SPIRV_WRITE(emitter, 71, SPIRV_ID_INVOCATION, 11, 28); // GlobalInvocationId.
    BUSTER_SPIRV_WRITE(emitter, 19, SPIRV_ID_VOID);
    BUSTER_SPIRV_WRITE(emitter, 20, SPIRV_ID_BOOL);
    BUSTER_SPIRV_WRITE(emitter, 21, SPIRV_ID_UINT, 32, 0);
    BUSTER_SPIRV_WRITE(emitter, 23, SPIRV_ID_UINT3, SPIRV_ID_UINT, 3);
    BUSTER_SPIRV_WRITE(emitter, 29, SPIRV_ID_ARRAY, SPIRV_ID_UINT);
    BUSTER_SPIRV_WRITE(emitter, 30, SPIRV_ID_BUFFER_TYPE, SPIRV_ID_ARRAY);
    BUSTER_SPIRV_WRITE(emitter, 32, SPIRV_ID_BUFFER_POINTER, 12, SPIRV_ID_BUFFER_TYPE);
    BUSTER_SPIRV_WRITE(emitter, 32, SPIRV_ID_ELEMENT_POINTER, 12, SPIRV_ID_UINT);
    BUSTER_SPIRV_WRITE(emitter, 32, SPIRV_ID_INPUT_POINTER, 1, SPIRV_ID_UINT3);
    BUSTER_SPIRV_WRITE(emitter, 33, SPIRV_ID_FUNCTION_TYPE, SPIRV_ID_VOID);
    BUSTER_SPIRV_WRITE(emitter, 43, SPIRV_ID_UINT, SPIRV_ID_ZERO, 0);
    BUSTER_SPIRV_WRITE(emitter, 59, SPIRV_ID_BUFFER_POINTER, SPIRV_ID_BUFFER, 12);
    BUSTER_SPIRV_WRITE(emitter, 59, SPIRV_ID_INPUT_POINTER, SPIRV_ID_INVOCATION, 1);
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction* row = function->instructions + index;
        if (row->opcode == IR_OPCODE_CONSTANT_INTEGER)
        {
            u32 literal = (u32)row->immediates[0];
            if (row->immediate_is_negative) literal = 0u - literal;
            u32 id = emitter->next_id++;
            emitter->ids[row->result.value] = id;
            BUSTER_SPIRV_WRITE(emitter, 43, SPIRV_ID_UINT, id, literal);
        }
    }
    BUSTER_SPIRV_WRITE(emitter, 54, SPIRV_ID_VOID, SPIRV_ID_FUNCTION, 0, SPIRV_ID_FUNCTION_TYPE);
    BUSTER_SPIRV_WRITE(emitter, 248, SPIRV_ID_ENTRY);
    BUSTER_SPIRV_WRITE(emitter, 61, SPIRV_ID_UINT3, SPIRV_ID_INVOCATION_VECTOR, SPIRV_ID_INVOCATION);
    BUSTER_SPIRV_WRITE(emitter, 81, SPIRV_ID_UINT, SPIRV_ID_INDEX, SPIRV_ID_INVOCATION_VECTOR, 0);
    BUSTER_SPIRV_WRITE(emitter, 68, SPIRV_ID_UINT, SPIRV_ID_LENGTH, SPIRV_ID_BUFFER, 0);
    BUSTER_SPIRV_WRITE(emitter, 176, SPIRV_ID_BOOL, SPIRV_ID_IN_BOUNDS, SPIRV_ID_INDEX, SPIRV_ID_LENGTH);
    BUSTER_SPIRV_WRITE(emitter, 247, SPIRV_ID_MERGE, 0);
    BUSTER_SPIRV_WRITE(emitter, 250, SPIRV_ID_IN_BOUNDS, SPIRV_ID_BODY, SPIRV_ID_MERGE);
    BUSTER_SPIRV_WRITE(emitter, 248, SPIRV_ID_BODY);
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction* row = function->instructions + index;
        u32 first = row->operand_count ? emitter->ids[row->operands[0].value] : 0;
        u32 second = row->operand_count > 1 ? emitter->ids[row->operands[1].value] : 0;
        u32 id = 0;
        switch (row->opcode)
        {
        case IR_OPCODE_INDEX:
            id = emitter->next_id++;
            BUSTER_SPIRV_WRITE(emitter, 65, SPIRV_ID_ELEMENT_POINTER, id, SPIRV_ID_BUFFER, SPIRV_ID_ZERO, second);
            break;
        case IR_OPCODE_LOAD:
            id = emitter->next_id++;
            BUSTER_SPIRV_WRITE(emitter, 61, SPIRV_ID_UINT, id, first);
            break;
        case IR_OPCODE_STORE:
            BUSTER_SPIRV_WRITE(emitter, 62, first, second);
            break;
        case IR_OPCODE_BINARY:
            id = emitter->next_id++;
            BUSTER_SPIRV_WRITE(emitter, spirv_binary_opcode(row->binary_operation), SPIRV_ID_UINT, id, first, second);
            break;
        case IR_OPCODE_CAST:
            id = first; // Representation-preserving copy; retains origin.
            break;
        default:
            break; // ARGUMENT/constants mapped above; source RETURN below.
        }
        if (id) emitter->ids[row->result.value] = id;
    }
    BUSTER_SPIRV_WRITE(emitter, 249, SPIRV_ID_MERGE);
    BUSTER_SPIRV_WRITE(emitter, 248, SPIRV_ID_MERGE);
    spirv_word(emitter, (1u << 16) | 253u); // OpReturn.
    spirv_word(emitter, (1u << 16) | 56u); // OpFunctionEnd.
    emitter->words[3] = emitter->next_id;
    u8* bytes = arena_allocate(emitter->arena, u8, (u64)emitter->word_count * 4);
    for (u32 index = 0; index < emitter->word_count; index += 1)
    {
        u32 word = emitter->words[index];
        for (u32 byte = 0; byte < 4; byte += 1) bytes[(u64)index * 4 + byte] = (u8)(word >> (byte * 8));
    }
    emitter->artifact.bytes = (ByteSlice){.pointer = bytes, .length = (u64)emitter->word_count * 4};
    emitter->artifact.success = true;
}

#undef BUSTER_SPIRV_WRITE

SpirvArtifact spirv_emit(Arena* arena, IrProgram* program, IrModule* module)
{
    SpirvEmitter emitter = {.arena = arena, .program = program};
    emitter.artifact.function = IR_FUNCTION_ID_INVALID;
    emitter.artifact.instruction = IR_INSTRUCTION_ID_INVALID;
    if (!arena || !program || !program->arena || !module)
    {
        spirv_fail(&emitter, S8("SPIR-V emission requires an arena, canonical program and module"), IR_INSTRUCTION_ID_INVALID);
    }
    else
    {
        IrValidationResult validation = ir_prepare_canonical_module(program, module, false);
        if (validation.error != IR_VALIDATION_NONE)
        {
            emitter.artifact.function = validation.function;
            spirv_fail(&emitter, S8("canonical IR validation failed before SPIR-V emission"), validation.instruction);
        }
        else if (ir_module_has_exception_root(module))
        {
            spirv_fail(&emitter, S8("SPIR-V does not support canonical exception roots"), IR_INSTRUCTION_ID_INVALID);
        }
        else if (module->function_count != 1 || module->functions[0].state != IR_FUNCTION_LOWERED || module->rejected_function_count || module->global_count ||
                 module->assembly_count || module->alias_count || module->initializer_count || program->data_layout.pointer.bit_width != 32)
        {
            spirv_fail(&emitter, S8("SPIR-V compute requires one defined kernel, no globals/assembly/aliases/initializers and the 32-bit compute data layout"),
                       IR_INSTRUCTION_ID_INVALID);
        }
        else
        {
            emitter.function = module->functions;
            emitter.artifact.function = emitter.function->id;
            if (spirv_check_signature(&emitter) && spirv_check_rows(&emitter)) spirv_write_module(&emitter);
        }
    }
    return emitter.artifact;
}
