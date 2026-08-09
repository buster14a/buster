#include <buster/lib/compiler/codegen/codegen_internal.h>

#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/ir/interpreter.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

#define X64_VALUE_SLOT_SIZE 32
#define X64_VALUE_SLOT_COMPONENT_COUNT 4
#define A64_VALUE_SLOT_SIZE 32

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_jump_target(IrFunction* function, IrInstruction* instruction, String8 literal, String8 prefix,
                                                             u32* target_index_out)
{
    return ir_inline_assembly_jump_target(function, instruction, literal, prefix, target_index_out);
}

BUSTER_GLOBAL_LOCAL void codegen_emit_u8(CodegenBuffer* buffer, u8 value);
BUSTER_GLOBAL_LOCAL void codegen_emit_u32(CodegenBuffer* buffer, u32 value);

BUSTER_GLOBAL_LOCAL bool codegen_decimal_number(String8 string, u64* value_out)
{
    if (!string.pointer || !string.length || !value_out)
    {
        return false;
    }
    u64 value = 0;
    for (u64 index = 0; index < string.length; index += 1)
    {
        u8 digit = (u8)string.pointer[index];
        if (digit < '0' || digit > '9' || value > (UINT64_MAX - (digit - '0')) / 10)
        {
            return false;
        }
        value = value * 10 + (digit - '0');
    }
    *value_out = value;
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_clobber_is_rbx(String8 clobber)
{
    return string_equal(clobber, S8("rbx")) || string_equal(clobber, S8("ebx")) || string_equal(clobber, S8("bx")) || string_equal(clobber, S8("bl"));
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_clobber_register(String8 clobber, X64Register* register_out)
{
    if (string_equal(clobber, S8("rax")) || string_equal(clobber, S8("eax")) || string_equal(clobber, S8("ax")) || string_equal(clobber, S8("al")))
    {
        *register_out = X64_REGISTER_RAX;
        return true;
    }
    if (codegen_inline_assembly_clobber_is_rbx(clobber))
    {
        *register_out = X64_REGISTER_RBX;
        return true;
    }
    if (string_equal(clobber, S8("rcx")) || string_equal(clobber, S8("ecx")) || string_equal(clobber, S8("cx")) || string_equal(clobber, S8("cl")))
    {
        *register_out = X64_REGISTER_RCX;
        return true;
    }
    if (string_equal(clobber, S8("rdx")) || string_equal(clobber, S8("edx")) || string_equal(clobber, S8("dx")) || string_equal(clobber, S8("dl")))
    {
        *register_out = X64_REGISTER_RDX;
        return true;
    }
    if (string_equal(clobber, S8("rsi")) || string_equal(clobber, S8("esi")) || string_equal(clobber, S8("si")) || string_equal(clobber, S8("sil")))
    {
        *register_out = X64_REGISTER_RSI;
        return true;
    }
    if (string_equal(clobber, S8("rdi")) || string_equal(clobber, S8("edi")) || string_equal(clobber, S8("di")) || string_equal(clobber, S8("dil")))
    {
        *register_out = X64_REGISTER_RDI;
        return true;
    }
    if (clobber.length >= 2 && clobber.pointer[0] == 'r')
    {
        u64 number = 0;
        String8 suffix = {
            .pointer = clobber.pointer + 1,
            .length = clobber.length - 1,
        };
        if (codegen_decimal_number(suffix, &number) && number >= 8 && number <= 11)
        {
            *register_out = (X64Register)number;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_constraint_register(u64 constraint, X64Register* register_out)
{
    switch (constraint & 0xff)
    {
    case IR_INLINE_ASSEMBLY_CONSTRAINT_A:
        *register_out = X64_REGISTER_RAX;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_B:
        *register_out = X64_REGISTER_RBX;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_C:
        *register_out = X64_REGISTER_RCX;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_D:
        *register_out = X64_REGISTER_RDX;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_R:
    case IR_INLINE_ASSEMBLY_CONSTRAINT_COUNT:
        break;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL u32 codegen_inline_assembly_type_class(IrType* type)
{
    if (!type || !type->layout.resolved || !type->layout.size || type->layout.size > 8)
    {
        return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID;
    }
    switch (type->kind)
    {
    case IR_TYPE_BOOLEAN:
    case IR_TYPE_INTEGER:
    case IR_TYPE_ENUM:
        return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INTEGER;
    case IR_TYPE_POINTER:
        return IR_INLINE_ASSEMBLY_OPERAND_CLASS_POINTER;
    case IR_TYPE_VOID:
    case IR_TYPE_FLOAT:
    case IR_TYPE_VA_LIST:
    case IR_TYPE_SLICE:
    case IR_TYPE_ARRAY:
    case IR_TYPE_VECTOR:
    case IR_TYPE_FUNCTION:
    case IR_TYPE_RANGE:
    case IR_TYPE_STRUCT:
    case IR_TYPE_UNION:
    case IR_TYPE_COUNT:
        break;
    }
    return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_types_compatible(IrType* output, IrType* input)
{
    u32 output_class = codegen_inline_assembly_type_class(output);
    u32 input_class = codegen_inline_assembly_type_class(input);
    return output_class != IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID && output_class == input_class && output->layout.size == input->layout.size;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_constraint_shape_valid(u64 constraint, u32 operand_index, u32 operand_count, u32* match_index_out)
{
    if ((constraint & ~IR_INLINE_ASSEMBLY_CONSTRAINT_KNOWN_MASK) ||
        (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) >= IR_INLINE_ASSEMBLY_CONSTRAINT_COUNT)
    {
        return false;
    }
    bool output = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0;
    bool read_write = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0;
    bool matching = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0;
    if ((read_write && !output) || (matching && (output || read_write)))
    {
        return false;
    }
    if (!matching)
    {
        return (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_MASK) == 0;
    }
    u32 match_index = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
    if (match_index >= operand_index || match_index >= operand_count)
    {
        return false;
    }
    if (match_index_out)
    {
        *match_index_out = match_index;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_function_saves_rbx(IrFunction* function)
{
    if (!function)
    {
        return false;
    }
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->opcode != IR_OPCODE_INLINE_ASSEMBLY)
        {
            continue;
        }
        IrInstructionExtra extra = ir_instruction_extra(function, instruction->id);
        if ((instruction->operand_count && !instruction->immediates) || (extra.clobber_count && !extra.clobbers))
        {
            return false;
        }
        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
        {
            if ((instruction->immediates[operand_index] & 0xff) == IR_INLINE_ASSEMBLY_CONSTRAINT_B)
            {
                return true;
            }
        }
        for (u32 clobber_index = 0; clobber_index < extra.clobber_count; clobber_index += 1)
        {
            if (codegen_inline_assembly_clobber_is_rbx(extra.clobbers[clobber_index]))
            {
                return true;
            }
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_asm_memory_width(u32 width)
{
    return width == 1 || width == 2 || width == 4 || width == 8;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_asm_memory_width(u32 width)
{
    return width == 1 || width == 2 || width == 4 || width == 8;
}

BUSTER_GLOBAL_LOCAL u32 codegen_canonical_a64_nop_count(String8 literal)
{
    u32 count = 0;
    u64 offset = 0;
    while (offset < literal.length)
    {
        while (offset < literal.length && (literal.pointer[offset] == ' ' || literal.pointer[offset] == '\t' || literal.pointer[offset] == '\r' ||
                                            literal.pointer[offset] == '\n'))
        {
            offset += 1;
        }
        while (offset + 1 < literal.length && literal.pointer[offset] == '\\' &&
               (literal.pointer[offset + 1] == 'n' || literal.pointer[offset + 1] == 'r' || literal.pointer[offset + 1] == 't'))
        {
            offset += 2;
            while (offset < literal.length && (literal.pointer[offset] == ' ' || literal.pointer[offset] == '\t' || literal.pointer[offset] == '\r' ||
                                                literal.pointer[offset] == '\n'))
            {
                offset += 1;
            }
        }
        if (offset == literal.length || literal.pointer[offset] == 0)
        {
            break;
        }
        if (offset + 3 > literal.length || literal.pointer[offset] != 'n' || literal.pointer[offset + 1] != 'o' || literal.pointer[offset + 2] != 'p')
        {
            return 0;
        }
        offset += 3;
        if (count == UINT32_MAX)
        {
            return 0;
        }
        count += 1;
    }
    return count;
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_asm_load(CodegenBuffer* buffer, X64Register target, X64Register base, u32 displacement, u32 width)
{
    u8 rex = 0x40;
    rex |= target >= X64_REGISTER_R8 ? 0x04 : 0;
    rex |= base >= X64_REGISTER_R8 ? 0x01 : 0;
    rex |= width == 8 ? 0x08 : 0;
    if (width == 2)
    {
        codegen_emit_u8(buffer, 0x66);
    }
    if (rex)
    {
        codegen_emit_u8(buffer, rex);
    }
    if (width == 1 || width == 2)
    {
        codegen_emit_u8(buffer, 0x0f);
        codegen_emit_u8(buffer, width == 1 ? 0xb6 : 0xb7);
    }
    else
    {
        codegen_emit_u8(buffer, 0x8b);
    }
    codegen_emit_u8(buffer, (u8)(0x80 | ((target & 7) << 3) | (base & 7)));
    codegen_emit_u32(buffer, displacement);
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_asm_store(CodegenBuffer* buffer, X64Register base, X64Register source, u32 displacement, u32 width)
{
    if (width == 2)
    {
        codegen_emit_u8(buffer, 0x66);
    }
    u8 rex = 0x40;
    rex |= source >= X64_REGISTER_R8 ? 0x04 : 0;
    rex |= base >= X64_REGISTER_R8 ? 0x01 : 0;
    rex |= width == 8 ? 0x08 : 0;
    if (width == 1 && source >= X64_REGISTER_RSP && source <= X64_REGISTER_RDI)
    {
        rex |= 0x40;
    }
    if (rex)
    {
        codegen_emit_u8(buffer, rex);
    }
    codegen_emit_u8(buffer, width == 1 ? 0x88 : 0x89);
    codegen_emit_u8(buffer, (u8)(0x80 | ((source & 7) << 3) | (base & 7)));
    codegen_emit_u32(buffer, displacement);
}

BUSTER_GLOBAL_LOCAL u32 codegen_align_u32(u32 value, u32 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

BUSTER_GLOBAL_LOCAL bool codegen_unwind_action_append(CodegenFunctionDescriptor* descriptor, u32 capacity, u32 code_offset,
                                                      CodegenUnwindActionKind kind, u8 register_index, u32 value)
{
    if (descriptor->unwind_action_count >= capacity)
    {
        return false;
    }
    descriptor->unwind_actions[descriptor->unwind_action_count++] = (CodegenUnwindAction){
        .code_offset = code_offset,
        .value = value,
        .kind = kind,
        .register_index = register_index,
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_epilog_offset_append(CodegenFunctionDescriptor* descriptor, u32 capacity, u32 code_offset)
{
    if (!descriptor || !descriptor->epilog_offsets || descriptor->epilog_count >= capacity)
    {
        return false;
    }
    descriptor->epilog_offsets[descriptor->epilog_count++] = code_offset;
    return true;
}

String8 codegen_register_allocator_mode_string(CodegenRegisterAllocatorMode mode)
{
    switch (mode)
    {
        break;
    case CODEGEN_REGISTER_ALLOCATOR_NONE:
        return S8("none");
        break;
    case CODEGEN_REGISTER_ALLOCATOR_MIR_STACK:
        return S8("mir-stack");
        break;
    case CODEGEN_REGISTER_ALLOCATOR_FAST:
        return S8("fast");
        break;
    case CODEGEN_REGISTER_ALLOCATOR_QUALITY:
        return S8("quality");
        break;
    case CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT:
        break;
    }
    return S8("invalid");
}

CodegenAbi codegen_abi_for_target(Target target)
{
    switch (target.cpu_arch)
    {
        break;
    case CPU_ARCH_X86_64:
    {
        switch (target.os)
        {
            break;
        case OPERATING_SYSTEM_WINDOWS:
            return CODEGEN_ABI_X86_64_WINDOWS;
            break;
        case OPERATING_SYSTEM_UEFI:
            return CODEGEN_ABI_X86_64_WINDOWS;
            break;
        case OPERATING_SYSTEM_LINUX:
            return CODEGEN_ABI_X86_64_SYSTEM_V;
            break;
        case OPERATING_SYSTEM_MACOS:
            return CODEGEN_ABI_X86_64_SYSTEM_V;
            break;
        case OPERATING_SYSTEM_ANDROID:
            return CODEGEN_ABI_X86_64_SYSTEM_V;
            break;
        case OPERATING_SYSTEM_IOS:
            return CODEGEN_ABI_X86_64_SYSTEM_V;
            break;
        case OPERATING_SYSTEM_FREESTANDING:
            return CODEGEN_ABI_X86_64_SYSTEM_V;
            break;
        case OPERATING_SYSTEM_COUNT:
            return CODEGEN_ABI_COUNT;
        }
    }
    break;
        break;
    case CPU_ARCH_AARCH64:
    {
        switch (target.os)
        {
            break;
        case OPERATING_SYSTEM_WINDOWS:
            return CODEGEN_ABI_AARCH64_WINDOWS;
            break;
        case OPERATING_SYSTEM_MACOS:
            return CODEGEN_ABI_AARCH64_DARWIN;
            break;
        case OPERATING_SYSTEM_IOS:
            return CODEGEN_ABI_AARCH64_DARWIN;
            break;
        case OPERATING_SYSTEM_LINUX:
            return CODEGEN_ABI_AARCH64_AAPCS64;
            break;
        case OPERATING_SYSTEM_UEFI:
            return CODEGEN_ABI_AARCH64_AAPCS64;
            break;
        case OPERATING_SYSTEM_ANDROID:
            return CODEGEN_ABI_AARCH64_AAPCS64;
            break;
        case OPERATING_SYSTEM_FREESTANDING:
            return CODEGEN_ABI_AARCH64_AAPCS64;
            break;
        case OPERATING_SYSTEM_COUNT:
            return CODEGEN_ABI_COUNT;
        }
    }
    break;
        break;
    case CPU_ARCH_COUNT:
        return CODEGEN_ABI_COUNT;
    }
    return CODEGEN_ABI_COUNT;
}

BUSTER_GLOBAL_LOCAL AnalysisAbiConvention codegen_analysis_abi_convention(CodegenAbi abi)
{
    return abi == CODEGEN_ABI_X86_64_SYSTEM_V   ? ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64
           : abi == CODEGEN_ABI_X86_64_WINDOWS  ? ANALYSIS_ABI_CONVENTION_WIN64_X86_64
           : abi == CODEGEN_ABI_AARCH64_DARWIN  ? ANALYSIS_ABI_CONVENTION_APPLE_AARCH64
           : abi == CODEGEN_ABI_AARCH64_WINDOWS ? ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64
                                                : ANALYSIS_ABI_CONVENTION_AAPCS64;
}

BUSTER_GLOBAL_LOCAL AnalysisType* codegen_analysis_type_from_id_checked(AnalysisResult* analysis, AnalysisTypeId id)
{
    if (!analysis || !analysis->types.types || id.value >= analysis->types.count)
    {
        return 0;
    }
    return analysis->types.types + id.value;
}

BUSTER_GLOBAL_LOCAL bool codegen_win64_relayout_indirect_copies(AnalysisResult* analysis, AnalysisType* function_type, AnalysisTypeId* argument_types,
                                                                 u32 argument_count, CodegenAbiSignature* signature)
{
    if (!argument_types)
    {
        return true;
    }
    if (argument_count && !signature->arguments)
    {
        return false;
    }
    u64 copy_cursor = 32;
    for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
    {
        CodegenAbiLocation* location = signature->arguments + argument_index;
        for (u32 part_index = 0; part_index < location->part_count; part_index += 1)
        {
            CodegenAbiPart* part = location->parts + part_index;
            if (part->kind == CODEGEN_ABI_LOCATION_STACK)
            {
                u64 part_end = (u64)part->stack_offset + part->size;
                if (part_end > UINT32_MAX)
                {
                    return false;
                }
                copy_cursor = BUSTER_MAX(copy_cursor, part_end);
            }
        }
    }
    for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
    {
        CodegenAbiLocation* location = signature->arguments + argument_index;
        if (!location->indirect)
        {
            continue;
        }
        AnalysisTypeId type_id = argument_types ? argument_types[argument_index] : function_type->as.function.argument_types[argument_index];
        AnalysisType* type = codegen_analysis_type_from_id_checked(analysis, type_id);
        if (!type || type->layout.alignment > 16 || !type->layout.size || type->layout.size > UINT32_MAX)
        {
            return false;
        }
        copy_cursor = (copy_cursor + 15) & ~(u64)15;
        u64 copy_size = (type->layout.size + 7) & ~(u64)7;
        if (copy_cursor > UINT32_MAX || copy_size > UINT32_MAX - copy_cursor)
        {
            return false;
        }
        location->indirect_copy_offset = (u32)copy_cursor;
        copy_cursor += copy_size;
    }
    copy_cursor = (copy_cursor + 15) & ~(u64)15;
    if (copy_cursor > UINT32_MAX)
    {
        return false;
    }
    signature->stack_size = (u32)copy_cursor;
    return true;
}

BUSTER_GLOBAL_LOCAL CodegenAbiSignature codegen_classify_signature_with_arguments_prepared(Arena* arena, AnalysisResult* analysis,
                                                                                            AnalysisTypeId function_type_id,
                                                                                            AnalysisTypeId* argument_types, u32 argument_count,
                                                                                            Target target, bool prepare_layouts)
{
    CodegenAbiSignature result = {0};
    CodegenAbi abi = codegen_abi_for_target(target);
    AnalysisType* function_type = analysis_type_from_id(analysis, function_type_id);
    if (function_type->kind != ANALYSIS_TYPE_FUNCTION || abi >= CODEGEN_ABI_COUNT)
    {
        return result;
    }
    if (prepare_layouts)
    {
        analysis_compute_layouts(analysis, (AnalysisLayoutOptions){
                                               .pointer_size = 8,
                                               .pointer_alignment = 8,
                                           });
    }
    AnalysisFunctionAbi classified = argument_types ? analysis_classify_call_abi(arena, analysis, function_type_id, argument_types, argument_count, target)
                                                    : analysis_classify_function_abi(arena, analysis, function_type_id, target);
    AnalysisAbiConvention expected = abi == CODEGEN_ABI_X86_64_SYSTEM_V   ? ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64
                                     : abi == CODEGEN_ABI_X86_64_WINDOWS  ? ANALYSIS_ABI_CONVENTION_WIN64_X86_64
                                     : abi == CODEGEN_ABI_AARCH64_DARWIN  ? ANALYSIS_ABI_CONVENTION_APPLE_AARCH64
                                     : abi == CODEGEN_ABI_AARCH64_WINDOWS ? ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64
                                                                          : ANALYSIS_ABI_CONVENTION_AAPCS64;
    if (classified.convention != expected)
    {
        return result;
    }
    result.argument_count = classified.argument_count;
    result.indirect_result_register = classified.indirect_result_register;
    result.arguments = arena_allocate(arena, CodegenAbiLocation, result.argument_count);
    for (u32 value_index = 0; value_index <= result.argument_count; value_index += 1)
    {
        AnalysisAbiValue* source = value_index == result.argument_count ? &classified.result : classified.arguments + value_index;
        CodegenAbiLocation* destination = value_index == result.argument_count ? &result.result : result.arguments + value_index;
        AnalysisTypeId value_type_id = value_index == result.argument_count
                                           ? function_type->as.function.return_type
                                           : (argument_types ? argument_types[value_index] : function_type->as.function.argument_types[value_index]);
        AnalysisType* value_type = analysis_type_from_id(analysis, value_type_id);
        if (source->part_count > CODEGEN_ABI_MAX_PARTS)
        {
            return result;
        }
        destination->part_count = source->part_count;
        destination->indirect = source->indirect;
        destination->indirect_copy_offset = source->indirect_copy_offset;
        if (source->indirect && value_index != result.argument_count &&
            (u64)source->indirect_copy_offset + value_type->layout.size > classified.stack_size)
        {
            return result;
        }
        for (u32 part_index = 0; part_index < source->part_count; part_index += 1)
        {
            AnalysisAbiPart* source_part = source->parts + part_index;
            CodegenAbiPart* destination_part = destination->parts + part_index;
            if (!source_part->size || source_part->value_offset + source_part->size > value_type->layout.size)
            {
                return result;
            }
            destination_part->index = source_part->register_index;
            destination_part->stack_offset = source_part->stack_offset;
            destination_part->value_offset = source_part->value_offset;
            destination_part->size = source_part->size;
            destination_part->kind = source_part->location == ANALYSIS_ABI_LOCATION_STACK ? CODEGEN_ABI_LOCATION_STACK
                                     : source_part->abi_class == ANALYSIS_ABI_CLASS_FLOAT || source_part->abi_class == ANALYSIS_ABI_CLASS_VECTOR
                                         ? CODEGEN_ABI_LOCATION_FLOAT_REGISTER
                                         : CODEGEN_ABI_LOCATION_INTEGER_REGISTER;
            if (destination_part->kind == CODEGEN_ABI_LOCATION_STACK && destination_part->stack_offset + destination_part->size > classified.stack_size)
            {
                return result;
            }
            if (destination_part->kind != CODEGEN_ABI_LOCATION_STACK)
            {
                u32 limit = abi == CODEGEN_ABI_X86_64_SYSTEM_V
                                ? (destination_part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER ? 8 : (value_index == result.argument_count ? 2 : 6))
                            : abi == CODEGEN_ABI_X86_64_WINDOWS ? (value_index == result.argument_count ? 1 : 4)
                                                                : 8;
                if (destination_part->index >= limit)
                {
                    return result;
                }
            }
        }
        if (destination->part_count)
        {
            destination->index = destination->parts[0].index;
            destination->stack_offset = destination->parts[0].stack_offset;
            destination->kind = destination->parts[0].kind;
        }
    }
    if (abi == CODEGEN_ABI_X86_64_WINDOWS)
    {
        if (!codegen_win64_relayout_indirect_copies(analysis, function_type, argument_types, result.argument_count, &result))
        {
            return (CodegenAbiSignature){0};
        }
    }
    else
    {
        result.stack_size = classified.stack_size;
    }
    result.valid = true;
    return result;
}

CodegenAbiSignature codegen_classify_signature_with_arguments(Arena* arena, AnalysisResult* analysis, AnalysisTypeId function_type_id,
                                                               AnalysisTypeId* argument_types, u32 argument_count, Target target)
{
    return codegen_classify_signature_with_arguments_prepared(arena, analysis, function_type_id, argument_types, argument_count, target, true);
}

BUSTER_GLOBAL_LOCAL Target codegen_abi_targets[CODEGEN_ABI_COUNT];
BUSTER_GLOBAL_LOCAL bool codegen_abi_targets_built;

// Called per aggregate-ABI classification, so the feature-array fold is
// cached per abi instead of re-run on every query.
Target codegen_target_for_abi(CodegenAbi abi)
{
    if (!codegen_abi_targets_built)
    {
        BUSTER_CHECK_SERIAL_INITIALIZATION();
        for (u32 abi_index = 0; abi_index < CODEGEN_ABI_COUNT; abi_index += 1)
        {
            bool x86 = abi_index == CODEGEN_ABI_X86_64_SYSTEM_V || abi_index == CODEGEN_ABI_X86_64_WINDOWS;
            codegen_abi_targets[abi_index] = (Target){
                .cpu_arch = x86 ? CPU_ARCH_X86_64 : CPU_ARCH_AARCH64,
                .os = abi_index == CODEGEN_ABI_X86_64_WINDOWS    ? OPERATING_SYSTEM_WINDOWS
                      : abi_index == CODEGEN_ABI_AARCH64_DARWIN  ? OPERATING_SYSTEM_MACOS
                      : abi_index == CODEGEN_ABI_AARCH64_WINDOWS ? OPERATING_SYSTEM_WINDOWS
                                                                 : OPERATING_SYSTEM_LINUX,
                .cpu_features_explicit = true,
                .cpu_features = x86 ? target_cpu_features_from_array((TargetCpuFeature const[]){
                                              TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX,
                                              TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AVX512F,
                                              TARGET_CPU_FEATURE_X86_AVX512VL, TARGET_CPU_FEATURE_X86_AVX512BW}, 6)
                                    : target_cpu_features_singleton(TARGET_CPU_FEATURE_AARCH64_NEON),
            };
        }
        codegen_abi_targets_built = true;
    }
    // An out-of-range abi used to fall through every x86/Windows/Darwin test,
    // which is exactly the AAPCS64 row.
    return codegen_abi_targets[(u32)abi < CODEGEN_ABI_COUNT ? (u32)abi : CODEGEN_ABI_AARCH64_AAPCS64];
}

// The one codegen table built on first use; asking for any abi fills them all.
void codegen_prewarm(void)
{
    (void)codegen_target_for_abi(CODEGEN_ABI_X86_64_SYSTEM_V);
}

BUSTER_GLOBAL_LOCAL CodegenAbiSignature codegen_classify_signature_for_target(Arena* arena, AnalysisResult* analysis, AnalysisTypeId function_type_id,
                                                                              Target target)
{
    return codegen_classify_signature_with_arguments_prepared(arena, analysis, function_type_id, 0, 0, target, false);
}

CodegenAbiSignature codegen_classify_signature(Arena* arena, AnalysisResult* analysis, AnalysisTypeId function_type_id, CodegenAbi abi)
{
    return codegen_classify_signature_with_arguments(arena, analysis, function_type_id, 0, 0, codegen_target_for_abi(abi));
}

// The one place code-buffer exhaustion is reported. It lives out of line
// because the scalar emitters are inlined throughout the backend and this is
// the only path none of them take: reporting it in a caller costs more per
// emitted byte, across nineteen megabytes of them, than the report is worth.
BUSTER_GLOBAL_LOCAL BUSTER_COLD BUSTER_PRESERVE_MOST void codegen_buffer_report_exhausted(CodegenBuffer* buffer)
{
    buffer->error = CODEGEN_ERROR_CAPACITY;
    if (buffer->exhausted)
    {
        *buffer->exhausted = true;
    }
}

BUSTER_GLOBAL_LOCAL BUSTER_ALWAYS_INLINE bool codegen_buffer_reserve(CodegenBuffer* buffer, u64 byte_count, u8** output)
{
    if (buffer->count > buffer->capacity || byte_count > buffer->capacity - buffer->count)
    {
        codegen_buffer_report_exhausted(buffer);
        return false;
    }
    *output = buffer->bytes + buffer->count;
    buffer->count += byte_count;
    return true;
}

BUSTER_GLOBAL_LOCAL void codegen_emit_u8(CodegenBuffer* buffer, u8 value)
{
    if (BUSTER_UNLIKELY(buffer->count >= buffer->capacity))
    {
        codegen_buffer_report_exhausted(buffer);
        return;
    }
    buffer->bytes[buffer->count++] = value;
}

BUSTER_GLOBAL_LOCAL void codegen_emit_u32(CodegenBuffer* buffer, u32 value)
{
    u8* output;
    if (!codegen_buffer_reserve(buffer, 4, &output))
    {
        return;
    }
    output[0] = (u8)value;
    output[1] = (u8)(value >> 8);
    output[2] = (u8)(value >> 16);
    output[3] = (u8)(value >> 24);
}

BUSTER_GLOBAL_LOCAL void codegen_emit_u64(CodegenBuffer* buffer, u64 value)
{
    u8* output;
    if (!codegen_buffer_reserve(buffer, 8, &output))
    {
        return;
    }
    output[0] = (u8)value;
    output[1] = (u8)(value >> 8);
    output[2] = (u8)(value >> 16);
    output[3] = (u8)(value >> 24);
    output[4] = (u8)(value >> 32);
    output[5] = (u8)(value >> 40);
    output[6] = (u8)(value >> 48);
    output[7] = (u8)(value >> 56);
}

#if BUSTER_INCLUDE_TESTS
void codegen_test_emit_scalar(CodegenBuffer* buffer, u32 byte_count, u64 value)
{
    switch (byte_count)
    {
    case 1:
        codegen_emit_u8(buffer, (u8)value);
        break;
    case 4:
        codegen_emit_u32(buffer, (u32)value);
        break;
    case 8:
        codegen_emit_u64(buffer, value);
        break;
    default:
        buffer->error = CODEGEN_ERROR_CAPACITY;
        break;
    }
}
#endif

BUSTER_GLOBAL_LOCAL s32 x64_value_displacement_component(IrValueId value, u32 component)
{
    return -(s32)(value.value * X64_VALUE_SLOT_SIZE + (component + 1) * 8);
}

BUSTER_GLOBAL_LOCAL s32 x64_value_displacement(IrValueId value)
{
    return x64_value_displacement_component(value, 0);
}

BUSTER_GLOBAL_LOCAL s32 x64_temporary_displacement(X64Builder* builder, u32 index)
{
    return -(s32)(builder->temporary_base + index * X64_VALUE_SLOT_SIZE + 8);
}

BUSTER_GLOBAL_LOCAL s32 x64_local_storage_displacement(X64Builder* builder, AnalysisLocalId local)
{
    return -(s32)builder->local_storage_offsets[local.value];
}

BUSTER_GLOBAL_LOCAL bool codegen_type_is_indirect_value(AnalysisType* type)
{
    return type->kind == ANALYSIS_TYPE_ARRAY || type->kind == ANALYSIS_TYPE_VECTOR || type->kind == ANALYSIS_TYPE_INFERRED_ARRAY ||
           type->kind == ANALYSIS_TYPE_STRUCT || type->kind == ANALYSIS_TYPE_UNION;
}

BUSTER_GLOBAL_LOCAL bool codegen_type_is_inline_collection(AnalysisType* type)
{
    return type->kind == ANALYSIS_TYPE_SLICE || type->kind == ANALYSIS_TYPE_RANGE || type->kind == ANALYSIS_TYPE_VA_LIST;
}

BUSTER_GLOBAL_LOCAL u32 codegen_type_storage_size(AnalysisType* type)
{
    u64 size = type->layout.size;
    if (!size)
    {
        size = 8;
    }
    if (size > UINT32_MAX)
    {
        return 0;
    }
    return (u32)size;
}

BUSTER_GLOBAL_LOCAL AnalysisEntitySemantic* codegen_type_semantic(AnalysisResult* analysis, AnalysisType* type)
{
    if (type->kind != ANALYSIS_TYPE_STRUCT && type->kind != ANALYSIS_TYPE_UNION)
    {
        return 0;
    }
    AnalysisResult* owner = analysis;
    if (type->as.declaration.module.value != analysis->module.id.value)
    {
        owner = 0;
        for (u32 index = 0; index < analysis->program_module_count; index += 1)
        {
            AnalysisResult* candidate = analysis->program_modules[index];
            if (candidate && candidate->module.id.value == type->as.declaration.module.value)
            {
                owner = candidate;
                break;
            }
        }
    }
    if (!owner || type->as.declaration.index.value >= owner->module.entity_count)
    {
        return 0;
    }
    return owner->module.semantics + type->as.declaration.index.value;
}

typedef struct CodegenRegisterAllocation CodegenRegisterAllocation;
struct CodegenRegisterAllocation
{
    u8* registers;
    u32 allocated_count;
    u32 spilled_count;
};

#define CODEGEN_REGISTER_UNALLOCATED UINT8_MAX

BUSTER_GLOBAL_LOCAL bool codegen_register_type_eligible(AnalysisType* type)
{
    return type->kind == ANALYSIS_TYPE_BOOL || type->kind == ANALYSIS_TYPE_INTEGER || type->kind == ANALYSIS_TYPE_POINTER || type->kind == ANALYSIS_TYPE_ENUM;
}

BUSTER_GLOBAL_LOCAL CodegenRegisterAllocation codegen_allocate_registers(Arena* arena, AnalysisResult* analysis, IrFunction* function, u32 register_count,
                                                                         bool vectors)
{
    CodegenRegisterAllocation result = {
        .registers = arena_allocate(arena, u8, function->value_count),
    };
    u32* starts = arena_allocate(arena, u32, function->value_count);
    u32* ends = arena_allocate(arena, u32, function->value_count);
    u32* blocks = arena_allocate(arena, u32, function->value_count);
    IrBlockId* instruction_blocks = arena_allocate(arena, IrBlockId, function->instruction_count);
    bool* candidates = arena_allocate(arena, bool, function->value_count);
    bool* eligible = arena_allocate(arena, bool, function->value_count);
    memset(candidates, 0, sizeof(*candidates) * function->value_count);
    memset(eligible, 0, sizeof(*eligible) * function->value_count);
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        result.registers[value_index] = CODEGEN_REGISTER_UNALLOCATED;
        blocks[value_index] = UINT32_MAX;
    }
    // The validated owner array replaces the block walk this used to do for
    // itself, which left instruction_blocks uninitialized for any instruction
    // no chain reached and then compared against it below. An unproven
    // function keeps every value in its frame slot instead.
    if (ir_function_instruction_owners(function, instruction_blocks).error != IR_VALIDATION_NONE)
    {
        return result;
    }
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->result.value == IR_ID_UNDERLYING_INVALID)
        {
            continue;
        }
        IrValueId value_id = instruction->result;
        IrValue* value = function->values + value_id.value;
        AnalysisType* type = analysis_type_from_id(analysis, value->type);
        bool eligible_type = vectors ? type->kind == ANALYSIS_TYPE_VECTOR && type->layout.size <= 16 : codegen_register_type_eligible(type);
        bool eligible_instruction = !vectors || instruction->opcode == IR_OPCODE_UNARY || instruction->opcode == IR_OPCODE_BINARY;
        candidates[value_id.value] = value->category == IR_VALUE_VALUE && eligible_type && eligible_instruction;
        eligible[value_id.value] = candidates[value_id.value];
        starts[value_id.value] = instruction_index;
        ends[value_id.value] = instruction_index;
        blocks[value_id.value] = instruction_blocks[instruction_index].value;
    }
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
        {
            IrValueId operand = instruction->operands[operand_index];
            if (operand.value >= function->value_count || !candidates[operand.value])
            {
                continue;
            }
            if (blocks[operand.value] != instruction_blocks[instruction_index].value)
            {
                candidates[operand.value] = false;
                continue;
            }
            ends[operand.value] = BUSTER_MAX(ends[operand.value], instruction_index);
        }
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        for (IrBlockParameter* parameter = function->blocks[block_index].first_parameter; parameter; parameter = parameter->next)
        {
            for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
            {
                if (incoming->value.value < function->value_count)
                {
                    candidates[incoming->value.value] = false;
                }
            }
        }
    }
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        if (!candidates[value_index] || starts[value_index] == ends[value_index])
        {
            candidates[value_index] = false;
            continue;
        }
        for (u32 instruction_index = starts[value_index] + 1; instruction_index < ends[value_index]; instruction_index += 1)
        {
            if (function->instructions[instruction_index].opcode == IR_OPCODE_CALL)
            {
                candidates[value_index] = false;
                break;
            }
        }
    }
    IrValueId* active_values = arena_allocate(arena, IrValueId, register_count);
    u32* active_ends = arena_allocate(arena, u32, register_count);
    for (u32 register_index = 0; register_index < register_count; register_index += 1)
    {
        active_values[register_index] = IR_VALUE_ID_INVALID;
    }
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        for (u32 register_index = 0; register_index < register_count; register_index += 1)
        {
            if (active_values[register_index].value != IR_ID_UNDERLYING_INVALID && active_ends[register_index] < instruction_index)
            {
                active_values[register_index] = IR_VALUE_ID_INVALID;
            }
        }
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->result.value == IR_ID_UNDERLYING_INVALID || !candidates[instruction->result.value])
        {
            continue;
        }
        bool assigned = false;
        for (u32 register_index = 0; register_index < register_count; register_index += 1)
        {
            if (active_values[register_index].value != IR_ID_UNDERLYING_INVALID)
            {
                continue;
            }
            active_values[register_index] = instruction->result;
            active_ends[register_index] = ends[instruction->result.value];
            result.registers[instruction->result.value] = (u8)register_index;
            result.allocated_count += 1;
            assigned = true;
            break;
        }
        if (!assigned)
        {
            continue;
        }
    }
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        if (eligible[value_index] && starts[value_index] != ends[value_index] && result.registers[value_index] == CODEGEN_REGISTER_UNALLOCATED)
        {
            result.spilled_count += 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void x64_emit_load(X64Builder* builder, X64Register target, s32 displacement)
{
    u8 rex = target >= X64_REGISTER_R8 ? 0x4c : 0x48;
    u8 register_bits = (u8)(target & 7);
    codegen_emit_u8(&builder->buffer, rex);
    codegen_emit_u8(&builder->buffer, 0x8b);
    codegen_emit_u8(&builder->buffer, (u8)(0x85 | (register_bits << 3)));
    codegen_emit_u32(&builder->buffer, (u32)displacement);
}

BUSTER_GLOBAL_LOCAL void x64_emit_store(X64Builder* builder, X64Register source, s32 displacement)
{
    u8 rex = source >= X64_REGISTER_R8 ? 0x4c : 0x48;
    u8 register_bits = (u8)(source & 7);
    codegen_emit_u8(&builder->buffer, rex);
    codegen_emit_u8(&builder->buffer, 0x89);
    codegen_emit_u8(&builder->buffer, (u8)(0x85 | (register_bits << 3)));
    codegen_emit_u32(&builder->buffer, (u32)displacement);
}

BUSTER_GLOBAL_LOCAL void x64_emit_move_register(X64Builder* builder, X64Register target, X64Register source)
{
    if (target == source)
    {
        return;
    }
    u8 rex = 0x48;
    rex |= source >= X64_REGISTER_R8 ? 0x04 : 0;
    rex |= target >= X64_REGISTER_R8 ? 0x01 : 0;
    codegen_emit_u8(&builder->buffer, rex);
    codegen_emit_u8(&builder->buffer, 0x89);
    codegen_emit_u8(&builder->buffer, (u8)(0xc0 | ((source & 7) << 3) | (target & 7)));
}

BUSTER_GLOBAL_LOCAL X64Register x64_allocated_register(X64Builder* builder, IrValueId value)
{
    BUSTER_CHECK(builder->value_registers[value.value] == 0);
    return X64_REGISTER_R10;
}

BUSTER_GLOBAL_LOCAL void x64_emit_load_value(X64Builder* builder, X64Register target, IrValueId value)
{
    if (builder->value_registers[value.value] != CODEGEN_REGISTER_UNALLOCATED)
    {
        x64_emit_move_register(builder, target, x64_allocated_register(builder, value));
        return;
    }
    x64_emit_load(builder, target, x64_value_displacement(value));
}

BUSTER_GLOBAL_LOCAL void x64_emit_constant_register(X64Builder* builder, X64Register target, u64 value)
{
    codegen_emit_u8(&builder->buffer, target >= X64_REGISTER_R8 ? 0x49 : 0x48);
    codegen_emit_u8(&builder->buffer, (u8)(0xb8 + (target & 7)));
    codegen_emit_u64(&builder->buffer, value);
}

BUSTER_GLOBAL_LOCAL void x64_emit_address(X64Builder* builder, X64Register target, s32 displacement)
{
    u8 rex = target >= X64_REGISTER_R8 ? 0x4c : 0x48;
    u8 register_bits = (u8)(target & 7);
    codegen_emit_u8(&builder->buffer, rex);
    codegen_emit_u8(&builder->buffer, 0x8d);
    codegen_emit_u8(&builder->buffer, (u8)(0x85 | (register_bits << 3)));
    codegen_emit_u32(&builder->buffer, (u32)displacement);
}

BUSTER_GLOBAL_LOCAL void x64_emit_copy_memory(X64Builder* builder, u32 size)
{
    u32 offset = 0;
    while (size - offset >= 8)
    {
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x8b);
        codegen_emit_u8(&builder->buffer, 0x91);
        codegen_emit_u32(&builder->buffer, offset);
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0x90);
        codegen_emit_u32(&builder->buffer, offset);
        offset += 8;
    }
    if (size - offset >= 4)
    {
        codegen_emit_u8(&builder->buffer, 0x8b);
        codegen_emit_u8(&builder->buffer, 0x91);
        codegen_emit_u32(&builder->buffer, offset);
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0x90);
        codegen_emit_u32(&builder->buffer, offset);
        offset += 4;
    }
    if (size - offset >= 2)
    {
        codegen_emit_u8(&builder->buffer, 0x66);
        codegen_emit_u8(&builder->buffer, 0x8b);
        codegen_emit_u8(&builder->buffer, 0x91);
        codegen_emit_u32(&builder->buffer, offset);
        codegen_emit_u8(&builder->buffer, 0x66);
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0x90);
        codegen_emit_u32(&builder->buffer, offset);
        offset += 2;
    }
    if (size != offset)
    {
        codegen_emit_u8(&builder->buffer, 0x8a);
        codegen_emit_u8(&builder->buffer, 0x91);
        codegen_emit_u32(&builder->buffer, offset);
        codegen_emit_u8(&builder->buffer, 0x88);
        codegen_emit_u8(&builder->buffer, 0x90);
        codegen_emit_u32(&builder->buffer, offset);
    }
}

BUSTER_GLOBAL_LOCAL void x64_emit_load_memory_rax(X64Builder* builder, u32 size)
{
    if (size == 1)
    {
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0xb6);
        codegen_emit_u8(&builder->buffer, 0x00);
    }
    else if (size == 2)
    {
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0xb7);
        codegen_emit_u8(&builder->buffer, 0x00);
    }
    else if (size == 4)
    {
        codegen_emit_u8(&builder->buffer, 0x8b);
        codegen_emit_u8(&builder->buffer, 0x00);
    }
    else
    {
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x8b);
        codegen_emit_u8(&builder->buffer, 0x00);
    }
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_memory_rcx(X64Builder* builder, u32 size)
{
    if (size == 1)
    {
        codegen_emit_u8(&builder->buffer, 0x88);
        codegen_emit_u8(&builder->buffer, 0x08);
    }
    else if (size == 2)
    {
        codegen_emit_u8(&builder->buffer, 0x66);
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0x08);
    }
    else if (size == 4)
    {
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0x08);
    }
    else
    {
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0x08);
    }
}

BUSTER_GLOBAL_LOCAL void x64_emit_load_memory(X64Builder* builder, X64Register target, X64Register base, u32 offset, u32 size)
{
    u8 rex = 0x40;
    rex |= target >= X64_REGISTER_R8 ? 0x04 : 0;
    rex |= base >= X64_REGISTER_R8 ? 0x01 : 0;
    if (size == 8)
    {
        rex |= 0x08;
    }
    if (size == 1 || size == 2)
    {
        if (size == 2)
        {
            codegen_emit_u8(&builder->buffer, 0x66);
        }
        if (rex != 0x40)
        {
            codegen_emit_u8(&builder->buffer, rex);
        }
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, size == 1 ? 0xb6 : 0xb7);
    }
    else
    {
        if (rex != 0x40)
        {
            codegen_emit_u8(&builder->buffer, rex);
        }
        codegen_emit_u8(&builder->buffer, 0x8b);
    }
    codegen_emit_u8(&builder->buffer, (u8)(0x80 | ((target & 7) << 3) | (base & 7)));
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_memory(X64Builder* builder, X64Register base, u32 offset, X64Register source, u32 size)
{
    if (size == 2)
    {
        codegen_emit_u8(&builder->buffer, 0x66);
    }
    u8 rex = 0x40;
    rex |= source >= X64_REGISTER_R8 ? 0x04 : 0;
    rex |= base >= X64_REGISTER_R8 ? 0x01 : 0;
    rex |= size == 8 ? 0x08 : 0;
    if (rex != 0x40)
    {
        codegen_emit_u8(&builder->buffer, rex);
    }
    codegen_emit_u8(&builder->buffer, size == 1 ? 0x88 : 0x89);
    codegen_emit_u8(&builder->buffer, (u8)(0x80 | ((source & 7) << 3) | (base & 7)));
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL void x64_emit_load_float_bits(X64Builder* builder, u32 target, X64Register base, u32 offset, u32 size)
{
    codegen_emit_u8(&builder->buffer, 0xf3);
    if (base >= X64_REGISTER_R8)
    {
        codegen_emit_u8(&builder->buffer, 0x41);
    }
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, size <= 4 ? 0x10 : 0x7e);
    codegen_emit_u8(&builder->buffer, (u8)(0x80 | ((target & 7) << 3) | (base & 7)));
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_float_bits(X64Builder* builder, X64Register base, u32 offset, u32 source, u32 size)
{
    codegen_emit_u8(&builder->buffer, size <= 4 ? 0xf3 : 0x66);
    if (base >= X64_REGISTER_R8)
    {
        codegen_emit_u8(&builder->buffer, 0x41);
    }
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, size <= 4 ? 0x11 : 0xd6);
    codegen_emit_u8(&builder->buffer, (u8)(0x80 | ((source & 7) << 3) | (base & 7)));
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL void x64_emit_rsp_address(X64Builder* builder, X64Register target, u32 offset)
{
    codegen_emit_u8(&builder->buffer, target >= X64_REGISTER_R8 ? 0x4c : 0x48);
    codegen_emit_u8(&builder->buffer, 0x8d);
    codegen_emit_u8(&builder->buffer, (u8)(0x84 | ((target & 7) << 3)));
    codegen_emit_u8(&builder->buffer, 0x24);
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL bool x64_emit_collection_component(X64Builder* builder, IrValueId base_id, u32 component, X64Register target)
{
    IrValue* base_value = builder->function->values + base_id.value;
    AnalysisType* base_type = analysis_type_from_id(builder->analysis, base_value->type);
    if (base_value->category == IR_VALUE_PLACE)
    {
        if (base_type->kind == ANALYSIS_TYPE_ARRAY || base_type->kind == ANALYSIS_TYPE_VECTOR || base_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY)
        {
            bool vector = base_type->kind == ANALYSIS_TYPE_VECTOR;
            if (component == 0)
            {
                x64_emit_load(builder, target, x64_value_displacement(base_id));
            }
            else if (component == 1)
            {
                x64_emit_constant_register(builder, target, vector ? base_type->as.vector.count : base_type->as.array.count);
            }
            else if (component == 2)
            {
                AnalysisType* element = analysis_type_from_id(builder->analysis, vector ? base_type->as.vector.element_type : base_type->as.array.element_type);
                x64_emit_constant_register(builder, target, codegen_type_storage_size(element));
            }
            else
            {
                x64_emit_constant_register(builder, target, 0);
            }
            return true;
        }
        if (base_type->kind == ANALYSIS_TYPE_SLICE)
        {
            x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement(base_id));
            u8 rex = target >= X64_REGISTER_R8 ? 0x4c : 0x48;
            u8 register_bits = (u8)(target & 7);
            codegen_emit_u8(&builder->buffer, rex);
            codegen_emit_u8(&builder->buffer, 0x8b);
            codegen_emit_u8(&builder->buffer, (u8)(0x80 | (register_bits << 3)));
            codegen_emit_u32(&builder->buffer, component * 8);
            return true;
        }
    }
    x64_emit_load(builder, target, x64_value_displacement_component(base_id, component));
    return true;
}

BUSTER_GLOBAL_LOCAL void x64_emit_float_load(X64Builder* builder, u32 register_index, s32 displacement, u32 width)
{
    codegen_emit_u8(&builder->buffer, width == 32 ? 0xf3 : 0xf2);
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, 0x10);
    codegen_emit_u8(&builder->buffer, (u8)(0x85 | ((register_index & 7) << 3)));
    codegen_emit_u32(&builder->buffer, (u32)displacement);
}

BUSTER_GLOBAL_LOCAL void x64_emit_float_store(X64Builder* builder, u32 register_index, s32 displacement, u32 width)
{
    codegen_emit_u8(&builder->buffer, width == 32 ? 0xf3 : 0xf2);
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, 0x11);
    codegen_emit_u8(&builder->buffer, (u8)(0x85 | ((register_index & 7) << 3)));
    codegen_emit_u32(&builder->buffer, (u32)displacement);
}

BUSTER_GLOBAL_LOCAL X64Register x64_abi_integer_argument_register(CodegenAbi abi, u32 index)
{
    X64Register system_v[] = {
        X64_REGISTER_RDI, X64_REGISTER_RSI, X64_REGISTER_RDX, X64_REGISTER_RCX, X64_REGISTER_R8, X64_REGISTER_R9,
    };
    X64Register windows[] = {
        X64_REGISTER_RCX,
        X64_REGISTER_RDX,
        X64_REGISTER_R8,
        X64_REGISTER_R9,
    };
    return abi == CODEGEN_ABI_X86_64_WINDOWS ? windows[index] : system_v[index];
}

BUSTER_GLOBAL_LOCAL void x64_emit_initialize_aggregate_result(X64Builder* builder, IrValueId value)
{
    x64_emit_address(builder, X64_REGISTER_RAX, -(s32)builder->value_storage_offsets[value.value]);
    x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(value, 0));
}

BUSTER_GLOBAL_LOCAL bool x64_emit_vector_abi_move(X64Builder* builder, bool store, u32 vector_register, X64Register base, u32 offset, u32 size)
{
    if (vector_register >= 8 || base >= X64_REGISTER_R8 || size > target_vector_register_size(builder->target) ||
        (size != 8 && size != 16 && size != 32 && size != 64))
    {
        return false;
    }
    if (size == 64)
    {
        codegen_emit_u8(&builder->buffer, 0x62);
        codegen_emit_u8(&builder->buffer, 0xf1);
        codegen_emit_u8(&builder->buffer, 0x7c);
        codegen_emit_u8(&builder->buffer, 0x48);
    }
    else if (size == 32)
    {
        codegen_emit_u8(&builder->buffer, 0xc5);
        codegen_emit_u8(&builder->buffer, 0xfc);
    }
    else
    {
        if (size == 8)
        {
            codegen_emit_u8(&builder->buffer, store ? 0x66 : 0xf3);
        }
        codegen_emit_u8(&builder->buffer, 0x0f);
    }
    codegen_emit_u8(&builder->buffer, size == 8 ? (store ? 0xd6 : 0x7e) : (store ? 0x11 : 0x10));
    codegen_emit_u8(&builder->buffer, (u8)(0x80 | ((vector_register & 7) << 3) | (base & 7)));
    codegen_emit_u32(&builder->buffer, offset);
    return true;
}

BUSTER_GLOBAL_LOCAL void x64_emit_load_abi_part(X64Builder* builder, IrValueId value, AnalysisType* type, CodegenAbiPart* part, X64Register integer_target,
                                                u32 float_target)
{
    if (codegen_type_is_indirect_value(type))
    {
        if (type->kind == ANALYSIS_TYPE_VECTOR && part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(value, 0));
            if (!x64_emit_vector_abi_move(builder, false, float_target, X64_REGISTER_RAX, part->value_offset, part->size))
            {
                builder->buffer.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
            }
            return;
        }
        x64_emit_load(builder, X64_REGISTER_R11, x64_value_displacement_component(value, 0));
        if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            x64_emit_load_float_bits(builder, float_target, X64_REGISTER_R11, part->value_offset, part->size);
        }
        else
        {
            x64_emit_load_memory(builder, integer_target, X64_REGISTER_R11, part->value_offset, part->size);
        }
        return;
    }
    if (codegen_type_is_inline_collection(type))
    {
        u32 component = part->value_offset / 8;
        if (type->kind == ANALYSIS_TYPE_RANGE)
        {
            AnalysisType* element = analysis_type_from_id(builder->analysis, type->as.element_type);
            component = part->value_offset / BUSTER_MAX(codegen_type_storage_size(element), 1);
        }
        if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            x64_emit_float_load(builder, float_target, x64_value_displacement_component(value, component), part->size * 8);
        }
        else
        {
            x64_emit_load(builder, integer_target, x64_value_displacement_component(value, component));
            if (type->kind == ANALYSIS_TYPE_RANGE)
            {
                AnalysisType* element = analysis_type_from_id(builder->analysis, type->as.element_type);
                u32 element_size = codegen_type_storage_size(element);
                if (element_size < 8 && part->size > element_size)
                {
                    u8 target_rex = integer_target >= X64_REGISTER_R8 ? 0x49 : 0x48;
                    u8 target_bits = (u8)(integer_target & 7);
                    u8 shift = (u8)(element_size * 8);
                    codegen_emit_u8(&builder->buffer, target_rex);
                    codegen_emit_u8(&builder->buffer, 0xc1);
                    codegen_emit_u8(&builder->buffer, (u8)(0xe0 | target_bits));
                    codegen_emit_u8(&builder->buffer, (u8)(64 - shift));
                    codegen_emit_u8(&builder->buffer, target_rex);
                    codegen_emit_u8(&builder->buffer, 0xc1);
                    codegen_emit_u8(&builder->buffer, (u8)(0xe8 | target_bits));
                    codegen_emit_u8(&builder->buffer, (u8)(64 - shift));
                    x64_emit_load(builder, X64_REGISTER_R11, x64_value_displacement_component(value, component + 1));
                    codegen_emit_u8(&builder->buffer, 0x49);
                    codegen_emit_u8(&builder->buffer, 0xc1);
                    codegen_emit_u8(&builder->buffer, 0xe3);
                    codegen_emit_u8(&builder->buffer, shift);
                    codegen_emit_u8(&builder->buffer, integer_target >= X64_REGISTER_R8 ? 0x4d : 0x4c);
                    codegen_emit_u8(&builder->buffer, 0x09);
                    codegen_emit_u8(&builder->buffer, (u8)(0xd8 | target_bits));
                }
            }
        }
        return;
    }
    if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
    {
        x64_emit_float_load(builder, float_target, x64_value_displacement(value), type->as.float_bit_width);
    }
    else
    {
        x64_emit_load_value(builder, integer_target, value);
    }
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_abi_part(X64Builder* builder, IrValueId value, AnalysisType* type, CodegenAbiPart* part, X64Register integer_source,
                                                 u32 float_source)
{
    if (codegen_type_is_indirect_value(type))
    {
        if (type->kind == ANALYSIS_TYPE_VECTOR && part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(value, 0));
            if (!x64_emit_vector_abi_move(builder, true, float_source, X64_REGISTER_RAX, part->value_offset, part->size))
            {
                builder->buffer.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
            }
            return;
        }
        x64_emit_load(builder, X64_REGISTER_R11, x64_value_displacement_component(value, 0));
        if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            x64_emit_store_float_bits(builder, X64_REGISTER_R11, part->value_offset, float_source, part->size);
        }
        else
        {
            x64_emit_store_memory(builder, X64_REGISTER_R11, part->value_offset, integer_source, part->size);
        }
        return;
    }
    if (codegen_type_is_inline_collection(type))
    {
        u32 component = part->value_offset / 8;
        if (type->kind == ANALYSIS_TYPE_RANGE)
        {
            AnalysisType* element = analysis_type_from_id(builder->analysis, type->as.element_type);
            component = part->value_offset / BUSTER_MAX(codegen_type_storage_size(element), 1);
        }
        if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            x64_emit_float_store(builder, float_source, x64_value_displacement_component(value, component), part->size * 8);
        }
        else
        {
            x64_emit_store(builder, integer_source, x64_value_displacement_component(value, component));
            if (type->kind == ANALYSIS_TYPE_RANGE)
            {
                AnalysisType* element = analysis_type_from_id(builder->analysis, type->as.element_type);
                u32 element_size = codegen_type_storage_size(element);
                if (element_size < 8 && part->size > element_size)
                {
                    x64_emit_move_register(builder, X64_REGISTER_R11, integer_source);
                    codegen_emit_u8(&builder->buffer, 0x49);
                    codegen_emit_u8(&builder->buffer, 0xc1);
                    codegen_emit_u8(&builder->buffer, 0xeb);
                    codegen_emit_u8(&builder->buffer, (u8)(element_size * 8));
                    x64_emit_store(builder, X64_REGISTER_R11, x64_value_displacement_component(value, component + 1));
                }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL bool x64_emit_windows_stack_allocate(CodegenBuffer* buffer, u32 size, CodegenFunctionDescriptor* descriptor, u32 action_capacity,
                                                          u32 function_offset)
{
    if (!buffer || size <= 4096)
    {
        return false;
    }
    // Probe with volatile r10/r11 while RSP still denotes the caller-visible
    // frame. The loop keeps the prolog size constant even for very large
    // frames; only the final SUB changes RSP and therefore needs a UWOP.
    codegen_emit_u8(buffer, 0x49);
    codegen_emit_u8(buffer, 0x89);
    codegen_emit_u8(buffer, 0xe2);
    codegen_emit_u8(buffer, 0x49);
    codegen_emit_u8(buffer, 0x81);
    codegen_emit_u8(buffer, 0xea);
    codegen_emit_u32(buffer, size);
    codegen_emit_u8(buffer, 0x49);
    codegen_emit_u8(buffer, 0x89);
    codegen_emit_u8(buffer, 0xe3);
    u64 loop_offset = buffer->count;
    codegen_emit_u8(buffer, 0x49);
    codegen_emit_u8(buffer, 0x81);
    codegen_emit_u8(buffer, 0xeb);
    codegen_emit_u32(buffer, 4096);
    codegen_emit_u8(buffer, 0x4d);
    codegen_emit_u8(buffer, 0x39);
    codegen_emit_u8(buffer, 0xd3);
    codegen_emit_u8(buffer, 0x76);
    u64 final_patch = buffer->count;
    codegen_emit_u8(buffer, 0);
    codegen_emit_u8(buffer, 0x41);
    codegen_emit_u8(buffer, 0xf6);
    codegen_emit_u8(buffer, 0x03);
    codegen_emit_u8(buffer, 0);
    codegen_emit_u8(buffer, 0xeb);
    u64 loop_patch = buffer->count;
    codegen_emit_u8(buffer, 0);
    u64 final_offset = buffer->count;
    codegen_emit_u8(buffer, 0x41);
    codegen_emit_u8(buffer, 0xf6);
    codegen_emit_u8(buffer, 0x02);
    codegen_emit_u8(buffer, 0);
    codegen_emit_u8(buffer, 0x48);
    codegen_emit_u8(buffer, 0x81);
    codegen_emit_u8(buffer, 0xec);
    codegen_emit_u32(buffer, size);
    s64 final_displacement = (s64)final_offset - (s64)(final_patch + 1);
    s64 loop_displacement = (s64)loop_offset - (s64)(loop_patch + 1);
    if (buffer->error != CODEGEN_ERROR_NONE || final_displacement < INT8_MIN || final_displacement > INT8_MAX || loop_displacement < INT8_MIN ||
        loop_displacement > INT8_MAX || buffer->count - function_offset > UINT32_MAX)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return true;
    }
    buffer->bytes[final_patch] = (u8)(s8)final_displacement;
    buffer->bytes[loop_patch] = (u8)(s8)loop_displacement;
    if (descriptor && !codegen_unwind_action_append(descriptor, action_capacity, (u32)(buffer->count - function_offset),
                                                    CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, size))
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void x64_emit_stack_adjust_described(X64Builder* builder, u32 size, bool subtract, CodegenFunctionDescriptor* descriptor,
                                                         u32 action_capacity)
{
    if (!subtract)
    {
        if (!size)
        {
            return;
        }
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, size <= INT8_MAX ? 0x83 : 0x81);
        codegen_emit_u8(&builder->buffer, 0xc4);
        if (size <= INT8_MAX)
        {
            codegen_emit_u8(&builder->buffer, (u8)size);
        }
        else
        {
            codegen_emit_u32(&builder->buffer, size);
        }
        return;
    }
    if (builder->target.os == OPERATING_SYSTEM_WINDOWS && x64_emit_windows_stack_allocate(&builder->buffer, size, descriptor, action_capacity, 0))
    {
        return;
    }
    while (size)
    {
        u32 chunk = BUSTER_MIN(size, 4096u);
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, chunk <= INT8_MAX ? 0x83 : 0x81);
        codegen_emit_u8(&builder->buffer, 0xec);
        if (chunk <= INT8_MAX)
        {
            codegen_emit_u8(&builder->buffer, (u8)chunk);
        }
        else
        {
            codegen_emit_u32(&builder->buffer, chunk);
        }
        if (descriptor && !codegen_unwind_action_append(descriptor, action_capacity, (u32)builder->buffer.count,
                                                        CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, chunk))
        {
            builder->buffer.error = CODEGEN_ERROR_CAPACITY;
            return;
        }
        codegen_emit_u8(&builder->buffer, 0xf6);
        codegen_emit_u8(&builder->buffer, 0x04);
        codegen_emit_u8(&builder->buffer, 0x24);
        codegen_emit_u8(&builder->buffer, 0);
        size -= chunk;
    }
}

BUSTER_GLOBAL_LOCAL void x64_emit_stack_adjust(X64Builder* builder, u32 size, bool subtract)
{
    x64_emit_stack_adjust_described(builder, size, subtract, 0, 0);
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_rsp(X64Builder* builder, X64Register source, u32 offset)
{
    u8 rex = source >= X64_REGISTER_R8 ? 0x4c : 0x48;
    u8 register_bits = (u8)(source & 7);
    codegen_emit_u8(&builder->buffer, rex);
    codegen_emit_u8(&builder->buffer, 0x89);
    codegen_emit_u8(&builder->buffer, (u8)(0x84 | (register_bits << 3)));
    codegen_emit_u8(&builder->buffer, 0x24);
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL void x64_emit_float_store_rsp(X64Builder* builder, u32 register_index, u32 offset, u32 width)
{
    codegen_emit_u8(&builder->buffer, width == 32 ? 0xf3 : 0xf2);
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, 0x11);
    codegen_emit_u8(&builder->buffer, (u8)(0x84 | ((register_index & 7) << 3)));
    codegen_emit_u8(&builder->buffer, 0x24);
    codegen_emit_u32(&builder->buffer, offset);
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_result(X64Builder* builder, IrInstruction* instruction)
{
    if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
    {
        if (builder->value_registers[instruction->result.value] != CODEGEN_REGISTER_UNALLOCATED)
        {
            x64_emit_move_register(builder, x64_allocated_register(builder, instruction->result), X64_REGISTER_RAX);
        }
        else
        {
            x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement(instruction->result));
        }
    }
}

BUSTER_GLOBAL_LOCAL CodegenError codegen_x64_maximum_call_stack_size_prepared(Arena* arena, AnalysisResult* analysis, IrFunction* function,
                                                                              Target target, u32* stack_size, bool prepare_layouts)
{
    if (!stack_size)
    {
        return CODEGEN_ERROR_INVALID_IR;
    }
    *stack_size = 0;
    if (!arena || !analysis || !function || !function->instructions || !function->values)
    {
        return CODEGEN_ERROR_INVALID_IR;
    }
    if (prepare_layouts)
    {
        analysis_compute_layouts(analysis, (AnalysisLayoutOptions){
                                               .pointer_size = 8,
                                               .pointer_alignment = 8,
                                           });
    }
    u32 result = 0;
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->opcode != IR_OPCODE_CALL)
        {
            continue;
        }
        if (!instruction->operand_count || !instruction->operands || instruction->operands[0].value >= function->value_count)
        {
            return CODEGEN_ERROR_INVALID_IR;
        }
        AnalysisTypeId callee_type_id = function->values[instruction->operands[0].value].type;
        AnalysisType* callee_type = codegen_analysis_type_from_id_checked(analysis, callee_type_id);
        if (!callee_type)
        {
            return CODEGEN_ERROR_INVALID_IR;
        }
        bool indirect = callee_type->kind == ANALYSIS_TYPE_POINTER;
        AnalysisTypeId function_type_id = indirect ? callee_type->as.element_type : callee_type_id;
        AnalysisType* function_type = codegen_analysis_type_from_id_checked(analysis, function_type_id);
        if (!function_type)
        {
            return CODEGEN_ERROR_INVALID_IR;
        }
        if (function_type->kind != ANALYSIS_TYPE_FUNCTION)
        {
            return CODEGEN_ERROR_UNSUPPORTED_ABI;
        }
        u32 argument_count = instruction->operand_count - 1;
        u32 fixed_argument_count = function_type->as.function.argument_count;
        if ((!function_type->as.function.is_variadic && argument_count != fixed_argument_count) ||
            (function_type->as.function.is_variadic && argument_count < fixed_argument_count))
        {
            return CODEGEN_ERROR_INVALID_IR;
        }
        if ((function_type->as.function.argument_count && !function_type->as.function.argument_types) ||
            !codegen_analysis_type_from_id_checked(analysis, function_type->as.function.return_type))
        {
            return CODEGEN_ERROR_INVALID_IR;
        }
        for (u32 argument_index = 0; argument_index < function_type->as.function.argument_count; argument_index += 1)
        {
            if (!codegen_analysis_type_from_id_checked(analysis, function_type->as.function.argument_types[argument_index]))
            {
                return CODEGEN_ERROR_INVALID_IR;
            }
        }
        AnalysisTypeId* argument_types = argument_count ? arena_allocate(arena, AnalysisTypeId, argument_count) : 0;
        for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
        {
            IrValueId argument = instruction->operands[argument_index + 1];
            if (argument.value >= function->value_count)
            {
                return CODEGEN_ERROR_INVALID_IR;
            }
            argument_types[argument_index] = function->values[argument.value].type;
            if (!codegen_analysis_type_from_id_checked(analysis, argument_types[argument_index]))
            {
                return CODEGEN_ERROR_INVALID_IR;
            }
        }
        CodegenAbiSignature signature = codegen_classify_signature_with_arguments_prepared(arena, analysis, function_type_id, argument_types,
                                                                                            argument_count, target, false);
        if (!signature.valid)
        {
            return CODEGEN_ERROR_UNSUPPORTED_ABI;
        }
        result = BUSTER_MAX(result, signature.stack_size);
    }
    *stack_size = result;
    return CODEGEN_ERROR_NONE;
}

CodegenError codegen_x64_maximum_call_stack_size(Arena* arena, AnalysisResult* analysis, IrFunction* function, Target target, u32* stack_size)
{
    return codegen_x64_maximum_call_stack_size_prepared(arena, analysis, function, target, stack_size, true);
}

BUSTER_GLOBAL_LOCAL void x64_relocation_add(X64Builder* builder, IrBlockId target)
{
    CodegenRelocation* relocation = arena_allocate(builder->arena, CodegenRelocation, 1);
    *relocation = (CodegenRelocation){
        .target = target,
        .displacement_offset = (u32)builder->buffer.count,
    };
    if (builder->last_relocation)
    {
        builder->last_relocation->next = relocation;
    }
    else
    {
        builder->first_relocation = relocation;
    }
    builder->last_relocation = relocation;
    codegen_emit_u32(&builder->buffer, 0);
}

BUSTER_GLOBAL_LOCAL void x64_emit_jump(X64Builder* builder, IrBlockId target)
{
    codegen_emit_u8(&builder->buffer, 0xe9);
    x64_relocation_add(builder, target);
}

BUSTER_GLOBAL_LOCAL void x64_call_relocation_add(X64Builder* builder, IrInstruction* instruction)
{
    CodegenCallRelocation* relocation = arena_allocate(builder->arena, CodegenCallRelocation, 1);
    *relocation = (CodegenCallRelocation){
        .entity = instruction->entity,
        .instantiation = instruction->instantiation,
        .displacement_offset = (u32)builder->buffer.count,
    };
    if (builder->last_call_relocation)
    {
        builder->last_call_relocation->next = relocation;
    }
    else
    {
        builder->first_call_relocation = relocation;
    }
    builder->last_call_relocation = relocation;
    codegen_emit_u32(&builder->buffer, 0);
}

BUSTER_GLOBAL_LOCAL IrValueId x64_parameter_incoming(IrBlockParameter* parameter, IrBlockId predecessor)
{
    for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
    {
        if (incoming->predecessor.value == predecessor.value)
        {
            return incoming->value;
        }
    }
    return IR_VALUE_ID_INVALID;
}

BUSTER_GLOBAL_LOCAL void x64_emit_edge_copies(X64Builder* builder, IrBlockId predecessor, IrBlockId target)
{
    IrBlock* block = builder->function->blocks + target.value;
    u32 index = 0;
    for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
    {
        IrValueId incoming = x64_parameter_incoming(parameter, predecessor);
        if (incoming.value == IR_ID_UNDERLYING_INVALID)
        {
            builder->buffer.error = CODEGEN_ERROR_INVALID_IR;
            return;
        }
        for (u32 component = 0; component < X64_VALUE_SLOT_COMPONENT_COUNT; component += 1)
        {
            x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(incoming, component));
            x64_emit_store(builder, X64_REGISTER_RAX, x64_temporary_displacement(builder, index) - (s32)(component * 8));
        }
        index += 1;
    }
    index = 0;
    for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
    {
        for (u32 component = 0; component < X64_VALUE_SLOT_COMPONENT_COUNT; component += 1)
        {
            x64_emit_load(builder, X64_REGISTER_RAX, x64_temporary_displacement(builder, index) - (s32)(component * 8));
            x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(parameter->value, component));
        }
        index += 1;
    }
}

BUSTER_GLOBAL_LOCAL void x64_emit_return(X64Builder* builder)
{
    if (builder->abi == CODEGEN_ABI_X86_64_WINDOWS)
    {
        if (builder->frame_size)
        {
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, builder->frame_size <= INT8_MAX ? 0x83 : 0x81);
            codegen_emit_u8(&builder->buffer, 0xc4);
            if (builder->frame_size <= INT8_MAX)
            {
                codegen_emit_u8(&builder->buffer, (u8)builder->frame_size);
            }
            else
            {
                codegen_emit_u32(&builder->buffer, builder->frame_size);
            }
        }
        codegen_emit_u8(&builder->buffer, 0x5d);
        codegen_emit_u8(&builder->buffer, 0xc3);
        return;
    }
    codegen_emit_u8(&builder->buffer, 0xc9);
    codegen_emit_u8(&builder->buffer, 0xc3);
}

BUSTER_GLOBAL_LOCAL void x64_emit_set_condition(X64Builder* builder, u8 condition)
{
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, condition);
    codegen_emit_u8(&builder->buffer, 0xc0);
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, 0xb6);
    codegen_emit_u8(&builder->buffer, 0xc0);
}

BUSTER_GLOBAL_LOCAL bool x64_emit_integer_binary(X64Builder* builder, IrInstruction* instruction)
{
    if (instruction->binary_operation == IR_BINARY_RANGE)
    {
        x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 0));
        x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[1]);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 1));
        codegen_emit_u8(&builder->buffer, 0x31);
        codegen_emit_u8(&builder->buffer, 0xc0);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 2));
        return true;
    }
    x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
    x64_emit_load_value(builder, X64_REGISTER_RCX, instruction->operands[1]);
    switch (instruction->binary_operation)
    {
    case IR_BINARY_INTEGER_ADD:
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x01);
        codegen_emit_u8(&builder->buffer, 0xc8);
        break;
    case IR_BINARY_INTEGER_SUBTRACT:
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x29);
        codegen_emit_u8(&builder->buffer, 0xc8);
        break;
    case IR_BINARY_INTEGER_MULTIPLY:
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0xaf);
        codegen_emit_u8(&builder->buffer, 0xc1);
        break;
    case IR_BINARY_INTEGER_BITWISE_AND:
    case IR_BINARY_BOOLEAN_AND:
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x21);
        codegen_emit_u8(&builder->buffer, 0xc8);
        break;
    case IR_BINARY_INTEGER_BITWISE_OR:
    case IR_BINARY_BOOLEAN_OR:
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x09);
        codegen_emit_u8(&builder->buffer, 0xc8);
        break;
    case IR_BINARY_INTEGER_BITWISE_XOR:
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x31);
        codegen_emit_u8(&builder->buffer, 0xc8);
        break;
    case IR_BINARY_SHIFT_LEFT:
    case IR_BINARY_SIGNED_SHIFT_RIGHT:
    case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0xd3);
        codegen_emit_u8(&builder->buffer, instruction->binary_operation == IR_BINARY_SHIFT_LEFT           ? 0xe0
                                          : instruction->binary_operation == IR_BINARY_SIGNED_SHIFT_RIGHT ? 0xf8
                                                                                                          : 0xe8);
        break;
    case IR_BINARY_SIGNED_DIVIDE:
    case IR_BINARY_SIGNED_REMAINDER:
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x99);
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0xf7);
        codegen_emit_u8(&builder->buffer, 0xf9);
        if (instruction->binary_operation == IR_BINARY_SIGNED_REMAINDER)
        {
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x89);
            codegen_emit_u8(&builder->buffer, 0xd0);
        }
        break;
    case IR_BINARY_UNSIGNED_DIVIDE:
    case IR_BINARY_UNSIGNED_REMAINDER:
        codegen_emit_u8(&builder->buffer, 0x31);
        codegen_emit_u8(&builder->buffer, 0xd2);
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0xf7);
        codegen_emit_u8(&builder->buffer, 0xf1);
        if (instruction->binary_operation == IR_BINARY_UNSIGNED_REMAINDER)
        {
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x89);
            codegen_emit_u8(&builder->buffer, 0xd0);
        }
        break;
    case IR_BINARY_INTEGER_EQUAL:
    case IR_BINARY_BOOLEAN_EQUAL:
    case IR_BINARY_POINTER_EQUAL:
    case IR_BINARY_INTEGER_NOT_EQUAL:
    case IR_BINARY_BOOLEAN_NOT_EQUAL:
    case IR_BINARY_POINTER_NOT_EQUAL:
    case IR_BINARY_SIGNED_LESS:
    case IR_BINARY_SIGNED_LESS_EQUAL:
    case IR_BINARY_SIGNED_GREATER:
    case IR_BINARY_SIGNED_GREATER_EQUAL:
    case IR_BINARY_UNSIGNED_LESS:
    case IR_BINARY_UNSIGNED_LESS_EQUAL:
    case IR_BINARY_UNSIGNED_GREATER:
    case IR_BINARY_UNSIGNED_GREATER_EQUAL:
    {
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x39);
        codegen_emit_u8(&builder->buffer, 0xc8);
        u8 condition = instruction->binary_operation == IR_BINARY_INTEGER_EQUAL || instruction->binary_operation == IR_BINARY_BOOLEAN_EQUAL ||
                               instruction->binary_operation == IR_BINARY_POINTER_EQUAL
                           ? 0x94
                       : instruction->binary_operation == IR_BINARY_INTEGER_NOT_EQUAL || instruction->binary_operation == IR_BINARY_BOOLEAN_NOT_EQUAL ||
                               instruction->binary_operation == IR_BINARY_POINTER_NOT_EQUAL
                           ? 0x95
                       : instruction->binary_operation == IR_BINARY_SIGNED_LESS          ? 0x9c
                       : instruction->binary_operation == IR_BINARY_SIGNED_LESS_EQUAL    ? 0x9e
                       : instruction->binary_operation == IR_BINARY_SIGNED_GREATER       ? 0x9f
                       : instruction->binary_operation == IR_BINARY_SIGNED_GREATER_EQUAL ? 0x9d
                       : instruction->binary_operation == IR_BINARY_UNSIGNED_LESS        ? 0x92
                       : instruction->binary_operation == IR_BINARY_UNSIGNED_LESS_EQUAL  ? 0x96
                       : instruction->binary_operation == IR_BINARY_UNSIGNED_GREATER     ? 0x97
                                                                                         : 0x93;
        x64_emit_set_condition(builder, condition);
    }
    break;
    default:
        return false;
    }
    x64_emit_store_result(builder, instruction);
    return true;
}

BUSTER_GLOBAL_LOCAL bool x64_emit_float_binary(X64Builder* builder, IrInstruction* instruction)
{
    AnalysisType* operand_type = analysis_type_from_id(builder->analysis, builder->function->values[instruction->operands[0].value].type);
    u32 width = operand_type->as.float_bit_width;
    x64_emit_float_load(builder, 0, x64_value_displacement(instruction->operands[0]), width);
    x64_emit_float_load(builder, 1, x64_value_displacement(instruction->operands[1]), width);
    if (instruction->binary_operation >= IR_BINARY_FLOAT_ADD && instruction->binary_operation <= IR_BINARY_FLOAT_DIVIDE)
    {
        codegen_emit_u8(&builder->buffer, width == 32 ? 0xf3 : 0xf2);
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, instruction->binary_operation == IR_BINARY_FLOAT_ADD        ? 0x58
                                          : instruction->binary_operation == IR_BINARY_FLOAT_SUBTRACT ? 0x5c
                                          : instruction->binary_operation == IR_BINARY_FLOAT_MULTIPLY ? 0x59
                                                                                                      : 0x5e);
        codegen_emit_u8(&builder->buffer, 0xc1);
        x64_emit_float_store(builder, 0, x64_value_displacement(instruction->result), width);
        return true;
    }
    if (width == 64)
    {
        codegen_emit_u8(&builder->buffer, 0x66);
    }
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, 0x2e);
    codegen_emit_u8(&builder->buffer, 0xc1);
    u8 condition = instruction->binary_operation == IR_BINARY_FLOAT_EQUAL           ? 0x94
                   : instruction->binary_operation == IR_BINARY_FLOAT_NOT_EQUAL     ? 0x95
                   : instruction->binary_operation == IR_BINARY_FLOAT_LESS          ? 0x92
                   : instruction->binary_operation == IR_BINARY_FLOAT_LESS_EQUAL    ? 0x96
                   : instruction->binary_operation == IR_BINARY_FLOAT_GREATER       ? 0x97
                   : instruction->binary_operation == IR_BINARY_FLOAT_GREATER_EQUAL ? 0x93
                                                                                    : 0;
    if (!condition)
    {
        return false;
    }
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, condition);
    codegen_emit_u8(&builder->buffer, 0xc0);
    if (instruction->binary_operation == IR_BINARY_FLOAT_EQUAL || instruction->binary_operation == IR_BINARY_FLOAT_LESS ||
        instruction->binary_operation == IR_BINARY_FLOAT_LESS_EQUAL)
    {
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0x9b);
        codegen_emit_u8(&builder->buffer, 0xc2);
        codegen_emit_u8(&builder->buffer, 0x20);
        codegen_emit_u8(&builder->buffer, 0xd0);
    }
    else if (instruction->binary_operation == IR_BINARY_FLOAT_NOT_EQUAL)
    {
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0x9a);
        codegen_emit_u8(&builder->buffer, 0xc2);
        codegen_emit_u8(&builder->buffer, 0x08);
        codegen_emit_u8(&builder->buffer, 0xd0);
    }
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, 0xb6);
    codegen_emit_u8(&builder->buffer, 0xc0);
    x64_emit_store_result(builder, instruction);
    return true;
}

// popcnt eax/rax, eax/rax. The C frontend only produces this operation when
// the target has POPCNT and expands the SWAR form itself otherwise, so there
// is no second sequence to keep in step here.
BUSTER_GLOBAL_LOCAL void x64_emit_population_count(CodegenBuffer* buffer, u32 width)
{
    codegen_emit_u8(buffer, 0xf3);
    if (width > 32)
    {
        codegen_emit_u8(buffer, 0x48);
    }
    codegen_emit_u8(buffer, 0x0f);
    codegen_emit_u8(buffer, 0xb8);
    codegen_emit_u8(buffer, 0xc0);
}

BUSTER_GLOBAL_LOCAL void x64_emit_vector_memory_operation(X64Builder* builder, u8 prefix, u8 opcode, u32 vector_register, X64Register base)
{
    if (prefix)
    {
        codegen_emit_u8(&builder->buffer, prefix);
    }
    if (vector_register >= 8 || base >= X64_REGISTER_R8)
    {
        codegen_emit_u8(&builder->buffer, (u8)(0x40 | (vector_register >= 8 ? 0x04 : 0) | (base >= X64_REGISTER_R8 ? 0x01 : 0)));
    }
    codegen_emit_u8(&builder->buffer, 0x0f);
    codegen_emit_u8(&builder->buffer, opcode);
    codegen_emit_u8(&builder->buffer, (u8)(((vector_register & 7) << 3) | (base & 7)));
}

void x64_emit_vector_native_memory(X64Builder* builder, bool store, u32 size, X64Register base)
{
    if (size == 64)
    {
        codegen_emit_u8(&builder->buffer, 0x62);
        codegen_emit_u8(&builder->buffer, base >= X64_REGISTER_R8 ? 0xd1 : 0xf1);
        codegen_emit_u8(&builder->buffer, 0x7c);
        codegen_emit_u8(&builder->buffer, 0x48);
    }
    else if (base >= X64_REGISTER_R8)
    {
        codegen_emit_u8(&builder->buffer, 0xc4);
        codegen_emit_u8(&builder->buffer, 0xc1);
        codegen_emit_u8(&builder->buffer, 0x7c);
    }
    else
    {
        codegen_emit_u8(&builder->buffer, 0xc5);
        codegen_emit_u8(&builder->buffer, 0xfc);
    }
    codegen_emit_u8(&builder->buffer, store ? 0x11 : 0x10);
    codegen_emit_u8(&builder->buffer, (u8)(base & 7));
}

void x64_emit_vector_native_binary_operation(X64Builder* builder, u8 prefix, u8 opcode, u32 size, X64Register base)
{
    u8 packed_prefix = prefix == 0x66 ? 1 : 0;
    if (size == 64)
    {
        codegen_emit_u8(&builder->buffer, 0x62);
        codegen_emit_u8(&builder->buffer, base >= X64_REGISTER_R8 ? 0xd1 : 0xf1);
        codegen_emit_u8(&builder->buffer, (u8)(0x7c | packed_prefix));
        codegen_emit_u8(&builder->buffer, 0x48);
    }
    else if (base >= X64_REGISTER_R8)
    {
        codegen_emit_u8(&builder->buffer, 0xc4);
        codegen_emit_u8(&builder->buffer, 0xc1);
        codegen_emit_u8(&builder->buffer, (u8)(0x7c | packed_prefix));
    }
    else
    {
        codegen_emit_u8(&builder->buffer, 0xc5);
        codegen_emit_u8(&builder->buffer, (u8)(0xfc | packed_prefix));
    }
    codegen_emit_u8(&builder->buffer, opcode);
    codegen_emit_u8(&builder->buffer, (u8)(base & 7));
}

bool x64_target_supports_native_vector(Target target, u64 size, u32 element_width, bool integer_operation)
{
    if (size <= 16 || size > target_vector_register_size(target))
    {
        return false;
    }
    if (!integer_operation)
    {
        return true;
    }
    if (size == 32)
    {
        return target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX2);
    }
    if (size == 64 && element_width < 32)
    {
        return target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512BW);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x64_vector_binary_is_commutative(IrBinaryOperation operation)
{
    return operation == IR_BINARY_VECTOR_INTEGER_ADD || operation == IR_BINARY_VECTOR_INTEGER_BITWISE_AND || operation == IR_BINARY_VECTOR_INTEGER_BITWISE_OR ||
           operation == IR_BINARY_VECTOR_INTEGER_BITWISE_XOR;
}

void x64_emit_vzeroupper(X64Builder* builder)
{
    if (!builder->upper_vector_dirty)
    {
        return;
    }
    codegen_emit_u8(&builder->buffer, 0xc5);
    codegen_emit_u8(&builder->buffer, 0xf8);
    codegen_emit_u8(&builder->buffer, 0x77);
    builder->upper_vector_dirty = false;
    builder->last_wide_vector_result = IR_VALUE_ID_INVALID;
    builder->last_wide_vector_size = 0;
    builder->vzeroupper_count += 1;
}

BUSTER_GLOBAL_LOCAL bool x64_vector_comparison_condition(IrBinaryOperation operation, u8* condition_out, bool* ordered_out, bool* unordered_out)
{
    u8 condition = 0;
    bool ordered = false;
    bool unordered = false;
    switch (operation)
    {
    case IR_BINARY_VECTOR_INTEGER_EQUAL:
    case IR_BINARY_VECTOR_FLOAT_EQUAL:
        condition = 0x94;
        ordered = operation == IR_BINARY_VECTOR_FLOAT_EQUAL;
        break;
    case IR_BINARY_VECTOR_INTEGER_NOT_EQUAL:
    case IR_BINARY_VECTOR_FLOAT_NOT_EQUAL:
        condition = 0x95;
        unordered = operation == IR_BINARY_VECTOR_FLOAT_NOT_EQUAL;
        break;
    case IR_BINARY_VECTOR_SIGNED_LESS:
        condition = 0x9c;
        break;
    case IR_BINARY_VECTOR_SIGNED_LESS_EQUAL:
        condition = 0x9e;
        break;
    case IR_BINARY_VECTOR_SIGNED_GREATER:
        condition = 0x9f;
        break;
    case IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL:
        condition = 0x9d;
        break;
    case IR_BINARY_VECTOR_UNSIGNED_LESS:
    case IR_BINARY_VECTOR_FLOAT_LESS:
        condition = 0x92;
        ordered = operation == IR_BINARY_VECTOR_FLOAT_LESS;
        break;
    case IR_BINARY_VECTOR_UNSIGNED_LESS_EQUAL:
    case IR_BINARY_VECTOR_FLOAT_LESS_EQUAL:
        condition = 0x96;
        ordered = operation == IR_BINARY_VECTOR_FLOAT_LESS_EQUAL;
        break;
    case IR_BINARY_VECTOR_UNSIGNED_GREATER:
    case IR_BINARY_VECTOR_FLOAT_GREATER:
        condition = 0x97;
        break;
    case IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL:
    case IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL:
        condition = 0x93;
        break;
    default:
        return false;
    }
    *condition_out = condition;
    *ordered_out = ordered;
    *unordered_out = unordered;
    return true;
}

BUSTER_GLOBAL_LOCAL bool x64_emit_vector_comparison(X64Builder* builder, IrInstruction* instruction, AnalysisType* vector, AnalysisType* element)
{
    u8 condition = 0;
    bool ordered = false;
    bool unordered = false;
    if (!x64_vector_comparison_condition(instruction->binary_operation, &condition, &ordered, &unordered))
    {
        return false;
    }
    u32 width = element->kind == ANALYSIS_TYPE_FLOAT ? element->as.float_bit_width : element->kind == ANALYSIS_TYPE_INTEGER ? element->as.integer.bit_width : 0;
    u32 lane_size = width / 8;
    if (!lane_size)
    {
        return false;
    }
    x64_emit_initialize_aggregate_result(builder, instruction->result);
    x64_emit_load(builder, X64_REGISTER_R8, x64_value_displacement_component(instruction->operands[0], 0));
    x64_emit_load(builder, X64_REGISTER_R9, x64_value_displacement_component(instruction->operands[1], 0));
    x64_emit_load(builder, X64_REGISTER_R10, x64_value_displacement_component(instruction->result, 0));
    for (u32 lane = 0; lane < vector->as.vector.count; lane += 1)
    {
        u32 offset = lane * lane_size;
        if (element->kind == ANALYSIS_TYPE_FLOAT)
        {
            x64_emit_load_float_bits(builder, 0, X64_REGISTER_R8, offset, lane_size);
            x64_emit_load_float_bits(builder, 1, X64_REGISTER_R9, offset, lane_size);
            if (width == 64)
            {
                codegen_emit_u8(&builder->buffer, 0x66);
            }
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x2e);
            codegen_emit_u8(&builder->buffer, 0xc1);
        }
        else
        {
            x64_emit_load_memory(builder, X64_REGISTER_RAX, X64_REGISTER_R8, offset, lane_size);
            x64_emit_load_memory(builder, X64_REGISTER_RCX, X64_REGISTER_R9, offset, lane_size);
            if (lane_size == 2)
            {
                codegen_emit_u8(&builder->buffer, 0x66);
            }
            if (lane_size == 8)
            {
                codegen_emit_u8(&builder->buffer, 0x48);
            }
            codegen_emit_u8(&builder->buffer, lane_size == 1 ? 0x38 : 0x39);
            codegen_emit_u8(&builder->buffer, 0xc8);
        }
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, condition);
        codegen_emit_u8(&builder->buffer, 0xc0);
        if (ordered || unordered)
        {
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, unordered ? 0x9a : 0x9b);
            codegen_emit_u8(&builder->buffer, 0xc2);
            codegen_emit_u8(&builder->buffer, unordered ? 0x08 : 0x20);
            codegen_emit_u8(&builder->buffer, 0xd0);
        }
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0xb6);
        codegen_emit_u8(&builder->buffer, 0xc0);
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0xf7);
        codegen_emit_u8(&builder->buffer, 0xd8);
        x64_emit_store_memory(builder, X64_REGISTER_R10, offset, X64_REGISTER_RAX, lane_size);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x64_emit_vector_binary(X64Builder* builder, IrInstruction* instruction)
{
    AnalysisType* vector = analysis_type_from_id(builder->analysis, builder->function->values[instruction->operands[0].value].type);
    if (vector->kind != ANALYSIS_TYPE_VECTOR)
    {
        return false;
    }
    AnalysisType* element = analysis_type_from_id(builder->analysis, vector->as.vector.element_type);
    if (instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_EQUAL && instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL)
    {
        return x64_emit_vector_comparison(builder, instruction, vector, element);
    }
    u32 width = element->kind == ANALYSIS_TYPE_FLOAT ? element->as.float_bit_width : element->kind == ANALYSIS_TYPE_INTEGER ? element->as.integer.bit_width : 0;
    if (!width || vector->layout.size > 64)
    {
        return false;
    }
    u8 opcode = 0;
    u8 prefix = 0;
    switch (instruction->binary_operation)
    {
    case IR_BINARY_VECTOR_FLOAT_ADD:
        opcode = 0x58;
        break;
    case IR_BINARY_VECTOR_FLOAT_SUBTRACT:
        opcode = 0x5c;
        break;
    case IR_BINARY_VECTOR_FLOAT_MULTIPLY:
        opcode = 0x59;
        break;
    case IR_BINARY_VECTOR_FLOAT_DIVIDE:
        opcode = 0x5e;
        break;
    case IR_BINARY_VECTOR_INTEGER_ADD:
    {
        prefix = 0x66;
        opcode = width == 8 ? 0xfc : width == 16 ? 0xfd : width == 32 ? 0xfe : width == 64 ? 0xd4 : 0;
    }
    break;
    case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
    {
        prefix = 0x66;
        opcode = width == 8 ? 0xf8 : width == 16 ? 0xf9 : width == 32 ? 0xfa : width == 64 ? 0xfb : 0;
    }
    break;
    case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
        prefix = 0x66;
        opcode = 0xdb;
        break;
    case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
        prefix = 0x66;
        opcode = 0xeb;
        break;
    case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
        prefix = 0x66;
        opcode = 0xef;
        break;
    default:
        return false;
    }
    if (!opcode)
    {
        return false;
    }
    if (element->kind == ANALYSIS_TYPE_FLOAT && width == 64)
    {
        prefix = 0x66;
    }
    x64_emit_initialize_aggregate_result(builder, instruction->result);
    x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->operands[0], 0));
    x64_emit_load(builder, X64_REGISTER_RCX, x64_value_displacement_component(instruction->operands[1], 0));
    x64_emit_load(builder, X64_REGISTER_RDX, x64_value_displacement_component(instruction->result, 0));
    bool integer_operation = element->kind == ANALYSIS_TYPE_INTEGER;
    bool native_width = x64_target_supports_native_vector(builder->target, vector->layout.size, width, integer_operation);
    if (native_width)
    {
        builder->native_vector_operation_count += 1;
        bool forwarded_left = builder->upper_vector_dirty && builder->last_wide_vector_result.value == instruction->operands[0].value &&
                              builder->last_wide_vector_size == vector->layout.size;
        bool forwarded_right = builder->upper_vector_dirty && x64_vector_binary_is_commutative(instruction->binary_operation) &&
                               builder->last_wide_vector_result.value == instruction->operands[1].value &&
                               builder->last_wide_vector_size == vector->layout.size;
        if (forwarded_left || forwarded_right)
        {
            builder->forwarded_wide_vector_load_count += 1;
        }
        else
        {
            x64_emit_vector_native_memory(builder, false, (u32)vector->layout.size, X64_REGISTER_RAX);
        }
        x64_emit_vector_native_binary_operation(builder, prefix, opcode, (u32)vector->layout.size, forwarded_right ? X64_REGISTER_RAX : X64_REGISTER_RCX);
        x64_emit_vector_native_memory(builder, true, (u32)vector->layout.size, X64_REGISTER_RDX);
        builder->upper_vector_dirty = true;
        builder->last_wide_vector_result = instruction->result;
        builder->last_wide_vector_size = (u32)vector->layout.size;
        return true;
    }
    u32 chunk_count = (u32)((vector->layout.size + 15) / 16);
    if (vector->layout.size > 16)
    {
        builder->split_vector_operation_count += 1;
    }
    for (u32 chunk = 0; chunk < chunk_count; chunk += 1)
    {
        u32 offset = chunk * 16;
        u32 target_register = 0;
        if (vector->layout.size <= 16 && builder->vector_registers[instruction->result.value] != CODEGEN_REGISTER_UNALLOCATED)
        {
            target_register = 3 + builder->vector_registers[instruction->result.value];
        }
        u8 load_prefix = vector->layout.size - offset < 16 ? 0xf3 : 0;
        u8 load_opcode = vector->layout.size - offset < 16 ? 0x7e : 0x10;
        x64_emit_vector_memory_operation(builder, load_prefix, load_opcode, target_register, X64_REGISTER_RAX);
        x64_emit_vector_memory_operation(builder, prefix, opcode, target_register, X64_REGISTER_RCX);
        u8 store_prefix = vector->layout.size - offset < 16 ? 0x66 : 0;
        u8 store_opcode = vector->layout.size - offset < 16 ? 0xd6 : 0x11;
        x64_emit_vector_memory_operation(builder, store_prefix, store_opcode, target_register, X64_REGISTER_RDX);
        if (chunk + 1 < chunk_count)
        {
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xc0);
            codegen_emit_u8(&builder->buffer, 16);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xc1);
            codegen_emit_u8(&builder->buffer, 16);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xc2);
            codegen_emit_u8(&builder->buffer, 16);
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x64_emit_vector_unary(X64Builder* builder, IrInstruction* instruction)
{
    AnalysisType* vector = analysis_type_from_id(builder->analysis, instruction->type);
    AnalysisType* element = vector->kind == ANALYSIS_TYPE_VECTOR ? analysis_type_from_id(builder->analysis, vector->as.vector.element_type) : 0;
    u32 width = element && element->kind == ANALYSIS_TYPE_FLOAT     ? element->as.float_bit_width
                : element && element->kind == ANALYSIS_TYPE_INTEGER ? element->as.integer.bit_width
                                                                    : 0;
    if (!width || vector->layout.size > 64)
    {
        return false;
    }
    x64_emit_initialize_aggregate_result(builder, instruction->result);
    x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->operands[0], 0));
    x64_emit_load(builder, X64_REGISTER_RDX, x64_value_displacement_component(instruction->result, 0));
    u32 chunk_count = (u32)((vector->layout.size + 15) / 16);
    for (u32 chunk = 0; chunk < chunk_count; chunk += 1)
    {
        u32 target = 0;
        if (vector->layout.size <= 16 && builder->vector_registers[instruction->result.value] != CODEGEN_REGISTER_UNALLOCATED)
        {
            target = 3 + builder->vector_registers[instruction->result.value];
        }
        bool short_chunk = vector->layout.size - chunk * 16 < 16;
        x64_emit_vector_memory_operation(builder, short_chunk ? 0xf3 : 0, short_chunk ? 0x7e : 0x10, target, X64_REGISTER_RAX);
        if (instruction->unary_operation == IR_UNARY_VECTOR_FLOAT_NEGATE)
        {
            if (width != 32 && width != 64)
            {
                return false;
            }
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x76);
            codegen_emit_u8(&builder->buffer, 0xc9);
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, width == 32 ? 0x72 : 0x73);
            codegen_emit_u8(&builder->buffer, 0xf1);
            codegen_emit_u8(&builder->buffer, width == 32 ? 31 : 63);
            if (width == 64)
            {
                codegen_emit_u8(&builder->buffer, 0x66);
            }
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x57);
            codegen_emit_u8(&builder->buffer, (u8)(0xc1 | (target << 3)));
        }
        else if (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_BITWISE_NOT)
        {
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x76);
            codegen_emit_u8(&builder->buffer, 0xc9);
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0xef);
            codegen_emit_u8(&builder->buffer, (u8)(0xc1 | (target << 3)));
        }
        else if (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE)
        {
            u8 subtract = width == 8 ? 0xf8 : width == 16 ? 0xf9 : width == 32 ? 0xfa : width == 64 ? 0xfb : 0;
            if (!subtract)
            {
                return false;
            }
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0xef);
            codegen_emit_u8(&builder->buffer, 0xc9);
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, subtract);
            codegen_emit_u8(&builder->buffer, (u8)(0xc8 | target));
            codegen_emit_u8(&builder->buffer, 0x66);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x6f);
            codegen_emit_u8(&builder->buffer, (u8)(0xc1 | (target << 3)));
        }
        else
        {
            return false;
        }
        x64_emit_vector_memory_operation(builder, short_chunk ? 0x66 : 0, short_chunk ? 0xd6 : 0x11, target, X64_REGISTER_RDX);
        if (chunk + 1 < chunk_count)
        {
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xc0);
            codegen_emit_u8(&builder->buffer, 16);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xc2);
            codegen_emit_u8(&builder->buffer, 16);
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x64_instruction_uses_wide_vector(X64Builder* builder, IrInstruction* instruction)
{
    if (instruction->opcode != IR_OPCODE_BINARY || instruction->operand_count != 2 || instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_EQUAL)
    {
        return false;
    }
    AnalysisType* vector = analysis_type_from_id(builder->analysis, builder->function->values[instruction->operands[0].value].type);
    if (!vector || vector->kind != ANALYSIS_TYPE_VECTOR)
    {
        return false;
    }
    AnalysisType* element = analysis_type_from_id(builder->analysis, vector->as.vector.element_type);
    u32 width = element->kind == ANALYSIS_TYPE_FLOAT ? element->as.float_bit_width : element->kind == ANALYSIS_TYPE_INTEGER ? element->as.integer.bit_width : 0;
    if (!width || !x64_target_supports_native_vector(builder->target, vector->layout.size, width, element->kind == ANALYSIS_TYPE_INTEGER))
    {
        return false;
    }
    switch (instruction->binary_operation)
    {
    case IR_BINARY_VECTOR_FLOAT_ADD:
    case IR_BINARY_VECTOR_FLOAT_SUBTRACT:
    case IR_BINARY_VECTOR_FLOAT_MULTIPLY:
    case IR_BINARY_VECTOR_FLOAT_DIVIDE:
    case IR_BINARY_VECTOR_INTEGER_ADD:
    case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
    case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
    case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
    case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
        return true;
    default:
        return false;
    }
}

BUSTER_GLOBAL_LOCAL bool codegen_binary_is_float(IrBinaryOperation operation)
{
    return (operation >= IR_BINARY_FLOAT_ADD && operation <= IR_BINARY_FLOAT_DIVIDE) || operation == IR_BINARY_FLOAT_EQUAL ||
           operation == IR_BINARY_FLOAT_NOT_EQUAL || (operation >= IR_BINARY_FLOAT_LESS && operation <= IR_BINARY_FLOAT_GREATER_EQUAL);
}

BUSTER_GLOBAL_LOCAL bool x64_emit_instruction(X64Builder* builder, IrBlockId block, IrInstruction* instruction)
{
    if (builder->upper_vector_dirty && !x64_instruction_uses_wide_vector(builder, instruction))
    {
        x64_emit_vzeroupper(builder);
    }
    switch (instruction->opcode)
    {
    case IR_OPCODE_LOCAL:
    {
        if (instruction->local.value >= builder->function->local_count)
        {
            return false;
        }
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x8d);
        codegen_emit_u8(&builder->buffer, 0x85);
        codegen_emit_u32(&builder->buffer, (u32)x64_local_storage_displacement(builder, instruction->local));
        x64_emit_store_result(builder, instruction);
    }
    break;
    case IR_OPCODE_LOAD:
    {
        AnalysisType* type = analysis_type_from_id(builder->analysis, instruction->type);
        x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
        if (codegen_type_is_indirect_value(type))
        {
            x64_emit_store_result(builder, instruction);
            x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 0));
        }
        else if (codegen_type_is_inline_collection(type))
        {
            for (u32 component = 0; component < X64_VALUE_SLOT_COMPONENT_COUNT; component += 1)
            {
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x8b);
                codegen_emit_u8(&builder->buffer, 0x90);
                codegen_emit_u32(&builder->buffer, component * 8);
                x64_emit_store(builder, X64_REGISTER_RDX, x64_value_displacement_component(instruction->result, component));
            }
        }
        else
        {
            x64_emit_load_memory_rax(builder, codegen_type_storage_size(type));
            x64_emit_store_result(builder, instruction);
        }
    }
    break;
    case IR_OPCODE_STORE:
    {
        AnalysisType* type = analysis_type_from_id(builder->analysis, builder->function->values[instruction->operands[1].value].type);
        x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
        if (codegen_type_is_indirect_value(type))
        {
            x64_emit_load(builder, X64_REGISTER_RCX, x64_value_displacement_component(instruction->operands[1], 0));
            x64_emit_copy_memory(builder, codegen_type_storage_size(type));
        }
        else if (codegen_type_is_inline_collection(type))
        {
            for (u32 component = 0; component < X64_VALUE_SLOT_COMPONENT_COUNT; component += 1)
            {
                x64_emit_load(builder, X64_REGISTER_RDX, x64_value_displacement_component(instruction->operands[1], component));
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x89);
                codegen_emit_u8(&builder->buffer, 0x90);
                codegen_emit_u32(&builder->buffer, component * 8);
            }
        }
        else
        {
            x64_emit_load_value(builder, X64_REGISTER_RCX, instruction->operands[1]);
            x64_emit_store_memory_rcx(builder, codegen_type_storage_size(type));
        }
    }
    break;
    case IR_OPCODE_ARGUMENT:
    {
        if (!instruction->immediate_count)
        {
            return false;
        }
        u32 argument = (u32)instruction->immediates[0];
        AnalysisType* argument_type = analysis_type_from_id(builder->analysis, instruction->type);
        CodegenAbiSignature signature = codegen_classify_signature_for_target(builder->arena, builder->analysis, builder->function->type, builder->target);
        if (argument >= signature.argument_count)
        {
            return false;
        }
        CodegenAbiLocation* location = signature.arguments + argument;
        if (codegen_type_is_indirect_value(argument_type) || codegen_type_is_inline_collection(argument_type))
        {
            if (codegen_type_is_indirect_value(argument_type))
            {
                x64_emit_initialize_aggregate_result(builder, instruction->result);
            }
            if (location->indirect)
            {
                CodegenAbiPart* part = location->parts;
                if (part->kind == CODEGEN_ABI_LOCATION_STACK)
                {
                    x64_emit_load(builder, X64_REGISTER_RCX, (s32)part->stack_offset + 16);
                }
                else
                {
                    x64_emit_move_register(builder, X64_REGISTER_RCX, x64_abi_integer_argument_register(builder->abi, part->index));
                }
                x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 0));
                x64_emit_copy_memory(builder, codegen_type_storage_size(argument_type));
                break;
            }
            if (codegen_type_is_indirect_value(argument_type) && location->part_count && location->parts[0].kind == CODEGEN_ABI_LOCATION_STACK)
            {
                x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 0));
                x64_emit_address(builder, X64_REGISTER_RCX, (s32)location->parts[0].stack_offset + 16);
                x64_emit_copy_memory(builder, codegen_type_storage_size(argument_type));
                break;
            }
            for (u32 part_index = 0; part_index < location->part_count; part_index += 1)
            {
                CodegenAbiPart* part = location->parts + part_index;
                if (part->kind == CODEGEN_ABI_LOCATION_STACK)
                {
                    x64_emit_load(builder, X64_REGISTER_RDX, (s32)part->stack_offset + 16);
                }
                else if (part->kind == CODEGEN_ABI_LOCATION_INTEGER_REGISTER)
                {
                    x64_emit_move_register(builder, X64_REGISTER_RDX, x64_abi_integer_argument_register(builder->abi, part->index));
                }
                else
                {
                    x64_emit_store_abi_part(builder, instruction->result, argument_type, part, X64_REGISTER_RDX, part->index);
                    continue;
                }
                x64_emit_store_abi_part(builder, instruction->result, argument_type, part, X64_REGISTER_RDX, 0);
            }
            break;
        }
        if (location->part_count != 1)
        {
            return false;
        }
        if (location->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            x64_emit_float_store(builder, location->index, x64_value_displacement(instruction->result), argument_type->as.float_bit_width);
            break;
        }
        if (location->kind == CODEGEN_ABI_LOCATION_STACK)
        {
            s32 displacement = (s32)location->stack_offset + 16;
            if (argument_type->kind == ANALYSIS_TYPE_FLOAT)
            {
                x64_emit_float_load(builder, 0, displacement, argument_type->as.float_bit_width);
                x64_emit_float_store(builder, 0, x64_value_displacement(instruction->result), argument_type->as.float_bit_width);
            }
            else
            {
                x64_emit_load(builder, X64_REGISTER_RAX, displacement);
                x64_emit_store_result(builder, instruction);
            }
            break;
        }
        if (location->kind != CODEGEN_ABI_LOCATION_INTEGER_REGISTER && location->kind != CODEGEN_ABI_LOCATION_INDIRECT)
        {
            return false;
        }
        X64Register source = x64_abi_integer_argument_register(builder->abi, location->index);
        x64_emit_move_register(builder, X64_REGISTER_RAX, source);
        x64_emit_store_result(builder, instruction);
    }
    break;
    case IR_OPCODE_CONSTANT_INTEGER:
    case IR_OPCODE_ENUM:
    {
        if (!instruction->immediate_count)
        {
            return false;
        }
        u64 value = instruction->immediates[0];
        if (instruction->immediate_is_negative)
        {
            value = 0 - value;
        }
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0xb8);
        codegen_emit_u64(&builder->buffer, value);
        x64_emit_store_result(builder, instruction);
    }
    break;
    case IR_OPCODE_CONSTANT_FLOAT:
    {
        if (!instruction->immediate_count)
        {
            return false;
        }
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0xb8);
        codegen_emit_u64(&builder->buffer, instruction->immediates[0]);
        x64_emit_store_result(builder, instruction);
    }
    break;
    case IR_OPCODE_CONSTANT_STRING:
    {
        AnalysisType* type = analysis_type_from_id(builder->analysis, instruction->type);
        String8 literal = ir_instruction_extra(builder->function, instruction->id).literal;
        if (type->kind != ANALYSIS_TYPE_ARRAY || type->as.array.count != literal.length)
        {
            return false;
        }
        u32 data_offset = (u32)builder->read_only_data.count;
        if (literal.length > builder->read_only_data.capacity - builder->read_only_data.count)
        {
            return false;
        }
        memcpy(builder->read_only_data.bytes + builder->read_only_data.count, literal.pointer, literal.length);
        builder->read_only_data.count += literal.length;
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x8d);
        codegen_emit_u8(&builder->buffer, 0x05);
        CodegenDataRelocation* relocation = arena_allocate(builder->arena, CodegenDataRelocation, 1);
        *relocation = (CodegenDataRelocation){
            .code_offset = (u32)builder->buffer.count,
            .data_offset = data_offset,
            .kind = CODEGEN_DATA_RELOCATION_X86_64_PC32,
        };
        if (builder->last_data_relocation)
        {
            builder->last_data_relocation->next = relocation;
        }
        else
        {
            builder->first_data_relocation = relocation;
        }
        builder->last_data_relocation = relocation;
        codegen_emit_u32(&builder->buffer, 0);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 0));
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0xb8);
        codegen_emit_u64(&builder->buffer, literal.length);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 1));
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0xb8);
        codegen_emit_u64(&builder->buffer, 1);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 2));
        codegen_emit_u8(&builder->buffer, 0x31);
        codegen_emit_u8(&builder->buffer, 0xc0);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 3));
    }
    break;
    case IR_OPCODE_UNDEFINED:
    {
        codegen_emit_u8(&builder->buffer, 0x31);
        codegen_emit_u8(&builder->buffer, 0xc0);
        x64_emit_store_result(builder, instruction);
    }
    break;
    case IR_OPCODE_UNARY:
    {
        AnalysisType* unary_type = analysis_type_from_id(builder->analysis, instruction->type);
        if (unary_type->kind == ANALYSIS_TYPE_VECTOR)
        {
            return x64_emit_vector_unary(builder, instruction);
        }
        x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
        if (instruction->unary_operation == IR_UNARY_FLOAT_NEGATE)
        {
            AnalysisType* type = analysis_type_from_id(builder->analysis, instruction->type);
            u64 sign = type->as.float_bit_width == 32 ? ((u64)1 << 31) : ((u64)1 << 63);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0xb9);
            codegen_emit_u64(&builder->buffer, sign);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x31);
            codegen_emit_u8(&builder->buffer, 0xc8);
        }
        else if (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE)
        {
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0xf7);
            codegen_emit_u8(&builder->buffer, 0xd8);
        }
        else if (instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT)
        {
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0xf7);
            codegen_emit_u8(&builder->buffer, 0xd0);
        }
        else if (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS || instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS)
        {
            u32 width = unary_type->as.integer.bit_width;
            if (width > 32)
            {
                codegen_emit_u8(&builder->buffer, 0x48);
            }
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS ? 0xbc : 0xbd);
            codegen_emit_u8(&builder->buffer, 0xc0);
            if (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS)
            {
                if (width > 32)
                {
                    codegen_emit_u8(&builder->buffer, 0x48);
                }
                codegen_emit_u8(&builder->buffer, 0x83);
                codegen_emit_u8(&builder->buffer, 0xf0);
                codegen_emit_u8(&builder->buffer, (u8)(width - 1));
            }
        }
        else if (instruction->unary_operation == IR_UNARY_INTEGER_POPULATION_COUNT)
        {
            x64_emit_population_count(&builder->buffer, unary_type->as.integer.bit_width);
        }
        else if (instruction->unary_operation == IR_UNARY_BOOLEAN_NOT)
        {
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x85);
            codegen_emit_u8(&builder->buffer, 0xc0);
            x64_emit_set_condition(builder, 0x94);
        }
        else
        {
            return false;
        }
        x64_emit_store_result(builder, instruction);
    }
    break;
    case IR_OPCODE_BINARY:
    {
        AnalysisType* result_type = analysis_type_from_id(builder->analysis, instruction->type);
        if (result_type->kind == ANALYSIS_TYPE_VECTOR)
        {
            return x64_emit_vector_binary(builder, instruction);
        }
        if (codegen_binary_is_float(instruction->binary_operation))
        {
            return x64_emit_float_binary(builder, instruction);
        }
        return x64_emit_integer_binary(builder, instruction);
    }
    case IR_OPCODE_FUNCTION:
    {
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x8d);
        codegen_emit_u8(&builder->buffer, 0x05);
        x64_call_relocation_add(builder, instruction);
        x64_emit_store_result(builder, instruction);
    }
    break;
    case IR_OPCODE_CALL:
    {
        if (!instruction->operand_count)
        {
            return false;
        }
        AnalysisTypeId callee_type_id = builder->function->values[instruction->operands[0].value].type;
        AnalysisType* callee_type = analysis_type_from_id(builder->analysis, callee_type_id);
        bool indirect = callee_type->kind == ANALYSIS_TYPE_POINTER;
        AnalysisTypeId function_type_id = indirect ? callee_type->as.element_type : callee_type_id;
        AnalysisType* function_type = analysis_type_from_id(builder->analysis, function_type_id);
        if (function_type->kind != ANALYSIS_TYPE_FUNCTION)
        {
            return false;
        }
        u32 call_argument_count = instruction->operand_count - 1;
        AnalysisTypeId* call_argument_types = arena_allocate(builder->arena, AnalysisTypeId, call_argument_count);
        for (u32 index = 0; index < call_argument_count; index += 1)
        {
            call_argument_types[index] = builder->function->values[instruction->operands[index + 1].value].type;
        }
        CodegenAbiSignature signature = codegen_classify_signature_with_arguments_prepared(
            builder->arena, builder->analysis, function_type_id,
            function_type->as.function.is_variadic || builder->abi == CODEGEN_ABI_X86_64_WINDOWS ? call_argument_types : 0, call_argument_count,
            builder->target, false);
        if (!signature.valid)
        {
            return false;
        }
        if (signature.argument_count != instruction->operand_count - 1)
        {
            return false;
        }
        X64Register system_v_registers[] = {
            X64_REGISTER_RDI, X64_REGISTER_RSI, X64_REGISTER_RDX, X64_REGISTER_RCX, X64_REGISTER_R8, X64_REGISTER_R9,
        };
        X64Register windows_registers[] = {
            X64_REGISTER_RCX,
            X64_REGISTER_RDX,
            X64_REGISTER_R8,
            X64_REGISTER_R9,
        };
        AnalysisType* call_result_type =
            instruction->result.value != IR_ID_UNDERLYING_INVALID ? analysis_type_from_id(builder->analysis, instruction->type) : 0;
        if (call_result_type && (codegen_type_is_indirect_value(call_result_type) || codegen_type_is_inline_collection(call_result_type)))
        {
            if (codegen_type_is_indirect_value(call_result_type))
            {
                x64_emit_initialize_aggregate_result(builder, instruction->result);
            }
            if (signature.result.indirect && builder->abi != CODEGEN_ABI_X86_64_WINDOWS)
            {
                X64Register hidden_register = x64_abi_integer_argument_register(builder->abi, signature.indirect_result_register);
                if (codegen_type_is_indirect_value(call_result_type))
                {
                    x64_emit_load(builder, hidden_register, x64_value_displacement_component(instruction->result, 0));
                }
            }
        }
        if (builder->abi != CODEGEN_ABI_X86_64_WINDOWS)
        {
            x64_emit_stack_adjust(builder, signature.stack_size, true);
        }
        if (builder->abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            // Win64 argument registers are all live at the call boundary. Build
            // every caller-owned aggregate copy before loading the hidden result
            // pointer or any indirect argument register, because the copy loop
            // uses RCX as its source and RDX as its scratch register.
            for (u32 index = 0; index < signature.argument_count; index += 1)
            {
                CodegenAbiLocation* location = signature.arguments + index;
                if (!location->indirect)
                {
                    continue;
                }
                IrValueId operand = instruction->operands[index + 1];
                AnalysisType* operand_type = analysis_type_from_id(builder->analysis, builder->function->values[operand.value].type);
                x64_emit_rsp_address(builder, X64_REGISTER_RAX, location->indirect_copy_offset);
                x64_emit_load(builder, X64_REGISTER_RCX, x64_value_displacement_component(operand, 0));
                x64_emit_copy_memory(builder, codegen_type_storage_size(operand_type));
            }
            if (call_result_type && (codegen_type_is_indirect_value(call_result_type) || codegen_type_is_inline_collection(call_result_type)) &&
                signature.result.indirect && codegen_type_is_indirect_value(call_result_type))
            {
                X64Register hidden_register = x64_abi_integer_argument_register(builder->abi, signature.indirect_result_register);
                x64_emit_load(builder, hidden_register, x64_value_displacement_component(instruction->result, 0));
            }
        }
        for (u32 index = 0; index < signature.argument_count; index += 1)
        {
            CodegenAbiLocation* location = signature.arguments + index;
            AnalysisType* operand_type = analysis_type_from_id(builder->analysis, builder->function->values[instruction->operands[index + 1].value].type);
            IrValueId operand = instruction->operands[index + 1];
            if (location->indirect)
            {
                if (builder->abi != CODEGEN_ABI_X86_64_WINDOWS)
                {
                    x64_emit_rsp_address(builder, X64_REGISTER_RAX, location->indirect_copy_offset);
                    x64_emit_load(builder, X64_REGISTER_RCX, x64_value_displacement_component(operand, 0));
                    x64_emit_copy_memory(builder, codegen_type_storage_size(operand_type));
                }
                else
                {
                    x64_emit_rsp_address(builder, X64_REGISTER_RAX, location->indirect_copy_offset);
                }
                CodegenAbiPart* part = location->parts;
                if (part->kind == CODEGEN_ABI_LOCATION_STACK)
                {
                    x64_emit_store_rsp(builder, X64_REGISTER_RAX, part->stack_offset);
                }
                else
                {
                    x64_emit_move_register(builder, x64_abi_integer_argument_register(builder->abi, part->index), X64_REGISTER_RAX);
                }
                continue;
            }
            if (codegen_type_is_indirect_value(operand_type) && location->part_count && location->parts[0].kind == CODEGEN_ABI_LOCATION_STACK)
            {
                x64_emit_rsp_address(builder, X64_REGISTER_RAX, location->parts[0].stack_offset);
                x64_emit_load(builder, X64_REGISTER_RCX, x64_value_displacement_component(operand, 0));
                x64_emit_copy_memory(builder, codegen_type_storage_size(operand_type));
                continue;
            }
            if (location->part_count != 1 && !codegen_type_is_indirect_value(operand_type) && !codegen_type_is_inline_collection(operand_type))
            {
                return false;
            }
            if (codegen_type_is_indirect_value(operand_type) || codegen_type_is_inline_collection(operand_type))
            {
                for (u32 part_index = 0; part_index < location->part_count; part_index += 1)
                {
                    CodegenAbiPart* part = location->parts + part_index;
                    if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                    {
                        x64_emit_load_abi_part(builder, operand, operand_type, part, X64_REGISTER_RAX, part->index);
                    }
                    else
                    {
                        x64_emit_load_abi_part(builder, operand, operand_type, part, X64_REGISTER_RAX, 0);
                        if (part->kind == CODEGEN_ABI_LOCATION_STACK)
                        {
                            x64_emit_store_rsp(builder, X64_REGISTER_RAX, part->stack_offset);
                        }
                        else
                        {
                            x64_emit_move_register(builder, x64_abi_integer_argument_register(builder->abi, part->index), X64_REGISTER_RAX);
                        }
                    }
                }
                continue;
            }
            if (location->kind == CODEGEN_ABI_LOCATION_STACK)
            {
                if (operand_type->kind == ANALYSIS_TYPE_FLOAT)
                {
                    x64_emit_float_load(builder, 0, x64_value_displacement(operand), operand_type->as.float_bit_width);
                    x64_emit_float_store_rsp(builder, 0, location->stack_offset, operand_type->as.float_bit_width);
                }
                else
                {
                    x64_emit_load_value(builder, X64_REGISTER_RAX, operand);
                    x64_emit_store_rsp(builder, X64_REGISTER_RAX, location->stack_offset);
                }
            }
            else if (location->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
            {
                x64_emit_float_load(builder, location->index, x64_value_displacement(operand), operand_type->as.float_bit_width);
                if (builder->abi == CODEGEN_ABI_X86_64_WINDOWS && function_type->as.function.is_variadic)
                {
                    x64_emit_load_value(builder, windows_registers[location->index], operand);
                }
            }
            else
            {
                X64Register* registers = builder->abi == CODEGEN_ABI_X86_64_WINDOWS ? windows_registers : system_v_registers;
                u32 register_count =
                    builder->abi == CODEGEN_ABI_X86_64_WINDOWS ? BUSTER_ARRAY_LENGTH(windows_registers) : BUSTER_ARRAY_LENGTH(system_v_registers);
                if (location->index >= register_count)
                {
                    return false;
                }
                x64_emit_load_value(builder, registers[location->index], operand);
            }
        }
        if (builder->abi == CODEGEN_ABI_X86_64_SYSTEM_V && function_type->as.function.is_variadic)
        {
            u32 vector_register_count = 0;
            for (u32 index = 0; index < signature.argument_count; index += 1)
            {
                CodegenAbiLocation* location = signature.arguments + index;
                for (u32 part = 0; part < location->part_count; part += 1)
                {
                    if (location->parts[part].kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                    {
                        vector_register_count = BUSTER_MAX(vector_register_count, location->parts[part].index + 1);
                    }
                }
            }
            codegen_emit_u8(&builder->buffer, 0xb8);
            codegen_emit_u32(&builder->buffer, vector_register_count);
        }
        if (indirect)
        {
            x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
            codegen_emit_u8(&builder->buffer, 0xff);
            codegen_emit_u8(&builder->buffer, 0xd0);
        }
        else
        {
            codegen_emit_u8(&builder->buffer, 0xe8);
            x64_call_relocation_add(builder, instruction);
        }
        if (builder->abi != CODEGEN_ABI_X86_64_WINDOWS)
        {
            x64_emit_stack_adjust(builder, signature.stack_size, false);
        }
        if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
        {
            AnalysisType* result_type = analysis_type_from_id(builder->analysis, instruction->type);
            if (signature.result.indirect)
            {
                if (builder->abi == CODEGEN_ABI_X86_64_WINDOWS)
                {
                    x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 0));
                }
            }
            else if (codegen_type_is_indirect_value(result_type) || codegen_type_is_inline_collection(result_type))
            {
                X64Register integer_results[] = {
                    X64_REGISTER_RAX,
                    X64_REGISTER_RDX,
                };
                for (u32 part_index = 0; part_index < signature.result.part_count; part_index += 1)
                {
                    CodegenAbiPart* part = signature.result.parts + part_index;
                    x64_emit_store_abi_part(builder, instruction->result, result_type, part, integer_results[part->index], part->index);
                }
            }
            else if (result_type->kind == ANALYSIS_TYPE_FLOAT)
            {
                x64_emit_float_store(builder, 0, x64_value_displacement(instruction->result), result_type->as.float_bit_width);
            }
            else if (!codegen_type_is_indirect_value(result_type))
            {
                x64_emit_store_result(builder, instruction);
            }
            else
            {
                return false;
            }
        }
    }
    break;
    case IR_OPCODE_CAST:
    {
        AnalysisType* source = analysis_type_from_id(builder->analysis, builder->function->values[instruction->operands[0].value].type);
        AnalysisType* target = analysis_type_from_id(builder->analysis, instruction->type);
        u32 source_width = source->kind == ANALYSIS_TYPE_INTEGER ? source->as.integer.bit_width : 64;
        u32 target_width = target->kind == ANALYSIS_TYPE_INTEGER ? target->as.integer.bit_width : 64;
        if (instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND || instruction->conversion_operation == IR_CONVERSION_FLOAT_TRUNCATE)
        {
            x64_emit_float_load(builder, 0, x64_value_displacement(instruction->operands[0]), source->as.float_bit_width);
            codegen_emit_u8(&builder->buffer, instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND ? 0xf3 : 0xf2);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x5a);
            codegen_emit_u8(&builder->buffer, 0xc0);
            x64_emit_float_store(builder, 0, x64_value_displacement(instruction->result), target->as.float_bit_width);
            break;
        }
        if (instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT ||
            instruction->conversion_operation == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT)
        {
            x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
            if (instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT)
            {
                if (source_width == 8)
                {
                    codegen_emit_u8(&builder->buffer, 0x48);
                    codegen_emit_u8(&builder->buffer, 0x0f);
                    codegen_emit_u8(&builder->buffer, 0xbe);
                    codegen_emit_u8(&builder->buffer, 0xc0);
                }
                else if (source_width == 16)
                {
                    codegen_emit_u8(&builder->buffer, 0x48);
                    codegen_emit_u8(&builder->buffer, 0x0f);
                    codegen_emit_u8(&builder->buffer, 0xbf);
                    codegen_emit_u8(&builder->buffer, 0xc0);
                }
                else if (source_width == 32)
                {
                    codegen_emit_u8(&builder->buffer, 0x48);
                    codegen_emit_u8(&builder->buffer, 0x63);
                    codegen_emit_u8(&builder->buffer, 0xc0);
                }
            }
            if (instruction->conversion_operation == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT && source_width == 64)
            {
                return false;
            }
            codegen_emit_u8(&builder->buffer, target->as.float_bit_width == 32 ? 0xf3 : 0xf2);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x2a);
            codegen_emit_u8(&builder->buffer, 0xc0);
            x64_emit_float_store(builder, 0, x64_value_displacement(instruction->result), target->as.float_bit_width);
            break;
        }
        if (instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER)
        {
            x64_emit_float_load(builder, 0, x64_value_displacement(instruction->operands[0]), source->as.float_bit_width);
            codegen_emit_u8(&builder->buffer, source->as.float_bit_width == 32 ? 0xf3 : 0xf2);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x2c);
            codegen_emit_u8(&builder->buffer, 0xc0);
            x64_emit_store_result(builder, instruction);
            break;
        }
        if (instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER)
        {
            return false;
        }
        x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
        if (instruction->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND)
        {
            codegen_emit_u8(&builder->buffer, 0x48);
            if (source_width == 8)
            {
                codegen_emit_u8(&builder->buffer, 0x0f);
                codegen_emit_u8(&builder->buffer, 0xbe);
                codegen_emit_u8(&builder->buffer, 0xc0);
            }
            else if (source_width == 16)
            {
                codegen_emit_u8(&builder->buffer, 0x0f);
                codegen_emit_u8(&builder->buffer, 0xbf);
                codegen_emit_u8(&builder->buffer, 0xc0);
            }
            else if (source_width == 32)
            {
                codegen_emit_u8(&builder->buffer, 0x63);
                codegen_emit_u8(&builder->buffer, 0xc0);
            }
        }
        else if (instruction->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND ||
                 instruction->conversion_operation == IR_CONVERSION_INTEGER_TRUNCATE || instruction->conversion_operation == IR_CONVERSION_INTEGER_REINTERPRET)
        {
            if (target_width == 8)
            {
                codegen_emit_u8(&builder->buffer, 0x0f);
                codegen_emit_u8(&builder->buffer, 0xb6);
                codegen_emit_u8(&builder->buffer, 0xc0);
            }
            else if (target_width == 16)
            {
                codegen_emit_u8(&builder->buffer, 0x0f);
                codegen_emit_u8(&builder->buffer, 0xb7);
                codegen_emit_u8(&builder->buffer, 0xc0);
            }
            else if (target_width == 32)
            {
                codegen_emit_u8(&builder->buffer, 0x89);
                codegen_emit_u8(&builder->buffer, 0xc0);
            }
        }
        else if (instruction->conversion_operation != IR_CONVERSION_IDENTITY)
        {
            return false;
        }
        x64_emit_store_result(builder, instruction);
    }
    break;
    case IR_OPCODE_ADDRESS_OF:
    case IR_OPCODE_DEREFERENCE:
    {
        x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
        x64_emit_store_result(builder, instruction);
    }
    break;
    case IR_OPCODE_ARRAY:
    {
        AnalysisType* type = analysis_type_from_id(builder->analysis, instruction->type);
        AnalysisTypeId element_type_id = type->kind == ANALYSIS_TYPE_ARRAY    ? type->as.array.element_type
                                         : type->kind == ANALYSIS_TYPE_VECTOR ? type->as.vector.element_type
                                                                              : type->as.element_type;
        AnalysisType* element_type = analysis_type_from_id(builder->analysis, element_type_id);
        u32 element_size = codegen_type_storage_size(element_type);
        if (!element_size || !builder->value_storage_offsets[instruction->result.value])
        {
            return false;
        }
        s32 storage = -(s32)builder->value_storage_offsets[instruction->result.value];
        x64_emit_address(builder, X64_REGISTER_RAX, storage);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 0));
        for (u32 index = 0; index < instruction->operand_count; index += 1)
        {
            x64_emit_address(builder, X64_REGISTER_RAX, storage + (s32)(index * element_size));
            if (codegen_type_is_indirect_value(element_type))
            {
                x64_emit_load(builder, X64_REGISTER_RCX, x64_value_displacement_component(instruction->operands[index], 0));
                x64_emit_copy_memory(builder, element_size);
            }
            else
            {
                x64_emit_load_value(builder, X64_REGISTER_RCX, instruction->operands[index]);
                x64_emit_store_memory_rcx(builder, element_size);
            }
        }
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0xb8);
        codegen_emit_u64(&builder->buffer, instruction->operand_count);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 1));
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0xb8);
        codegen_emit_u64(&builder->buffer, element_size);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 2));
        codegen_emit_u8(&builder->buffer, 0x31);
        codegen_emit_u8(&builder->buffer, 0xc0);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 3));
    }
    break;
    case IR_OPCODE_AGGREGATE:
    {
        AnalysisType* type = analysis_type_from_id(builder->analysis, instruction->type);
        AnalysisEntitySemantic* semantic = codegen_type_semantic(builder->analysis, type);
        if (!semantic || instruction->immediate_count != instruction->operand_count || !builder->value_storage_offsets[instruction->result.value])
        {
            return false;
        }
        s32 storage = -(s32)builder->value_storage_offsets[instruction->result.value];
        x64_emit_address(builder, X64_REGISTER_RAX, storage);
        x64_emit_store_result(builder, instruction);
        for (u32 index = 0; index < instruction->operand_count; index += 1)
        {
            u64 field_index = instruction->immediates[index];
            if (field_index >= semantic->field_count)
            {
                return false;
            }
            AnalysisField* field = semantic->fields + field_index;
            AnalysisType* field_type = analysis_type_from_id(builder->analysis, field->type);
            u32 field_size = codegen_type_storage_size(field_type);
            x64_emit_address(builder, X64_REGISTER_RAX, storage + (s32)field->offset);
            if (codegen_type_is_indirect_value(field_type))
            {
                x64_emit_load(builder, X64_REGISTER_RCX, x64_value_displacement_component(instruction->operands[index], 0));
                x64_emit_copy_memory(builder, field_size);
            }
            else
            {
                x64_emit_load_value(builder, X64_REGISTER_RCX, instruction->operands[index]);
                x64_emit_store_memory_rcx(builder, field_size);
            }
        }
    }
    break;
    case IR_OPCODE_REVERSE:
    {
        AnalysisType* type = analysis_type_from_id(builder->analysis, instruction->type);
        u32 reverse_component = type->kind == ANALYSIS_TYPE_RANGE ? 2 : 3;
        for (u32 component = 0; component < X64_VALUE_SLOT_COMPONENT_COUNT; component += 1)
        {
            x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->operands[0], component));
            if (component == reverse_component)
            {
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x83);
                codegen_emit_u8(&builder->buffer, 0xf0);
                codegen_emit_u8(&builder->buffer, 0x01);
            }
            x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, component));
        }
    }
    break;
    case IR_OPCODE_LENGTH:
    {
        AnalysisType* base = analysis_type_from_id(builder->analysis, builder->function->values[instruction->operands[0].value].type);
        if (base->kind != ANALYSIS_TYPE_RANGE)
        {
            x64_emit_collection_component(builder, instruction->operands[0], 1, X64_REGISTER_RAX);
            x64_emit_store_result(builder, instruction);
            break;
        }
        x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->operands[0], 1));
        x64_emit_load(builder, X64_REGISTER_RCX, x64_value_displacement_component(instruction->operands[0], 0));
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x29);
        codegen_emit_u8(&builder->buffer, 0xc8);
        x64_emit_store_result(builder, instruction);
    }
    break;
    case IR_OPCODE_VA_START:
    {
        AnalysisType* function_type = analysis_type_from_id(builder->analysis, builder->function->type);
        u32 integer_register_count = builder->signature.result.indirect ? 1 : 0;
        u32 float_register_count = 0;
        u32 stack_end = 0;
        for (u32 argument = 0; argument < function_type->as.function.argument_count; argument += 1)
        {
            CodegenAbiLocation* location = builder->signature.arguments + argument;
            for (u32 part = 0; part < location->part_count; part += 1)
            {
                CodegenAbiPart* abi_part = location->parts + part;
                if (abi_part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                {
                    float_register_count = BUSTER_MAX(float_register_count, abi_part->index + 1);
                }
                else if (abi_part->kind == CODEGEN_ABI_LOCATION_INTEGER_REGISTER)
                {
                    integer_register_count = BUSTER_MAX(integer_register_count, abi_part->index + 1);
                }
                else
                {
                    stack_end = BUSTER_MAX(stack_end, abi_part->stack_offset + codegen_align_u32(abi_part->size, 8));
                }
            }
        }
        if (builder->abi == CODEGEN_ABI_X86_64_SYSTEM_V)
        {
            u64 offsets = (u64)(integer_register_count * 8) | ((u64)(48 + float_register_count * 16) << 32);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0xb8);
            codegen_emit_u64(&builder->buffer, offsets);
            x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 0));
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x8d);
            codegen_emit_u8(&builder->buffer, 0x85);
            codegen_emit_u32(&builder->buffer, 16 + stack_end);
            x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 1));
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x8d);
            codegen_emit_u8(&builder->buffer, 0x85);
            codegen_emit_u32(&builder->buffer, (u32)builder->va_register_save_displacement);
            x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 2));
        }
        else
        {
            u32 slot = function_type->as.function.argument_count + (builder->signature.result.indirect ? 1 : 0);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x8d);
            codegen_emit_u8(&builder->buffer, 0x85);
            codegen_emit_u32(&builder->buffer, 16 + slot * 8);
            x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 0));
            codegen_emit_u8(&builder->buffer, 0x31);
            codegen_emit_u8(&builder->buffer, 0xc0);
            for (u32 component = 1; component < 3; component += 1)
            {
                x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, component));
            }
        }
        codegen_emit_u8(&builder->buffer, 0x31);
        codegen_emit_u8(&builder->buffer, 0xc0);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 3));
    }
    break;
    case IR_OPCODE_VA_COPY:
    {
        x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
        for (u32 component = 0; component < 4; component += 1)
        {
            x64_emit_load_memory(builder, X64_REGISTER_RDX, X64_REGISTER_RAX, component * 8, 8);
            x64_emit_store(builder, X64_REGISTER_RDX, x64_value_displacement_component(instruction->result, component));
        }
    }
    break;
    case IR_OPCODE_VA_END:
    {
        x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
        codegen_emit_u8(&builder->buffer, 0xba);
        codegen_emit_u32(&builder->buffer, 1);
        x64_emit_store_memory(builder, X64_REGISTER_RAX, 24, X64_REGISTER_RDX, 8);
    }
    break;
    case IR_OPCODE_VA_ARG:
    {
        AnalysisType* type = analysis_type_from_id(builder->analysis, instruction->type);
        u32 size = codegen_type_storage_size(type);
        bool aggregate = codegen_type_is_indirect_value(type);
        bool scalar = type->kind == ANALYSIS_TYPE_INTEGER || type->kind == ANALYSIS_TYPE_FLOAT || type->kind == ANALYSIS_TYPE_BOOL ||
                      type->kind == ANALYSIS_TYPE_POINTER || type->kind == ANALYSIS_TYPE_ENUM;
        if (!size || (!aggregate && (!scalar || size > 8)))
        {
            return false;
        }
        AnalysisAbiValue abi_value =
            analysis_abi_value_classify(builder->arena, builder->analysis, instruction->type, codegen_analysis_abi_convention(builder->abi), false);
        if (!abi_value.part_count)
        {
            return false;
        }
        if (aggregate)
        {
            x64_emit_initialize_aggregate_result(builder, instruction->result);
            x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
            if (builder->abi == CODEGEN_ABI_X86_64_WINDOWS)
            {
                x64_emit_load_memory(builder, X64_REGISTER_RDX, X64_REGISTER_RAX, 0, 8);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x83);
                codegen_emit_u8(&builder->buffer, 0xc2);
                codegen_emit_u8(&builder->buffer, 8);
                x64_emit_store_memory(builder, X64_REGISTER_RAX, 0, X64_REGISTER_RDX, 8);
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x83);
                codegen_emit_u8(&builder->buffer, 0xea);
                codegen_emit_u8(&builder->buffer, 8);
                if (abi_value.indirect)
                {
                    x64_emit_load_memory(builder, X64_REGISTER_RCX, X64_REGISTER_RDX, 0, 8);
                }
                else
                {
                    x64_emit_move_register(builder, X64_REGISTER_RCX, X64_REGISTER_RDX);
                }
                x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 0));
                x64_emit_copy_memory(builder, size);
                break;
            }

            u32 integer_parts = 0;
            u32 float_parts = 0;
            bool register_value = abi_value.parts[0].location != ANALYSIS_ABI_LOCATION_STACK;
            for (u32 part = 0; part < abi_value.part_count; part += 1)
            {
                if (abi_value.parts[part].abi_class == ANALYSIS_ABI_CLASS_FLOAT)
                {
                    float_parts += 1;
                }
                else if (abi_value.parts[part].abi_class == ANALYSIS_ABI_CLASS_INTEGER || abi_value.parts[part].abi_class == ANALYSIS_ABI_CLASS_POINTER)
                {
                    integer_parts += 1;
                }
                else
                {
                    register_value = false;
                }
            }
            u32 overflow_patches[2] = {0};
            u32 overflow_patch_count = 0;
            if (register_value && integer_parts)
            {
                x64_emit_load_memory(builder, X64_REGISTER_RCX, X64_REGISTER_RAX, 0, 4);
                codegen_emit_u8(&builder->buffer, 0x81);
                codegen_emit_u8(&builder->buffer, 0xf9);
                codegen_emit_u32(&builder->buffer, 48 - integer_parts * 8);
                codegen_emit_u8(&builder->buffer, 0x0f);
                codegen_emit_u8(&builder->buffer, 0x87);
                overflow_patches[overflow_patch_count++] = (u32)builder->buffer.count;
                codegen_emit_u32(&builder->buffer, 0);
            }
            if (register_value && float_parts)
            {
                x64_emit_load_memory(builder, X64_REGISTER_RCX, X64_REGISTER_RAX, 4, 4);
                codegen_emit_u8(&builder->buffer, 0x81);
                codegen_emit_u8(&builder->buffer, 0xf9);
                codegen_emit_u32(&builder->buffer, 176 - float_parts * 16);
                codegen_emit_u8(&builder->buffer, 0x0f);
                codegen_emit_u8(&builder->buffer, 0x87);
                overflow_patches[overflow_patch_count++] = (u32)builder->buffer.count;
                codegen_emit_u32(&builder->buffer, 0);
            }
            if (register_value)
            {
                for (u32 part = 0; part < abi_value.part_count; part += 1)
                {
                    AnalysisAbiPart* abi_part = abi_value.parts + part;
                    u32 offset = abi_part->abi_class == ANALYSIS_ABI_CLASS_FLOAT ? 4 : 0;
                    u32 increment = abi_part->abi_class == ANALYSIS_ABI_CLASS_FLOAT ? 16 : 8;
                    x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
                    x64_emit_load_memory(builder, X64_REGISTER_RCX, X64_REGISTER_RAX, offset, 4);
                    x64_emit_load_memory(builder, X64_REGISTER_RDX, X64_REGISTER_RAX, 16, 8);
                    codegen_emit_u8(&builder->buffer, 0x48);
                    codegen_emit_u8(&builder->buffer, 0x01);
                    codegen_emit_u8(&builder->buffer, 0xca);
                    x64_emit_load_memory(builder, X64_REGISTER_R8, X64_REGISTER_RDX, 0, abi_part->size);
                    x64_emit_load(builder, X64_REGISTER_R11, x64_value_displacement_component(instruction->result, 0));
                    x64_emit_store_memory(builder, X64_REGISTER_R11, abi_part->value_offset, X64_REGISTER_R8, abi_part->size);
                    codegen_emit_u8(&builder->buffer, 0x83);
                    codegen_emit_u8(&builder->buffer, 0x80);
                    codegen_emit_u32(&builder->buffer, offset);
                    codegen_emit_u8(&builder->buffer, (u8)increment);
                }
                codegen_emit_u8(&builder->buffer, 0xe9);
            }
            u32 end_patch = register_value ? (u32)builder->buffer.count : 0;
            if (register_value)
            {
                codegen_emit_u32(&builder->buffer, 0);
            }
            u32 overflow_offset = (u32)builder->buffer.count;
            for (u32 patch = 0; patch < overflow_patch_count; patch += 1)
            {
                s32 displacement = (s32)(overflow_offset - (overflow_patches[patch] + 4));
                memcpy(builder->buffer.bytes + overflow_patches[patch], &displacement, sizeof(displacement));
            }
            x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
            x64_emit_load_memory(builder, X64_REGISTER_RDX, X64_REGISTER_RAX, 8, 8);
            u32 alignment = (u32)BUSTER_MAX(type->layout.alignment, 8);
            if (alignment > 8)
            {
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x83);
                codegen_emit_u8(&builder->buffer, 0xc2);
                codegen_emit_u8(&builder->buffer, (u8)(alignment - 1));
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x83);
                codegen_emit_u8(&builder->buffer, 0xe2);
                codegen_emit_u8(&builder->buffer, (u8)(0 - alignment));
            }
            x64_emit_move_register(builder, X64_REGISTER_RCX, X64_REGISTER_RDX);
            u32 stack_size = codegen_align_u32(size, 8);
            if (stack_size <= 127)
            {
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x83);
                codegen_emit_u8(&builder->buffer, 0xc2);
                codegen_emit_u8(&builder->buffer, (u8)stack_size);
            }
            else
            {
                codegen_emit_u8(&builder->buffer, 0x48);
                codegen_emit_u8(&builder->buffer, 0x81);
                codegen_emit_u8(&builder->buffer, 0xc2);
                codegen_emit_u32(&builder->buffer, stack_size);
            }
            x64_emit_store_memory(builder, X64_REGISTER_RAX, 8, X64_REGISTER_RDX, 8);
            x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 0));
            x64_emit_copy_memory(builder, size);
            if (register_value)
            {
                u32 end_offset = (u32)builder->buffer.count;
                s32 displacement = (s32)(end_offset - (end_patch + 4));
                memcpy(builder->buffer.bytes + end_patch, &displacement, sizeof(displacement));
            }
            break;
        }
        x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
        if (builder->abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            x64_emit_load_memory(builder, X64_REGISTER_RDX, X64_REGISTER_RAX, 0, 8);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xc2);
            codegen_emit_u8(&builder->buffer, 8);
            x64_emit_store_memory(builder, X64_REGISTER_RAX, 0, X64_REGISTER_RDX, 8);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xea);
            codegen_emit_u8(&builder->buffer, 8);
        }
        else
        {
            bool floating = type->kind == ANALYSIS_TYPE_FLOAT;
            u32 offset = floating ? 4 : 0;
            u32 limit = floating ? 160 : 40;
            u32 increment = floating ? 16 : 8;
            x64_emit_load_memory(builder, X64_REGISTER_RCX, X64_REGISTER_RAX, offset, 4);
            codegen_emit_u8(&builder->buffer, 0x81);
            codegen_emit_u8(&builder->buffer, 0xf9);
            codegen_emit_u32(&builder->buffer, limit);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x87);
            u32 overflow_patch = (u32)builder->buffer.count;
            codegen_emit_u32(&builder->buffer, 0);
            x64_emit_load_memory(builder, X64_REGISTER_RDX, X64_REGISTER_RAX, 16, 8);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x01);
            codegen_emit_u8(&builder->buffer, 0xca);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, (u8)(offset ? 0x40 : 0x00));
            if (offset)
            {
                codegen_emit_u8(&builder->buffer, (u8)offset);
            }
            codegen_emit_u8(&builder->buffer, (u8)increment);
            codegen_emit_u8(&builder->buffer, 0xe9);
            u32 end_patch = (u32)builder->buffer.count;
            codegen_emit_u32(&builder->buffer, 0);
            u32 overflow_offset = (u32)builder->buffer.count;
            x64_emit_load_memory(builder, X64_REGISTER_RDX, X64_REGISTER_RAX, 8, 8);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xc2);
            codegen_emit_u8(&builder->buffer, 8);
            x64_emit_store_memory(builder, X64_REGISTER_RAX, 8, X64_REGISTER_RDX, 8);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x83);
            codegen_emit_u8(&builder->buffer, 0xea);
            codegen_emit_u8(&builder->buffer, 8);
            u32 end_offset = (u32)builder->buffer.count;
            s32 overflow_displacement = (s32)(overflow_offset - (overflow_patch + 4));
            s32 end_displacement = (s32)(end_offset - (end_patch + 4));
            memcpy(builder->buffer.bytes + overflow_patch, &overflow_displacement, sizeof(overflow_displacement));
            memcpy(builder->buffer.bytes + end_patch, &end_displacement, sizeof(end_displacement));
        }
        x64_emit_load_memory(builder, X64_REGISTER_RAX, X64_REGISTER_RDX, 0, size);
        x64_emit_store_result(builder, instruction);
    }
    break;
    case IR_OPCODE_INDEX:
    {
        AnalysisType* base = analysis_type_from_id(builder->analysis, builder->function->values[instruction->operands[0].value].type);
        if (base->kind != ANALYSIS_TYPE_RANGE)
        {
            x64_emit_load_value(builder, X64_REGISTER_RCX, instruction->operands[1]);
            x64_emit_collection_component(builder, instruction->operands[0], 1, X64_REGISTER_RDX);
            x64_emit_collection_component(builder, instruction->operands[0], 3, X64_REGISTER_RAX);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x85);
            codegen_emit_u8(&builder->buffer, 0xc0);
            codegen_emit_u8(&builder->buffer, 0x74);
            u32 forward_jump = (u32)builder->buffer.count;
            codegen_emit_u8(&builder->buffer, 0);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0xff);
            codegen_emit_u8(&builder->buffer, 0xca);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x29);
            codegen_emit_u8(&builder->buffer, 0xca);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x89);
            codegen_emit_u8(&builder->buffer, 0xd1);
            u32 forward_path = (u32)builder->buffer.count;
            u32 forward_delta = forward_path - forward_jump - 1;
            if (forward_delta > UINT8_MAX)
            {
                return false;
            }
            builder->buffer.bytes[forward_jump] = (u8)forward_delta;
            x64_emit_collection_component(builder, instruction->operands[0], 2, X64_REGISTER_RDX);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0xaf);
            codegen_emit_u8(&builder->buffer, 0xca);
            x64_emit_collection_component(builder, instruction->operands[0], 0, X64_REGISTER_RAX);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x01);
            codegen_emit_u8(&builder->buffer, 0xc8);
            if (builder->function->values[instruction->result.value].category == IR_VALUE_PLACE)
            {
                x64_emit_store_result(builder, instruction);
            }
            else
            {
                AnalysisType* result_type = analysis_type_from_id(builder->analysis, instruction->type);
                if (codegen_type_is_indirect_value(result_type))
                {
                    x64_emit_store_result(builder, instruction);
                }
                else
                {
                    x64_emit_load_memory_rax(builder, codegen_type_storage_size(result_type));
                    x64_emit_store_result(builder, instruction);
                }
            }
            break;
        }
        x64_emit_load_value(builder, X64_REGISTER_RCX, instruction->operands[1]);
        x64_emit_load(builder, X64_REGISTER_RDX, x64_value_displacement_component(instruction->operands[0], 1));
        x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->operands[0], 0));
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x29);
        codegen_emit_u8(&builder->buffer, 0xc2);
        x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->operands[0], 2));
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x85);
        codegen_emit_u8(&builder->buffer, 0xc0);
        codegen_emit_u8(&builder->buffer, 0x74);
        u32 forward_jump = (u32)builder->buffer.count;
        codegen_emit_u8(&builder->buffer, 0);
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0xff);
        codegen_emit_u8(&builder->buffer, 0xca);
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x29);
        codegen_emit_u8(&builder->buffer, 0xca);
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0xd1);
        u32 forward_path = (u32)builder->buffer.count;
        u32 forward_delta = forward_path - forward_jump - 1;
        if (forward_delta > UINT8_MAX)
        {
            return false;
        }
        builder->buffer.bytes[forward_jump] = (u8)forward_delta;
        x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->operands[0], 0));
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x01);
        codegen_emit_u8(&builder->buffer, 0xc8);
        x64_emit_store_result(builder, instruction);
    }
    break;
    case IR_OPCODE_SLICE:
    {
        if (instruction->immediate_count != 2)
        {
            return false;
        }
        bool has_start = instruction->immediates[0] != 0;
        bool has_end = instruction->immediates[1] != 0;
        u32 operand_index = 1;
        if (has_start)
        {
            x64_emit_load_value(builder, X64_REGISTER_RCX, instruction->operands[operand_index++]);
        }
        else
        {
            codegen_emit_u8(&builder->buffer, 0x31);
            codegen_emit_u8(&builder->buffer, 0xc9);
        }
        if (has_end)
        {
            x64_emit_load_value(builder, X64_REGISTER_RDX, instruction->operands[operand_index]);
        }
        else
        {
            x64_emit_collection_component(builder, instruction->operands[0], 1, X64_REGISTER_RDX);
        }
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x29);
        codegen_emit_u8(&builder->buffer, 0xca);
        x64_emit_collection_component(builder, instruction->operands[0], 2, X64_REGISTER_RAX);
        codegen_emit_u8(&builder->buffer, 0x49);
        codegen_emit_u8(&builder->buffer, 0x89);
        codegen_emit_u8(&builder->buffer, 0xc0);
        codegen_emit_u8(&builder->buffer, 0x49);
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0xaf);
        codegen_emit_u8(&builder->buffer, 0xc8);
        x64_emit_collection_component(builder, instruction->operands[0], 0, X64_REGISTER_RAX);
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x01);
        codegen_emit_u8(&builder->buffer, 0xc8);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 0));
        x64_emit_store(builder, X64_REGISTER_RDX, x64_value_displacement_component(instruction->result, 1));
        x64_emit_collection_component(builder, instruction->operands[0], 2, X64_REGISTER_RAX);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 2));
        codegen_emit_u8(&builder->buffer, 0x31);
        codegen_emit_u8(&builder->buffer, 0xc0);
        x64_emit_store(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->result, 3));
    }
    break;
    case IR_OPCODE_FIELD:
    {
        AnalysisType* base_type = analysis_type_from_id(builder->analysis, builder->function->values[instruction->operands[0].value].type);
        AnalysisEntitySemantic* semantic = codegen_type_semantic(builder->analysis, base_type);
        if (!semantic || instruction->immediate_count != 1 || instruction->immediates[0] >= semantic->field_count)
        {
            return false;
        }
        AnalysisField* field = semantic->fields + instruction->immediates[0];
        x64_emit_load(builder, X64_REGISTER_RAX, x64_value_displacement_component(instruction->operands[0], 0));
        if (field->offset)
        {
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x05);
            codegen_emit_u32(&builder->buffer, (u32)field->offset);
        }
        if (builder->function->values[instruction->result.value].category == IR_VALUE_PLACE)
        {
            x64_emit_store_result(builder, instruction);
        }
        else
        {
            AnalysisType* result_type = analysis_type_from_id(builder->analysis, instruction->type);
            if (!codegen_type_is_indirect_value(result_type))
            {
                x64_emit_load_memory_rax(builder, codegen_type_storage_size(result_type));
            }
            x64_emit_store_result(builder, instruction);
        }
    }
    break;
    case IR_OPCODE_BRANCH:
    {
        x64_emit_edge_copies(builder, block, instruction->targets[0]);
        x64_emit_jump(builder, instruction->targets[0]);
    }
    break;
    case IR_OPCODE_BRANCH_IF:
    {
        x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
        codegen_emit_u8(&builder->buffer, 0x48);
        codegen_emit_u8(&builder->buffer, 0x85);
        codegen_emit_u8(&builder->buffer, 0xc0);
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0x84);
        u32 false_jump = (u32)builder->buffer.count;
        codegen_emit_u32(&builder->buffer, 0);
        x64_emit_edge_copies(builder, block, instruction->targets[0]);
        x64_emit_jump(builder, instruction->targets[0]);
        u32 false_path = (u32)builder->buffer.count;
        s64 false_displacement = (s64)false_path - (s64)(false_jump + 4);
        memcpy(builder->buffer.bytes + false_jump, &false_displacement, 4);
        x64_emit_edge_copies(builder, block, instruction->targets[1]);
        x64_emit_jump(builder, instruction->targets[1]);
    }
    break;
    case IR_OPCODE_SWITCH:
    {
        if (!instruction->operand_count || instruction->target_count != instruction->immediate_count + 1)
        {
            return false;
        }
        for (u32 index = 0; index < instruction->immediate_count; index += 1)
        {
            x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
            x64_emit_constant_register(builder, X64_REGISTER_RCX, instruction->immediates[index]);
            codegen_emit_u8(&builder->buffer, 0x48);
            codegen_emit_u8(&builder->buffer, 0x39);
            codegen_emit_u8(&builder->buffer, 0xc8);
            codegen_emit_u8(&builder->buffer, 0x0f);
            codegen_emit_u8(&builder->buffer, 0x85);
            u32 next_case = (u32)builder->buffer.count;
            codegen_emit_u32(&builder->buffer, 0);
            x64_emit_edge_copies(builder, block, instruction->targets[index]);
            x64_emit_jump(builder, instruction->targets[index]);
            s64 displacement = (s64)builder->buffer.count - (s64)(next_case + 4);
            s32 displacement_32 = (s32)displacement;
            memcpy(builder->buffer.bytes + next_case, &displacement_32, sizeof(displacement_32));
        }
        IrBlockId default_target = instruction->targets[instruction->immediate_count];
        x64_emit_edge_copies(builder, block, default_target);
        x64_emit_jump(builder, default_target);
    }
    break;
    case IR_OPCODE_RETURN:
    {
        if (instruction->operand_count)
        {
            AnalysisType* return_type = analysis_type_from_id(builder->analysis, builder->function->values[instruction->operands[0].value].type);
            IrValueId return_value = instruction->operands[0];
            if (builder->signature.result.indirect)
            {
                x64_emit_load(builder, X64_REGISTER_RAX, builder->hidden_result_displacement);
                x64_emit_load(builder, X64_REGISTER_RCX, x64_value_displacement_component(return_value, 0));
                x64_emit_copy_memory(builder, codegen_type_storage_size(return_type));
                if (builder->abi == CODEGEN_ABI_X86_64_SYSTEM_V || builder->abi == CODEGEN_ABI_X86_64_WINDOWS)
                {
                    x64_emit_load(builder, X64_REGISTER_RAX, builder->hidden_result_displacement);
                }
            }
            else if (codegen_type_is_indirect_value(return_type) || codegen_type_is_inline_collection(return_type))
            {
                X64Register integer_results[] = {
                    X64_REGISTER_RAX,
                    X64_REGISTER_RDX,
                };
                for (u32 part_index = 0; part_index < builder->signature.result.part_count; part_index += 1)
                {
                    CodegenAbiPart* part = builder->signature.result.parts + part_index;
                    x64_emit_load_abi_part(builder, return_value, return_type, part, integer_results[part->index], part->index);
                }
            }
            else if (return_type->kind == ANALYSIS_TYPE_FLOAT)
            {
                x64_emit_float_load(builder, 0, x64_value_displacement(instruction->operands[0]), return_type->as.float_bit_width);
            }
            else
            {
                x64_emit_load_value(builder, X64_REGISTER_RAX, instruction->operands[0]);
            }
        }
        x64_emit_return(builder);
    }
    break;
    case IR_OPCODE_DEBUG_TRAP:
        codegen_emit_u8(&builder->buffer, 0xcc);
        break;
    case IR_OPCODE_UNREACHABLE:
        codegen_emit_u8(&builder->buffer, 0x0f);
        codegen_emit_u8(&builder->buffer, 0x0b);
        break;
    default:
        return false;
    }
    return true;
}

// The declaration line of a Buster function. An IR source range carries the
// offset alone, and these emitters are handed an analysis rather than a
// program, so the line comes back from the entity the range was taken from.
BUSTER_GLOBAL_LOCAL ParserSourceRange codegen_analysis_declaration_range(AnalysisResult* analysis, IrFunction* function)
{
    if (!analysis || function->entity.index.value >= analysis->module.entity_count)
    {
        return (ParserSourceRange){0};
    }
    return analysis->module.entities[function->entity.index.value].range;
}

void codegen_record_line(CodegenLineEntry* entries, u32* count, u32 capacity, u32 code_offset, u32 source, u32 line, u32 column)
{
    if (!entries || !line || *count >= capacity)
    {
        return;
    }
    if (*count)
    {
        CodegenLineEntry* last = entries + (*count - 1);
        if (last->code_offset == code_offset || (last->source == source && last->line == line && last->column == column))
        {
            return;
        }
    }
    entries[*count] = (CodegenLineEntry){
        .code_offset = code_offset,
        .source = source,
        .line = line,
        .column = column,
    };
    *count += 1;
}

BUSTER_GLOBAL_LOCAL DebugRegister codegen_debug_register_for_value(Target target, bool vector, u32 index)
{
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        return vector ? (DebugRegister)((u32)DEBUG_REGISTER_X86_XMM0 + 3 + index) : (DebugRegister)((u32)DEBUG_REGISTER_X86_R10 + index);
    }
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        return vector ? (DebugRegister)((u32)DEBUG_REGISTER_AARCH64_V0 + 2 + index) : (DebugRegister)((u32)DEBUG_REGISTER_AARCH64_X0 + 9 + index);
    }
    return DEBUG_REGISTER_NONE;
}

// Debug locations use the frame pointer as their common base.  x86-64 storage
// offsets are distances below RBP and need their sign changed; AArch64
// codegen stores values relative to the final SP and keeps X29 at the
// pre-allocation SP, so translate those offsets back across the frame here.
s32 codegen_debug_frame_offset(u32 offset, Target target, bool negative_offsets, u32 frame_size)
{
    s64 result = offset > INT32_MAX ? INT32_MAX : (s64)offset;
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        result -= frame_size;
    }
    else if (negative_offsets)
    {
        result = -result;
    }
    if (result < INT32_MIN)
    {
        result = INT32_MIN;
    }
    else if (result > INT32_MAX)
    {
        result = INT32_MAX;
    }
    return (s32)result;
}

BUSTER_GLOBAL_LOCAL DebugLocation codegen_debug_analysis_value_location(IrFunction* function, AnalysisResult* analysis, IrValueId value,
                                                                         u8* value_registers, u8* vector_registers, u32* value_storage_offsets,
                                                                         Target target, bool negative_offsets, u32 frame_size)
{
    DebugLocation result = {
        .kind = DEBUG_LOCATION_UNAVAILABLE,
    };
    if (!function || value.value >= function->value_count)
    {
        return result;
    }
    AnalysisType* type = analysis ? analysis_type_from_id(analysis, function->values[value.value].type) : 0;
    bool vector = type && type->kind == ANALYSIS_TYPE_VECTOR && type->layout.size <= 16;
    if (vector && vector_registers && vector_registers[value.value] != CODEGEN_REGISTER_UNALLOCATED)
    {
        return (DebugLocation){
            .kind = DEBUG_LOCATION_REGISTER,
            .reg = codegen_debug_register_for_value(target, true, vector_registers[value.value]),
        };
    }
    if (!vector && value_registers && value_registers[value.value] != CODEGEN_REGISTER_UNALLOCATED)
    {
        return (DebugLocation){
            .kind = DEBUG_LOCATION_REGISTER,
            .reg = codegen_debug_register_for_value(target, false, value_registers[value.value]),
        };
    }
    u64 offset = value_storage_offsets && value_storage_offsets[value.value]
                     ? value_storage_offsets[value.value]
                     : target.cpu_arch == CPU_ARCH_X86_64 ? (u64)value.value * X64_VALUE_SLOT_SIZE + 8
                                                          : (u64)value.value * A64_VALUE_SLOT_SIZE;
    return (DebugLocation){
        .kind = DEBUG_LOCATION_FRAME,
        .frame_offset = codegen_debug_frame_offset(offset > UINT32_MAX ? UINT32_MAX : (u32)offset, target, negative_offsets, frame_size),
    };
}

BUSTER_GLOBAL_LOCAL void codegen_debug_location_append(CodegenFunction* result, u32 capacity, IrSymbolId symbol, IrLocalId local, u32 start, u32 end,
                                                        DebugLocation location)
{
    if (!result || result->debug_location_count >= capacity || end <= start)
    {
        return;
    }
    result->debug_locations[result->debug_location_count++] = (DebugLocationSeed){
        .function_symbol = symbol,
        .local = local,
        .start = start,
        .end = end,
        .location = location,
    };
}

BUSTER_GLOBAL_LOCAL void codegen_record_analysis_locations(Arena* arena, CodegenFunction* result, AnalysisResult* analysis, IrFunction* function,
                                                             u32* local_offsets, u32* value_storage_offsets, u8* value_registers, u8* vector_registers,
                                                             u32* block_offsets, Target target, bool negative_offsets, u32 frame_size)
{
    if (!arena || !result || !function || !result->code.length || !function->local_count)
    {
        return;
    }
    u64 capacity_64 = (u64)function->local_count * ((u64)function->block_count + 1);
    if (capacity_64 > UINT32_MAX)
    {
        result->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    u32 capacity = (u32)capacity_64;
    result->debug_locations = arena_allocate(arena, DebugLocationSeed, capacity ? capacity : 1);
    result->debug_location_count = 0;
    for (u32 local_index = 0; local_index < function->local_count; local_index += 1)
    {
        IrLocalId local = {.value = local_index};
        bool emitted = false;
        if (function->local_uses_memory && function->local_places && function->local_uses_memory[local_index] &&
            function->local_places[local_index].value != IR_ID_UNDERLYING_INVALID && local_offsets)
        {
            u32 offset = local_offsets[local_index];
            codegen_debug_location_append(result, capacity, function->symbol, local, 0, (u32)result->code.length,
                                          (DebugLocation){
                                              .kind = DEBUG_LOCATION_FRAME,
                                              .frame_offset = codegen_debug_frame_offset(offset, target, negative_offsets, frame_size),
                                          });
            emitted = true;
        }
        if (!emitted && block_offsets)
        {
            for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
            {
                IrBlock* block = function->blocks + block_index;
                if (!block->local_values || local_index >= function->local_count)
                {
                    continue;
                }
                IrValueId value = block->local_values[local_index];
                if (value.value == IR_ID_UNDERLYING_INVALID || value.value >= function->value_count)
                {
                    continue;
                }
                u32 start = block_offsets[block_index];
                u32 end = block_index + 1 < function->block_count ? block_offsets[block_index + 1] : (u32)result->code.length;
                start = BUSTER_MIN(start, (u32)result->code.length);
                end = BUSTER_MIN(end, (u32)result->code.length);
                DebugLocation location = codegen_debug_analysis_value_location(function, analysis, value, value_registers, vector_registers,
                                                                                value_storage_offsets, target, negative_offsets, frame_size);
                codegen_debug_location_append(result, capacity, function->symbol, local, start, end, location);
                emitted |= end > start;
            }
        }
        if (!emitted)
        {
            codegen_debug_location_append(result, capacity, function->symbol, local, 0, (u32)result->code.length,
                                          (DebugLocation){
                                              .kind = DEBUG_LOCATION_UNAVAILABLE,
                                          });
        }
    }
}

BUSTER_GLOBAL_LOCAL DebugLocation codegen_debug_canonical_value_location(IrValueId value, IrFunction* function, u32* value_offsets, Target target,
                                                                         u32 frame_size, s32 frame_base_offset)
{
    if (!function || !value_offsets || value.value >= function->value_count)
    {
        return (DebugLocation){
            .kind = DEBUG_LOCATION_UNAVAILABLE,
        };
    }
    u32 offset = value_offsets[value.value];
    s32 frame_offset = target.cpu_arch == CPU_ARCH_X86_64 ? (s32)((s64)frame_base_offset - (s64)offset)
                                                          : codegen_debug_frame_offset(offset, target, true, frame_size);
    return (DebugLocation){
        .kind = DEBUG_LOCATION_FRAME,
        .frame_offset = frame_offset,
    };
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_location_append(CodegenModule* result, u32 capacity, IrSymbolId symbol, IrLocalId local, u32 start, u32 end,
                                                            DebugLocation location)
{
    if (!result || result->debug_location_count >= capacity || end <= start)
    {
        return;
    }
    result->debug_locations[result->debug_location_count++] = (DebugLocationSeed){
        .function_symbol = symbol,
        .local = local,
        .start = start,
        .end = end,
        .location = location,
    };
}

BUSTER_GLOBAL_LOCAL void codegen_record_canonical_locations(CodegenModule* result, IrFunction* function, u32* value_offsets, u32* block_offsets,
                                                             u32 function_start, u32 function_end, Target target, u32 frame_size,
                                                             s32 frame_base_offset, u32 capacity)
{
    if (!result || !function || !function->debug_local_count || !result->debug_locations)
    {
        return;
    }
    TemporalArena temporary = scratch_begin(0, 0);
    IrValueId* local_places = arena_allocate(temporary.arena, IrValueId, function->local_count);
    memset(local_places, 0xff, sizeof(*local_places) * function->local_count);
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->result.value < function->value_count && instruction->canonical_local.value < function->local_count &&
            (instruction->opcode == IR_OPCODE_LOCAL || instruction->opcode == IR_OPCODE_ARGUMENT) &&
            local_places[instruction->canonical_local.value].value == IR_ID_UNDERLYING_INVALID)
        {
            local_places[instruction->canonical_local.value] = instruction->result;
        }
    }
    for (u32 local_index = 0; local_index < function->debug_local_count; local_index += 1)
    {
        IrDebugLocal* local = function->debug_locals + local_index;
        if (local->id.value == IR_ID_UNDERLYING_INVALID)
        {
            continue;
        }
        bool emitted = false;
        IrValueId place = local->id.value < function->local_count ? local_places[local->id.value] : IR_VALUE_ID_INVALID;
        if (place.value == IR_ID_UNDERLYING_INVALID && local->id.value >= function->local_count)
        {
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                if (instruction->result.value < function->value_count && instruction->canonical_local.value == local->id.value &&
                    (instruction->opcode == IR_OPCODE_LOCAL || instruction->opcode == IR_OPCODE_ARGUMENT))
                {
                    place = instruction->result;
                    break;
                }
            }
        }
        if (place.value != IR_ID_UNDERLYING_INVALID)
        {
            codegen_canonical_location_append(result, capacity, function->symbol, local->id, function_start, function_end,
                                              codegen_debug_canonical_value_location(place, function, value_offsets, target, frame_size, frame_base_offset));
            emitted = true;
        }
        if (!emitted && block_offsets)
        {
            for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
            {
                IrBlock* block = function->blocks + block_index;
                IrValueId value = IR_VALUE_ID_INVALID;
                if (block->local_values && local->id.value < function->local_count)
                {
                    value = block->local_values[local->id.value];
                }
                if (value.value == IR_ID_UNDERLYING_INVALID)
                {
                    for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
                    {
                        if (parameter->canonical_local.value == local->id.value)
                        {
                            value = parameter->value;
                            break;
                        }
                    }
                }
                if (value.value == IR_ID_UNDERLYING_INVALID)
                {
                    continue;
                }
                u32 start = BUSTER_MAX(block_offsets[block_index], function_start);
                u32 end = block_index + 1 < function->block_count ? block_offsets[block_index + 1] : function_end;
                end = BUSTER_MIN(end, function_end);
                codegen_canonical_location_append(result, capacity, function->symbol, local->id, start, end,
                                                  codegen_debug_canonical_value_location(value, function, value_offsets, target, frame_size, frame_base_offset));
                emitted |= end > start;
            }
        }
        if (!emitted)
        {
            codegen_canonical_location_append(result, capacity, function->symbol, local->id, function_start, function_end,
                                              (DebugLocation){
                                                  .kind = DEBUG_LOCATION_UNAVAILABLE,
                                              });
        }
    }
    scratch_end(temporary);
}

BUSTER_GLOBAL_LOCAL CodegenFunction codegen_generate_x86_64(Arena* arena, AnalysisResult* analysis, IrFunction* function, Target target, bool record_lines)
{
    CodegenAbi abi = codegen_abi_for_target(target);
    CodegenFunction result = {
        .abi = abi,
        .symbol = function->symbol,
    };
    CodegenAbiSignature signature = codegen_classify_signature_for_target(arena, analysis, function->type, target);
    if (!signature.valid)
    {
        result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
        return result;
    }
    u32 maximum_parameters = 0;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        maximum_parameters = BUSTER_MAX(maximum_parameters, function->blocks[block_index].parameter_count);
    }
    u32 value_bytes = function->value_count * X64_VALUE_SLOT_SIZE;
    u32 temporary_bytes = maximum_parameters * X64_VALUE_SLOT_SIZE;
    u32* local_storage_offsets = arena_allocate(arena, u32, function->local_count);
    u32* local_storage_sizes = arena_allocate(arena, u32, function->local_count);
    for (u32 index = 0; index < function->local_count; index += 1)
    {
        local_storage_sizes[index] = X64_VALUE_SLOT_SIZE;
    }
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction* instruction = function->instructions + index;
        if (instruction->opcode == IR_OPCODE_LOCAL && instruction->local.value < function->local_count)
        {
            AnalysisType* type = analysis_type_from_id(analysis, instruction->type);
            u32 size = codegen_type_is_inline_collection(type) ? X64_VALUE_SLOT_SIZE : codegen_type_storage_size(type);
            local_storage_sizes[instruction->local.value] = BUSTER_MAX(size, 8);
        }
    }
    u32 frame_cursor = value_bytes + temporary_bytes;
    for (u32 index = 0; index < function->local_count; index += 1)
    {
        frame_cursor = codegen_align_u32(frame_cursor, 8);
        frame_cursor += local_storage_sizes[index];
        local_storage_offsets[index] = frame_cursor;
    }
    u32* value_storage_offsets = arena_allocate(arena, u32, function->value_count);
    for (u32 index = 0; index < function->value_count; index += 1)
    {
        AnalysisType* type = analysis_type_from_id(analysis, function->values[index].type);
        if (!codegen_type_is_indirect_value(type))
        {
            continue;
        }
        u32 size = codegen_type_storage_size(type);
        u32 alignment = BUSTER_MAX(type->layout.alignment, 1);
        if (type->kind == ANALYSIS_TYPE_VECTOR)
        {
            alignment = BUSTER_MIN(alignment, 16);
        }
        if (!size || alignment > 16)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        frame_cursor = codegen_align_u32(frame_cursor, alignment);
        frame_cursor += size;
        value_storage_offsets[index] = frame_cursor;
    }
    s32 hidden_result_displacement = 0;
    if (signature.result.indirect)
    {
        frame_cursor = codegen_align_u32(frame_cursor, 8);
        frame_cursor += 8;
        hidden_result_displacement = -(s32)frame_cursor;
    }
    s32 va_register_save_displacement = 0;
    AnalysisType* generated_function_type = analysis_type_from_id(analysis, function->type);
    if (generated_function_type->as.function.is_variadic && abi == CODEGEN_ABI_X86_64_SYSTEM_V)
    {
        frame_cursor = codegen_align_u32(frame_cursor, 16);
        frame_cursor += 176;
        va_register_save_displacement = -(s32)frame_cursor;
    }
    u32 frame_size = codegen_align_u32(frame_cursor, 16);
    u32 outgoing_stack_size = 0;
    if (abi == CODEGEN_ABI_X86_64_WINDOWS)
    {
        CodegenError outgoing_error = codegen_x64_maximum_call_stack_size_prepared(arena, analysis, function, target, &outgoing_stack_size, false);
        if (outgoing_error != CODEGEN_ERROR_NONE)
        {
            result.error = outgoing_error;
            return result;
        }
        if (frame_size > UINT32_MAX - outgoing_stack_size)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        frame_size += outgoing_stack_size;
    }
    u64 capacity = (u64)function->instruction_count * 256 + (u64)function->block_count * 128 + 128;
    u64 read_only_data_capacity = 0;
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->opcode == IR_OPCODE_CONSTANT_STRING)
        {
            read_only_data_capacity += ir_instruction_extra(function, instruction->id).literal.length;
        }
    }
    CodegenRegisterAllocation allocation = codegen_allocate_registers(arena, analysis, function, 1, false);
    CodegenRegisterAllocation vector_allocation = codegen_allocate_registers(arena, analysis, function, 5, true);
    X64Builder builder = {
        .arena = arena,
        .analysis = analysis,
        .function = function,
        .buffer =
            {
                .bytes = arena_allocate(arena, u8, capacity),
                .capacity = capacity,
            },
        .read_only_data =
            {
                .bytes = arena_allocate(arena, u8, read_only_data_capacity),
                .capacity = read_only_data_capacity,
            },
        .block_offsets = arena_allocate(arena, u32, function->block_count),
        .frame_size = frame_size,
        .temporary_base = value_bytes,
        .temporary_count = maximum_parameters,
        .local_storage_base = value_bytes + temporary_bytes,
        .value_storage_offsets = value_storage_offsets,
        .local_storage_offsets = local_storage_offsets,
        .value_registers = allocation.registers,
        .vector_registers = vector_allocation.registers,
        .hidden_result_displacement = hidden_result_displacement,
        .va_register_save_displacement = va_register_save_displacement,
        .signature = signature,
        .abi = abi,
        .target = target,
        .last_wide_vector_result = IR_VALUE_ID_INVALID,
    };
    u32 unwind_action_capacity = 2 + frame_size / 4096 + (frame_size % 4096 != 0);
    result.descriptor = (CodegenFunctionDescriptor){
        .unwind_actions = arena_allocate(arena, CodegenUnwindAction, unwind_action_capacity),
        .symbol = function->symbol,
    };
    codegen_emit_u8(&builder.buffer, 0x55);
    if (!codegen_unwind_action_append(&result.descriptor, unwind_action_capacity, (u32)builder.buffer.count, CODEGEN_UNWIND_ACTION_PUSH_REGISTER,
                                      X64_REGISTER_RBP, 0))
    {
        result.error = CODEGEN_ERROR_CAPACITY;
        return result;
    }
    codegen_emit_u8(&builder.buffer, 0x48);
    codegen_emit_u8(&builder.buffer, 0x89);
    codegen_emit_u8(&builder.buffer, 0xe5);
    if (!codegen_unwind_action_append(&result.descriptor, unwind_action_capacity, (u32)builder.buffer.count,
                                      CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, X64_REGISTER_RBP, 0))
    {
        result.error = CODEGEN_ERROR_CAPACITY;
        return result;
    }
    x64_emit_stack_adjust_described(&builder, frame_size, true, &result.descriptor, unwind_action_capacity);
    result.descriptor.prolog_size = (u32)builder.buffer.count;
    if (signature.result.indirect)
    {
        X64Register source = abi == CODEGEN_ABI_X86_64_WINDOWS ? X64_REGISTER_RCX : X64_REGISTER_RDI;
        x64_emit_store(&builder, source, hidden_result_displacement);
    }
    if (generated_function_type->as.function.is_variadic)
    {
        X64Register registers[] = {
            X64_REGISTER_RDI, X64_REGISTER_RSI, X64_REGISTER_RDX, X64_REGISTER_RCX, X64_REGISTER_R8, X64_REGISTER_R9,
        };
        if (abi == CODEGEN_ABI_X86_64_SYSTEM_V)
        {
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(registers); index += 1)
            {
                x64_emit_store(&builder, registers[index], va_register_save_displacement + (s32)(index * 8));
            }
            for (u32 index = 0; index < 8; index += 1)
            {
                x64_emit_float_store(&builder, index, va_register_save_displacement + 48 + (s32)(index * 16), 64);
            }
        }
        else
        {
            X64Register windows[] = {
                X64_REGISTER_RCX,
                X64_REGISTER_RDX,
                X64_REGISTER_R8,
                X64_REGISTER_R9,
            };
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(windows); index += 1)
            {
                x64_emit_store_memory(&builder, X64_REGISTER_RBP, 16 + index * 8, windows[index], 8);
            }
        }
    }
    u32 line_entry_capacity = function->instruction_count + 1;
    CodegenLineEntry* line_entries = record_lines ? arena_allocate(arena, CodegenLineEntry, line_entry_capacity) : 0;
    u32 line_entry_count = 0;
    ParserSourceRange declaration_range = codegen_analysis_declaration_range(analysis, function);
    // Parser lines are zero-based; recorded lines are one-based.
    codegen_record_line(line_entries, &line_entry_count, line_entry_capacity, 0, 0, declaration_range.line + 1, declaration_range.column + 1);
    for (u32 block_index = 0; block_index < function->block_count && builder.buffer.error == CODEGEN_ERROR_NONE; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        builder.block_offsets[block_index] = (u32)builder.buffer.count;
        for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID; id = function->instructions[id.value].next)
        {
            IrInstruction* emitted = function->instructions + id.value;
            ParserSourceRange emitted_source = ir_instruction_source(function, id);
            // Parser lines are zero-based; recorded lines are one-based.
            codegen_record_line(line_entries, &line_entry_count, line_entry_capacity, (u32)builder.buffer.count, 0, emitted_source.line + 1,
                                emitted_source.column + 1);
            if (!x64_emit_instruction(&builder, block->id, emitted))
            {
                builder.buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                break;
            }
        }
    }
    result.line_entries = line_entries;
    result.line_entry_count = line_entry_count;
    for (CodegenRelocation* relocation = builder.first_relocation; relocation && builder.buffer.error == CODEGEN_ERROR_NONE; relocation = relocation->next)
    {
        if (relocation->target.value >= function->block_count)
        {
            builder.buffer.error = CODEGEN_ERROR_INVALID_IR;
            break;
        }
        s64 displacement = (s64)builder.block_offsets[relocation->target.value] - (s64)(relocation->displacement_offset + 4);
        if (displacement < INT32_MIN || displacement > INT32_MAX)
        {
            builder.buffer.error = CODEGEN_ERROR_CAPACITY;
            break;
        }
        s32 displacement_32 = (s32)displacement;
        memcpy(builder.buffer.bytes + relocation->displacement_offset, &displacement_32, sizeof(displacement_32));
    }
    result.code = (ByteSlice){
        .pointer = builder.buffer.bytes,
        .length = builder.buffer.count,
    };
    result.descriptor.code_size = (u32)result.code.length;
    result.error = builder.buffer.error;
    result.stack_frame_size = frame_size;
    result.first_call_relocation = builder.first_call_relocation;
    result.read_only_data = (ByteSlice){
        .pointer = builder.read_only_data.bytes,
        .length = builder.read_only_data.count,
    };
    result.first_data_relocation = builder.first_data_relocation;
    result.register_value_count = allocation.allocated_count + vector_allocation.allocated_count;
    result.spilled_value_count = allocation.spilled_count + vector_allocation.spilled_count;
    result.native_vector_operation_count = builder.native_vector_operation_count;
    result.split_vector_operation_count = builder.split_vector_operation_count;
    result.vzeroupper_count = builder.vzeroupper_count;
    result.forwarded_wide_vector_load_count = builder.forwarded_wide_vector_load_count;
    if (record_lines)
    {
        codegen_record_analysis_locations(arena, &result, analysis, function, local_storage_offsets, builder.value_storage_offsets,
                                          allocation.registers, vector_allocation.registers, builder.block_offsets, target, true, frame_size);
    }
    return result;
}

typedef struct A64Relocation A64Relocation;
struct A64Relocation
{
    A64Relocation* next;
    IrBlockId target;
    u32 instruction_offset;
    bool conditional;
    u8 reserved[3];
};

#define A64_VALUE_SLOT_COMPONENT_COUNT 4

BUSTER_GLOBAL_LOCAL void a64_emit_instruction_word(CodegenBuffer* buffer, u32 instruction)
{
    codegen_emit_u32(buffer, instruction);
}

BUSTER_GLOBAL_LOCAL u32 a64_value_offset(IrValueId value)
{
    return value.value * A64_VALUE_SLOT_SIZE;
}

BUSTER_GLOBAL_LOCAL u32 a64_value_component_offset(IrValueId value, u32 component)
{
    return a64_value_offset(value) + component * 8;
}

BUSTER_GLOBAL_LOCAL void a64_emit_load_value(CodegenBuffer* buffer, u32 target, IrValueId value)
{
    if (buffer->value_registers && buffer->value_registers[value.value] != CODEGEN_REGISTER_UNALLOCATED)
    {
        u32 source = buffer->allocated_register_base + buffer->value_registers[value.value];
        if (target != source)
        {
            a64_emit_instruction_word(buffer, 0xaa0003e0 | (source << 16) | target);
        }
        return;
    }
    u32 offset = a64_value_offset(value);
    if (offset > 32760)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(buffer, 0xf94003e0 | ((offset / 8) << 10) | target);
}

BUSTER_GLOBAL_LOCAL void a64_emit_store_value(CodegenBuffer* buffer, u32 source, IrValueId value)
{
    if (buffer->value_registers && buffer->value_registers[value.value] != CODEGEN_REGISTER_UNALLOCATED)
    {
        u32 target = buffer->allocated_register_base + buffer->value_registers[value.value];
        if (target != source)
        {
            a64_emit_instruction_word(buffer, 0xaa0003e0 | (source << 16) | target);
        }
        return;
    }
    u32 offset = a64_value_offset(value);
    if (offset > 32760)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(buffer, 0xf90003e0 | ((offset / 8) << 10) | source);
}

BUSTER_GLOBAL_LOCAL void a64_emit_load_offset(CodegenBuffer* buffer, u32 target, u32 offset)
{
    if (offset > 32760)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(buffer, 0xf94003e0 | ((offset / 8) << 10) | target);
}

BUSTER_GLOBAL_LOCAL void a64_emit_store_offset(CodegenBuffer* buffer, u32 source, u32 offset)
{
    if (offset > 32760)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(buffer, 0xf90003e0 | ((offset / 8) << 10) | source);
}

void a64_emit_float_load_offset(CodegenBuffer* buffer, u32 target, u32 offset, u32 size)
{
    u32 scale = size <= 4 ? 4 : size <= 8 ? 8 : 16;
    if (offset % scale || offset / scale > 4095)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(buffer, (size <= 4 ? 0xbd4003e0 : size <= 8 ? 0xfd4003e0 : 0x3dc003e0) | ((offset / scale) << 10) | target);
}

void a64_emit_float_store_offset(CodegenBuffer* buffer, u32 source, u32 offset, u32 size)
{
    u32 scale = size <= 4 ? 4 : size <= 8 ? 8 : 16;
    if (offset % scale || offset / scale > 4095)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(buffer, (size <= 4 ? 0xbd0003e0 : size <= 8 ? 0xfd0003e0 : 0x3d8003e0) | ((offset / scale) << 10) | source);
}

BUSTER_GLOBAL_LOCAL void a64_emit_load_value_component(CodegenBuffer* buffer, u32 target, IrValueId value, u32 component)
{
    a64_emit_load_offset(buffer, target, a64_value_component_offset(value, component));
}

BUSTER_GLOBAL_LOCAL void a64_emit_store_value_component(CodegenBuffer* buffer, u32 source, IrValueId value, u32 component)
{
    a64_emit_store_offset(buffer, source, a64_value_component_offset(value, component));
}

BUSTER_GLOBAL_LOCAL void a64_emit_float_load_value(CodegenBuffer* buffer, u32 target, IrValueId value, u32 width)
{
    u32 offset = a64_value_offset(value);
    u32 scale = width == 32 ? 4 : 8;
    if (offset / scale > 4095)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(buffer, (width == 32 ? 0xbd4003e0 : 0xfd4003e0) | ((offset / scale) << 10) | target);
}

BUSTER_GLOBAL_LOCAL void a64_emit_float_store_value(CodegenBuffer* buffer, u32 source, IrValueId value, u32 width)
{
    u32 offset = a64_value_offset(value);
    u32 scale = width == 32 ? 4 : 8;
    if (offset / scale > 4095)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(buffer, (width == 32 ? 0xbd0003e0 : 0xfd0003e0) | ((offset / scale) << 10) | source);
}

BUSTER_GLOBAL_LOCAL void a64_emit_constant(CodegenBuffer* buffer, u32 target, u64 value)
{
    a64_emit_instruction_word(buffer, 0xd2800000 | ((u32)(value & 0xffff) << 5) | target);
    for (u32 shift = 16; shift < 64; shift += 16)
    {
        a64_emit_instruction_word(buffer, 0xf2800000 | ((shift / 16) << 21) | ((u32)((value >> shift) & 0xffff) << 5) | target);
    }
}

BUSTER_GLOBAL_LOCAL bool a64_emit_windows_large_stack_adjust(CodegenBuffer* buffer, u32 size, bool subtract,
                                                             CodegenFunctionDescriptor* descriptor, u32 action_capacity)
{
    if (size <= 4080 || size % 16)
    {
        return false;
    }
    u32 units = size / 16;
    if (!subtract)
    {
        a64_emit_constant(buffer, 15, units);
        a64_emit_instruction_word(buffer, 0x8b2f73ff);
        return true;
    }
    u32 instruction_offsets[13] = {0};
    for (u32 shift = 0; shift < 64; shift += 16)
    {
        a64_emit_instruction_word(buffer,
                                  (shift ? 0xf2800000 : 0xd2800000) | ((shift / 16) << 21) |
                                      ((u32)(((u64)units >> shift) & 0xffff) << 5) | 15);
        instruction_offsets[shift / 16] = (u32)buffer->count;
    }
    a64_emit_instruction_word(buffer, 0x910003f0);
    instruction_offsets[4] = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0xcb0f1210);
    instruction_offsets[5] = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0x910003f1);
    instruction_offsets[6] = (u32)buffer->count;
    u32 loop_offset = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0xd1400631);
    instruction_offsets[7] = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0xeb10023f);
    instruction_offsets[8] = (u32)buffer->count;
    u32 final_branch = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0x54000009);
    instruction_offsets[9] = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0xf900023f);
    instruction_offsets[10] = (u32)buffer->count;
    u32 loop_branch = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0x14000000);
    instruction_offsets[11] = (u32)buffer->count;
    u32 final_offset = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0xf900021f);
    instruction_offsets[12] = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0xcb2f73ff);
    if (buffer->error != CODEGEN_ERROR_NONE || (final_offset - final_branch) % 4 || (loop_offset - loop_branch) % 4)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return true;
    }
    u32 final_words = (final_offset - final_branch) / 4;
    s32 loop_words = ((s32)loop_offset - (s32)loop_branch) / 4;
    u32 final_instruction = 0x54000009 | ((final_words & 0x7ffff) << 5);
    u32 loop_instruction = 0x14000000 | ((u32)loop_words & 0x03ffffff);
    memcpy(buffer->bytes + final_branch, &final_instruction, sizeof(final_instruction));
    memcpy(buffer->bytes + loop_branch, &loop_instruction, sizeof(loop_instruction));
    if (descriptor)
    {
        for (u32 instruction_index = 0; instruction_index < BUSTER_ARRAY_LENGTH(instruction_offsets); instruction_index += 1)
        {
            if (!codegen_unwind_action_append(descriptor, action_capacity, instruction_offsets[instruction_index] - descriptor->code_offset,
                                              CODEGEN_UNWIND_ACTION_NOP, 0, 0))
            {
                buffer->error = CODEGEN_ERROR_CAPACITY;
                return true;
            }
        }
        if (!codegen_unwind_action_append(descriptor, action_capacity, (u32)buffer->count - descriptor->code_offset,
                                          CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, size))
        {
            buffer->error = CODEGEN_ERROR_CAPACITY;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void a64_emit_stack_adjust_described(CodegenBuffer* buffer, u32 size, bool subtract, CodegenFunctionDescriptor* descriptor,
                                                         u32 action_capacity, bool windows)
{
    if (windows && a64_emit_windows_large_stack_adjust(buffer, size, subtract, descriptor, action_capacity))
    {
        return;
    }
    while (size)
    {
        u32 chunk = BUSTER_MIN(size, windows ? 4080u : 4095u);
        a64_emit_instruction_word(buffer, (subtract ? 0xd10003ff : 0x910003ff) | (chunk << 10));
        if (subtract && descriptor &&
            !codegen_unwind_action_append(descriptor, action_capacity, (u32)buffer->count - descriptor->code_offset,
                                          CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, chunk))
        {
            buffer->error = CODEGEN_ERROR_CAPACITY;
            return;
        }
        if (subtract)
        {
            a64_emit_instruction_word(buffer, 0xf90003ff);
            if (windows && descriptor &&
                !codegen_unwind_action_append(descriptor, action_capacity, (u32)buffer->count - descriptor->code_offset, CODEGEN_UNWIND_ACTION_NOP, 0, 0))
            {
                buffer->error = CODEGEN_ERROR_CAPACITY;
                return;
            }
        }
        size -= chunk;
    }
}

BUSTER_GLOBAL_LOCAL void a64_emit_stack_address(CodegenBuffer* buffer, u32 target, u32 offset)
{
    a64_emit_instruction_word(buffer, 0x910003e0 | target);
    while (offset)
    {
        u32 chunk = BUSTER_MIN(offset, 4095);
        a64_emit_instruction_word(buffer, 0x91000000 | target | (target << 5) | (chunk << 10));
        offset -= chunk;
    }
}

BUSTER_GLOBAL_LOCAL void a64_emit_load_pointer(CodegenBuffer* buffer, u32 target, u32 address, u32 size)
{
    u32 encoded = size == 1 ? 0x39400000 : size == 2 ? 0x79400000 : size == 4 ? 0xb9400000 : size == 8 ? 0xf9400000 : 0;
    if (!encoded)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(buffer, encoded | (address << 5) | target);
}

BUSTER_GLOBAL_LOCAL void a64_emit_store_pointer(CodegenBuffer* buffer, u32 source, u32 address, u32 size)
{
    u32 encoded = size == 1 ? 0x39000000 : size == 2 ? 0x79000000 : size == 4 ? 0xb9000000 : size == 8 ? 0xf9000000 : 0;
    if (!encoded)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(buffer, encoded | (address << 5) | source);
}

BUSTER_GLOBAL_LOCAL void a64_emit_atomic_pointer(CodegenBuffer* buffer, u32 value, u32 address, u32 size, bool store)
{
    u32 size_bits = size == 1 ? 0 : size == 2 ? UINT32_C(0x40000000) : size == 4 ? UINT32_C(0x80000000) : size == 8 ? UINT32_C(0xc0000000) : 0;
    if ((size != 1 && !size_bits) || value > 31 || address > 31)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(buffer, (store ? UINT32_C(0x089ffc00) : UINT32_C(0x08dffc00)) | size_bits | (address << 5) | value);
}

BUSTER_GLOBAL_LOCAL void a64_emit_atomic_exclusive_load(CodegenBuffer* buffer, u32 value, u32 address, u32 size, bool acquire)
{
    u32 size_bits = size == 1 ? 0 : size == 2 ? UINT32_C(0x40000000) : size == 4 ? UINT32_C(0x80000000) : size == 8 ? UINT32_C(0xc0000000) : 0;
    if ((size != 1 && !size_bits) || value > 31 || address > 31)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(buffer, (acquire ? UINT32_C(0x085ffc00) : UINT32_C(0x085f7c00)) | size_bits | (address << 5) | value);
}

BUSTER_GLOBAL_LOCAL void a64_emit_atomic_exclusive_store(CodegenBuffer* buffer, u32 status, u32 value, u32 address, u32 size, bool release)
{
    u32 size_bits = size == 1 ? 0 : size == 2 ? UINT32_C(0x40000000) : size == 4 ? UINT32_C(0x80000000) : size == 8 ? UINT32_C(0xc0000000) : 0;
    if ((size != 1 && !size_bits) || status > 31 || value > 31 || address > 31)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(buffer, (release ? UINT32_C(0x0800fc00) : UINT32_C(0x08007c00)) | size_bits | (status << 16) | (address << 5) | value);
}

void codegen_canonical_a64_base_address(CodegenBuffer* buffer, u32 register_number, u32 base_register, u32 byte_offset);

void a64_emit_load_pointer_offset(CodegenBuffer* buffer, u32 target, u32 address, u32 offset, u32 size)
{
    u32 scale = size == 1 ? 1 : size == 2 ? 2 : size == 4 ? 4 : 8;
    u32 encoded = size == 1 ? 0x39400000 : size == 2 ? 0x79400000 : size == 4 ? 0xb9400000 : size == 8 ? 0xf9400000 : 0;
    if (!encoded || offset % scale || target > 30 || address > 31)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    if (offset / scale > 4095)
    {
        codegen_canonical_a64_base_address(buffer, target, address, offset);
        address = target;
        offset = 0;
    }
    a64_emit_instruction_word(buffer, encoded | ((offset / scale) << 10) | (address << 5) | target);
}

void a64_emit_store_pointer_offset(CodegenBuffer* buffer, u32 source, u32 address, u32 offset, u32 size)
{
    u32 scale = size == 1 ? 1 : size == 2 ? 2 : size == 4 ? 4 : 8;
    u32 encoded = size == 1 ? 0x39000000 : size == 2 ? 0x79000000 : size == 4 ? 0xb9000000 : size == 8 ? 0xf9000000 : 0;
    if (!encoded || offset % scale || source > 31 || address > 31)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    if (offset / scale > 4095)
    {
        u32 scratch = 16;
        if (scratch == source || scratch == address)
        {
            scratch = 17;
        }
        if (scratch == source || scratch == address)
        {
            scratch = 15;
        }
        codegen_canonical_a64_base_address(buffer, scratch, address, offset);
        address = scratch;
        offset = 0;
    }
    a64_emit_instruction_word(buffer, encoded | ((offset / scale) << 10) | (address << 5) | source);
}

BUSTER_GLOBAL_LOCAL void a64_emit_float_pointer_offset(CodegenBuffer* buffer, u32 target, u32 address, u32 offset, u32 size, bool store)
{
    u32 scale = size <= 4 ? 4 : 8;
    if (offset % scale || offset / scale > 4095)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    u32 encoded = size <= 4 ? (store ? 0xbd000000 : 0xbd400000) : (store ? 0xfd000000 : 0xfd400000);
    a64_emit_instruction_word(buffer, encoded | ((offset / scale) << 10) | (address << 5) | target);
}

void a64_emit_copy_memory_registers(CodegenBuffer* buffer, u32 destination, u32 source, u32 scratch, u32 size)
{
    u32 offset = 0;
    while (size - offset >= 8)
    {
        a64_emit_instruction_word(buffer, 0xf9400000 | ((offset / 8) << 10) | (source << 5) | scratch);
        a64_emit_instruction_word(buffer, 0xf9000000 | ((offset / 8) << 10) | (destination << 5) | scratch);
        offset += 8;
    }
    if (size - offset >= 4)
    {
        a64_emit_instruction_word(buffer, 0xb9400000 | ((offset / 4) << 10) | (source << 5) | scratch);
        a64_emit_instruction_word(buffer, 0xb9000000 | ((offset / 4) << 10) | (destination << 5) | scratch);
        offset += 4;
    }
    if (size - offset >= 2)
    {
        a64_emit_instruction_word(buffer, 0x79400000 | ((offset / 2) << 10) | (source << 5) | scratch);
        a64_emit_instruction_word(buffer, 0x79000000 | ((offset / 2) << 10) | (destination << 5) | scratch);
        offset += 2;
    }
    if (size != offset)
    {
        a64_emit_instruction_word(buffer, 0x39400000 | (offset << 10) | (source << 5) | scratch);
        a64_emit_instruction_word(buffer, 0x39000000 | (offset << 10) | (destination << 5) | scratch);
    }
}

BUSTER_GLOBAL_LOCAL void a64_emit_copy_memory(CodegenBuffer* buffer, u32 size)
{
    a64_emit_copy_memory_registers(buffer, 0, 1, 2, size);
}

void a64_emit_initialize_aggregate_result(CodegenBuffer* buffer, u32* value_storage_offsets, IrValueId value)
{
    a64_emit_stack_address(buffer, 16, value_storage_offsets[value.value]);
    a64_emit_store_value_component(buffer, 16, value, 0);
}

BUSTER_GLOBAL_LOCAL bool a64_emit_vector_binary(CodegenBuffer* buffer, AnalysisResult* analysis, Target target, AnalysisTypeId operand_type_id,
                                                u32* value_storage_offsets, u8* vector_registers, IrInstruction* instruction)
{
    if (!target_cpu_feature_has(target, TARGET_CPU_FEATURE_AARCH64_NEON))
    {
        return false;
    }
    AnalysisType* vector = analysis_type_from_id(analysis, operand_type_id);
    if (vector->kind != ANALYSIS_TYPE_VECTOR ||
        (vector->layout.size != 8 && vector->layout.size != 16 && vector->layout.size != 32 && vector->layout.size != 64))
    {
        return false;
    }
    AnalysisType* element = analysis_type_from_id(analysis, vector->as.vector.element_type);
    u32 width = element->kind == ANALYSIS_TYPE_FLOAT ? element->as.float_bit_width : element->kind == ANALYSIS_TYPE_INTEGER ? element->as.integer.bit_width : 0;
    if (!width)
    {
        return false;
    }
    u32 operation = 0;
    bool lane_sized = true;
    bool comparison = false;
    bool swap_operands = false;
    bool invert_result = false;
    switch (instruction->binary_operation)
    {
    case IR_BINARY_VECTOR_FLOAT_ADD:
        operation = 0x0e20d400;
        break;
    case IR_BINARY_VECTOR_FLOAT_SUBTRACT:
        operation = 0x0ea0d400;
        break;
    case IR_BINARY_VECTOR_FLOAT_MULTIPLY:
        operation = 0x2e20dc00;
        break;
    case IR_BINARY_VECTOR_FLOAT_DIVIDE:
        operation = 0x2e20fc00;
        break;
    case IR_BINARY_VECTOR_INTEGER_ADD:
        operation = 0x0e208400;
        break;
    case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
        operation = 0x2e208400;
        break;
    case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
        operation = 0x0e201c00;
        lane_sized = false;
        break;
    case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
        operation = 0x0ea01c00;
        lane_sized = false;
        break;
    case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
        operation = 0x2e201c00;
        lane_sized = false;
        break;
    case IR_BINARY_VECTOR_INTEGER_EQUAL:
        operation = 0x2e208c00;
        comparison = true;
        break;
    case IR_BINARY_VECTOR_INTEGER_NOT_EQUAL:
        operation = 0x2e208c00;
        comparison = true;
        invert_result = true;
        break;
    case IR_BINARY_VECTOR_SIGNED_LESS:
        operation = 0x0e203400;
        comparison = true;
        swap_operands = true;
        break;
    case IR_BINARY_VECTOR_SIGNED_LESS_EQUAL:
        operation = 0x0e203c00;
        comparison = true;
        swap_operands = true;
        break;
    case IR_BINARY_VECTOR_SIGNED_GREATER:
        operation = 0x0e203400;
        comparison = true;
        break;
    case IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL:
        operation = 0x0e203c00;
        comparison = true;
        break;
    case IR_BINARY_VECTOR_UNSIGNED_LESS:
        operation = 0x2e203400;
        comparison = true;
        swap_operands = true;
        break;
    case IR_BINARY_VECTOR_UNSIGNED_LESS_EQUAL:
        operation = 0x2e203c00;
        comparison = true;
        swap_operands = true;
        break;
    case IR_BINARY_VECTOR_UNSIGNED_GREATER:
        operation = 0x2e203400;
        comparison = true;
        break;
    case IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL:
        operation = 0x2e203c00;
        comparison = true;
        break;
    case IR_BINARY_VECTOR_FLOAT_EQUAL:
        operation = 0x0e20e400;
        comparison = true;
        break;
    case IR_BINARY_VECTOR_FLOAT_NOT_EQUAL:
        operation = 0x0e20e400;
        comparison = true;
        invert_result = true;
        break;
    case IR_BINARY_VECTOR_FLOAT_LESS:
        operation = 0x2ea0e400;
        comparison = true;
        swap_operands = true;
        break;
    case IR_BINARY_VECTOR_FLOAT_LESS_EQUAL:
        operation = 0x2e20e400;
        comparison = true;
        swap_operands = true;
        break;
    case IR_BINARY_VECTOR_FLOAT_GREATER:
        operation = 0x2ea0e400;
        comparison = true;
        break;
    case IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL:
        operation = 0x2e20e400;
        comparison = true;
        break;
    default:
        return false;
    }
    if (element->kind == ANALYSIS_TYPE_FLOAT)
    {
        if (width == 64)
        {
            operation |= 0x00400000;
        }
        else if (width != 32)
        {
            return false;
        }
    }
    else if (lane_sized)
    {
        u32 size = width == 8 ? 0 : width == 16 ? 1 : width == 32 ? 2 : width == 64 ? 3 : UINT32_MAX;
        if (size == UINT32_MAX)
        {
            return false;
        }
        operation |= size << 22;
    }
    u32 target_register = 0;
    if (vector->layout.size <= 16 && vector_registers[instruction->result.value] != CODEGEN_REGISTER_UNALLOCATED)
    {
        target_register = 2 + vector_registers[instruction->result.value];
    }
    a64_emit_initialize_aggregate_result(buffer, value_storage_offsets, instruction->result);
    a64_emit_load_value_component(buffer, 0, instruction->operands[0], 0);
    a64_emit_load_value_component(buffer, 1, instruction->operands[1], 0);
    a64_emit_load_value_component(buffer, 2, instruction->result, 0);
    u32 chunk_count = (u32)((vector->layout.size + 15) / 16);
    for (u32 chunk = 0; chunk < chunk_count; chunk += 1)
    {
        u32 chunk_size = vector->layout.size - (u64)chunk * 16 >= 16 ? 16 : 8;
        u32 load = chunk_size == 16 ? 0x3dc00000 : 0xfd400000;
        u32 store = chunk_size == 16 ? 0x3d800000 : 0xfd000000;
        u32 chunk_operation = operation | (chunk_size == 16 ? 0x40000000 : 0);
        a64_emit_instruction_word(buffer, load | (0 << 5) | target_register);
        a64_emit_instruction_word(buffer, load | (1 << 5) | 1);
        a64_emit_instruction_word(buffer, chunk_operation | ((swap_operands ? target_register : 1) << 16) | ((swap_operands ? 1 : target_register) << 5) |
                                              target_register);
        if (comparison && invert_result)
        {
            a64_emit_instruction_word(buffer, 0x2e205800 | (chunk_size == 16 ? 0x40000000 : 0) | (target_register << 5) | target_register);
        }
        a64_emit_instruction_word(buffer, store | (2 << 5) | target_register);
        if (chunk + 1 < chunk_count)
        {
            a64_emit_instruction_word(buffer, 0x91004000);
            a64_emit_instruction_word(buffer, 0x91004021);
            a64_emit_instruction_word(buffer, 0x91004042);
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void a64_emit_load_abi_part(CodegenBuffer* buffer, AnalysisResult* analysis, IrFunction* function, IrValueId value, AnalysisType* type,
                                                CodegenAbiPart* part, u32 integer_target, u32 float_target)
{
    if (codegen_type_is_indirect_value(type))
    {
        a64_emit_load_value_component(buffer, 16, value, 0);
        if (type->kind == ANALYSIS_TYPE_VECTOR && part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER && (part->size == 8 || part->size == 16))
        {
            a64_emit_instruction_word(buffer, (part->size == 16 ? 0x3dc00200 : 0xfd400200) | float_target);
            return;
        }
        if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            a64_emit_float_pointer_offset(buffer, float_target, 16, part->value_offset, part->size, false);
        }
        else
        {
            a64_emit_load_pointer_offset(buffer, integer_target, 16, part->value_offset, part->size);
        }
        return;
    }
    if (codegen_type_is_inline_collection(type))
    {
        u32 component = part->value_offset / 8;
        if (type->kind == ANALYSIS_TYPE_RANGE)
        {
            AnalysisType* element = analysis_type_from_id(analysis, type->as.element_type);
            component = part->value_offset / BUSTER_MAX(codegen_type_storage_size(element), 1);
        }
        if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            u32 offset = a64_value_component_offset(value, component);
            u32 scale = part->size <= 4 ? 4 : 8;
            a64_emit_instruction_word(buffer, (part->size <= 4 ? 0xbd4003e0 : 0xfd4003e0) | ((offset / scale) << 10) | float_target);
        }
        else
        {
            a64_emit_load_value_component(buffer, integer_target, value, component);
            if (type->kind == ANALYSIS_TYPE_RANGE)
            {
                AnalysisType* element = analysis_type_from_id(analysis, type->as.element_type);
                u32 element_size = codegen_type_storage_size(element);
                if (element_size < 8 && part->size > element_size)
                {
                    u32 shift = element_size * 8;
                    a64_emit_instruction_word(buffer, 0xd3400000 | ((shift - 1) << 10) | (integer_target << 5) | integer_target);
                    a64_emit_load_value_component(buffer, 16, value, component + 1);
                    a64_emit_instruction_word(buffer, 0xd3400000 | ((64 - shift) << 16) | ((63 - shift) << 10) | (16 << 5) | 16);
                    a64_emit_instruction_word(buffer, 0xaa000000 | (16 << 16) | (integer_target << 5) | integer_target);
                }
            }
        }
        return;
    }
    BUSTER_UNUSED(function);
}

BUSTER_GLOBAL_LOCAL void a64_emit_store_abi_part(CodegenBuffer* buffer, AnalysisResult* analysis, IrFunction* function, IrValueId value, AnalysisType* type,
                                                 CodegenAbiPart* part, u32 integer_source, u32 float_source)
{
    if (codegen_type_is_indirect_value(type))
    {
        a64_emit_load_value_component(buffer, 16, value, 0);
        if (type->kind == ANALYSIS_TYPE_VECTOR && part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER && (part->size == 8 || part->size == 16))
        {
            a64_emit_instruction_word(buffer, (part->size == 16 ? 0x3d800200 : 0xfd000200) | float_source);
            return;
        }
        if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            a64_emit_float_pointer_offset(buffer, float_source, 16, part->value_offset, part->size, true);
        }
        else
        {
            a64_emit_store_pointer_offset(buffer, integer_source, 16, part->value_offset, part->size);
        }
        return;
    }
    if (codegen_type_is_inline_collection(type))
    {
        u32 component = part->value_offset / 8;
        if (type->kind == ANALYSIS_TYPE_RANGE)
        {
            AnalysisType* element = analysis_type_from_id(analysis, type->as.element_type);
            component = part->value_offset / BUSTER_MAX(codegen_type_storage_size(element), 1);
        }
        if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
        {
            u32 offset = a64_value_component_offset(value, component);
            u32 scale = part->size <= 4 ? 4 : 8;
            a64_emit_instruction_word(buffer, (part->size <= 4 ? 0xbd0003e0 : 0xfd0003e0) | ((offset / scale) << 10) | float_source);
        }
        else
        {
            a64_emit_store_value_component(buffer, integer_source, value, component);
            if (type->kind == ANALYSIS_TYPE_RANGE)
            {
                AnalysisType* element = analysis_type_from_id(analysis, type->as.element_type);
                u32 element_size = codegen_type_storage_size(element);
                if (element_size < 8 && part->size > element_size)
                {
                    u32 shift = element_size * 8;
                    a64_emit_instruction_word(buffer, 0xaa0003f0 | (integer_source << 16));
                    a64_emit_instruction_word(buffer, 0xd3400000 | (shift << 16) | (63 << 10) | (16 << 5) | 16);
                    a64_emit_store_value_component(buffer, 16, value, component + 1);
                }
            }
        }
        return;
    }
    BUSTER_UNUSED(function);
}

BUSTER_GLOBAL_LOCAL void a64_emit_collection_component(CodegenBuffer* buffer, AnalysisResult* analysis, IrFunction* function, IrValueId base_id, u32 component,
                                                       u32 target)
{
    IrValue* base_value = function->values + base_id.value;
    AnalysisType* base_type = analysis_type_from_id(analysis, base_value->type);
    if (base_value->category == IR_VALUE_PLACE)
    {
        if (base_type->kind == ANALYSIS_TYPE_ARRAY || base_type->kind == ANALYSIS_TYPE_VECTOR)
        {
            bool vector = base_type->kind == ANALYSIS_TYPE_VECTOR;
            if (component == 0)
            {
                a64_emit_load_value(buffer, target, base_id);
            }
            else if (component == 1)
            {
                a64_emit_constant(buffer, target, vector ? base_type->as.vector.count : base_type->as.array.count);
            }
            else if (component == 2)
            {
                AnalysisType* element = analysis_type_from_id(analysis, vector ? base_type->as.vector.element_type : base_type->as.array.element_type);
                a64_emit_constant(buffer, target, codegen_type_storage_size(element));
            }
            else
            {
                a64_emit_constant(buffer, target, 0);
            }
            return;
        }
        if (base_type->kind == ANALYSIS_TYPE_SLICE)
        {
            a64_emit_load_value(buffer, 4, base_id);
            a64_emit_instruction_word(buffer, 0xf9400000 | (4 << 5) | ((component * 8 / 8) << 10) | target);
            return;
        }
    }
    a64_emit_load_value_component(buffer, target, base_id, component);
}

BUSTER_GLOBAL_LOCAL void a64_relocation_add(Arena* arena, CodegenBuffer* buffer, A64Relocation** first, A64Relocation** last, IrBlockId target,
                                            bool conditional)
{
    A64Relocation* relocation = arena_allocate(arena, A64Relocation, 1);
    *relocation = (A64Relocation){
        .target = target,
        .instruction_offset = (u32)buffer->count,
        .conditional = conditional,
    };
    if (*last)
    {
        (*last)->next = relocation;
    }
    else
    {
        *first = relocation;
    }
    *last = relocation;
    a64_emit_instruction_word(buffer, conditional ? 0xb4000000 : 0x14000000);
}

BUSTER_GLOBAL_LOCAL void a64_emit_edge_copies(CodegenBuffer* buffer, IrFunction* function, IrBlockId predecessor, IrBlockId target, u32 temporary_base)
{
    IrBlock* block = function->blocks + target.value;
    u32 index = 0;
    for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
    {
        IrValueId incoming = x64_parameter_incoming(parameter, predecessor);
        if (incoming.value == IR_ID_UNDERLYING_INVALID)
        {
            buffer->error = CODEGEN_ERROR_INVALID_IR;
            return;
        }
        for (u32 component = 0; component < A64_VALUE_SLOT_COMPONENT_COUNT; component += 1)
        {
            a64_emit_load_value_component(buffer, 0, incoming, component);
            a64_emit_store_offset(buffer, 0, temporary_base + index * A64_VALUE_SLOT_SIZE + component * 8);
        }
        index += 1;
    }
    index = 0;
    for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
    {
        for (u32 component = 0; component < A64_VALUE_SLOT_COMPONENT_COUNT; component += 1)
        {
            a64_emit_load_offset(buffer, 0, temporary_base + index * A64_VALUE_SLOT_SIZE + component * 8);
            a64_emit_store_value_component(buffer, 0, parameter->value, component);
        }
        index += 1;
    }
}

BUSTER_GLOBAL_LOCAL void a64_call_relocation_add(Arena* arena, CodegenBuffer* buffer, CodegenCallRelocation** first, CodegenCallRelocation** last,
                                                 IrInstruction* instruction, bool absolute)
{
    if (absolute && (buffer->count & 7))
    {
        a64_emit_instruction_word(buffer, 0xd503201f);
    }
    CodegenCallRelocation* relocation = arena_allocate(arena, CodegenCallRelocation, 1);
    *relocation = (CodegenCallRelocation){
        .entity = instruction->entity,
        .instantiation = instruction->instantiation,
        .displacement_offset = (u32)buffer->count + (absolute ? 8 : 0),
        .aarch64 = true,
        .absolute = absolute,
    };
    if (*last)
    {
        (*last)->next = relocation;
    }
    else
    {
        *first = relocation;
    }
    *last = relocation;
    if (absolute)
    {
        a64_emit_instruction_word(buffer, 0x58000040);
        a64_emit_instruction_word(buffer, 0x14000003);
        codegen_emit_u64(buffer, 0);
    }
    else
    {
        a64_emit_instruction_word(buffer, 0x94000000);
    }
}

BUSTER_GLOBAL_LOCAL bool codegen_value_requires_materialization(IrFunction* function, IrValueId value)
{
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
        {
            if (instruction->operands[operand_index].value != value.value)
            {
                continue;
            }
            if (instruction->opcode == IR_OPCODE_CALL && operand_index == 0)
            {
                continue;
            }
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL CodegenFunction codegen_generate_aarch64(Arena* arena, AnalysisResult* analysis, IrFunction* function, Target target, bool record_lines)
{
    CodegenAbi abi = codegen_abi_for_target(target);
    CodegenFunction result = {
        .abi = abi,
        .symbol = function->symbol,
    };
    CodegenAbiSignature function_signature = codegen_classify_signature_for_target(arena, analysis, function->type, target);
    if (!function_signature.valid)
    {
        result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
        return result;
    }
    CodegenRegisterAllocation allocation = codegen_allocate_registers(arena, analysis, function, 7, false);
    CodegenRegisterAllocation vector_allocation = codegen_allocate_registers(arena, analysis, function, 6, true);
    u32 maximum_parameters = 0;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        maximum_parameters = BUSTER_MAX(maximum_parameters, function->blocks[block_index].parameter_count);
    }
    u32 temporary_base = function->value_count * A64_VALUE_SLOT_SIZE;
    u32 local_base = temporary_base + maximum_parameters * A64_VALUE_SLOT_SIZE;
    u32* local_storage_offsets = arena_allocate(arena, u32, function->local_count);
    u32* local_storage_sizes = arena_allocate(arena, u32, function->local_count);
    for (u32 index = 0; index < function->local_count; index += 1)
    {
        local_storage_sizes[index] = 8;
    }
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction* instruction = function->instructions + index;
        if (instruction->opcode == IR_OPCODE_LOCAL && instruction->local.value < function->local_count)
        {
            AnalysisType* type = analysis_type_from_id(analysis, instruction->type);
            u32 size = codegen_type_is_inline_collection(type) ? A64_VALUE_SLOT_SIZE : codegen_type_storage_size(type);
            local_storage_sizes[instruction->local.value] = BUSTER_MAX(size, 8);
        }
    }
    u32 frame_cursor = local_base;
    for (u32 index = 0; index < function->local_count; index += 1)
    {
        frame_cursor = codegen_align_u32(frame_cursor, 8);
        local_storage_offsets[index] = frame_cursor;
        frame_cursor += local_storage_sizes[index];
    }
    u32* value_storage_offsets = arena_allocate(arena, u32, function->value_count);
    for (u32 index = 0; index < function->value_count; index += 1)
    {
        AnalysisType* type = analysis_type_from_id(analysis, function->values[index].type);
        if (!codegen_type_is_indirect_value(type))
        {
            continue;
        }
        u32 size = codegen_type_storage_size(type);
        u32 alignment = BUSTER_MAX(type->layout.alignment, 1);
        if (type->kind == ANALYSIS_TYPE_VECTOR)
        {
            alignment = BUSTER_MIN(alignment, 16);
        }
        if (!size || alignment > 16)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        frame_cursor = codegen_align_u32(frame_cursor, alignment);
        value_storage_offsets[index] = frame_cursor;
        frame_cursor += size;
    }
    u32 hidden_result_offset = 0;
    if (function_signature.result.indirect)
    {
        frame_cursor = codegen_align_u32(frame_cursor, 8);
        hidden_result_offset = frame_cursor;
        frame_cursor += 8;
    }
    u32 incoming_integer_register_count = 0;
    u32 incoming_float_register_count = 0;
    for (u32 argument_index = 0; argument_index < function_signature.argument_count; argument_index += 1)
    {
        CodegenAbiLocation* location = function_signature.arguments + argument_index;
        for (u32 part_index = 0; part_index < location->part_count; part_index += 1)
        {
            CodegenAbiPart* part = location->parts + part_index;
            if (part->kind == CODEGEN_ABI_LOCATION_INTEGER_REGISTER)
            {
                incoming_integer_register_count = BUSTER_MAX(incoming_integer_register_count, part->index + 1);
            }
            else if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
            {
                incoming_float_register_count = BUSTER_MAX(incoming_float_register_count, part->index + 1);
            }
        }
    }
    u32 incoming_integer_register_base = 0;
    if (incoming_integer_register_count)
    {
        frame_cursor = codegen_align_u32(frame_cursor, 8);
        incoming_integer_register_base = frame_cursor;
        frame_cursor += incoming_integer_register_count * 8;
    }
    u32 incoming_float_register_base = 0;
    if (incoming_float_register_count)
    {
        frame_cursor = codegen_align_u32(frame_cursor, 16);
        incoming_float_register_base = frame_cursor;
        frame_cursor += incoming_float_register_count * 16;
    }
    AnalysisType* generated_function_type = analysis_type_from_id(analysis, function->type);
    u32 va_register_save_base = 0;
    if (generated_function_type->as.function.is_variadic && (abi == CODEGEN_ABI_AARCH64_AAPCS64 || abi == CODEGEN_ABI_AARCH64_WINDOWS))
    {
        frame_cursor = codegen_align_u32(frame_cursor, 16);
        va_register_save_base = frame_cursor;
        frame_cursor += abi == CODEGEN_ABI_AARCH64_AAPCS64 ? 192 : 64;
    }
    u32 frame_size = codegen_align_u32(frame_cursor, 16);
    if (frame_size > 32760)
    {
        result.error = CODEGEN_ERROR_CAPACITY;
        return result;
    }
    u64 capacity = (u64)function->instruction_count * 256 + (u64)function->block_count * 192 + 192;
    u64 read_only_data_capacity = 0;
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->opcode == IR_OPCODE_CONSTANT_STRING)
        {
            read_only_data_capacity += ir_instruction_extra(function, instruction->id).literal.length;
        }
    }
    CodegenBuffer buffer = {
        .bytes = arena_allocate(arena, u8, capacity),
        .capacity = capacity,
        .value_registers = allocation.registers,
        .allocated_register_base = 9,
    };
    CodegenBuffer read_only_data = {
        .bytes = arena_allocate(arena, u8, read_only_data_capacity),
        .capacity = read_only_data_capacity,
    };
    u32* block_offsets = arena_allocate(arena, u32, function->block_count);
    A64Relocation* first_relocation = 0;
    A64Relocation* last_relocation = 0;
    CodegenCallRelocation* first_call_relocation = 0;
    CodegenCallRelocation* last_call_relocation = 0;
    CodegenDataRelocation* first_data_relocation = 0;
    CodegenDataRelocation* last_data_relocation = 0;
    u32 stack_chunk_size = target.os == OPERATING_SYSTEM_WINDOWS ? 4080 : 4095;
    u32 stack_action_count = frame_size / stack_chunk_size + (frame_size % stack_chunk_size != 0);
    u32 stack_action_capacity = target.os == OPERATING_SYSTEM_WINDOWS ? (frame_size > 4080 ? 14u : stack_action_count * 2) : stack_action_count;
    u32 unwind_action_capacity = 4 + stack_action_capacity;
    result.descriptor = (CodegenFunctionDescriptor){
        .unwind_actions = arena_allocate(arena, CodegenUnwindAction, unwind_action_capacity),
        .epilog_offsets = arena_allocate(arena, u32, function->instruction_count),
        .symbol = function->symbol,
    };
    a64_emit_instruction_word(&buffer, 0xa9bf7bfd);
    bool unwind_valid = codegen_unwind_action_append(&result.descriptor, unwind_action_capacity, (u32)buffer.count,
                                                     CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, 16);
    unwind_valid = codegen_unwind_action_append(&result.descriptor, unwind_action_capacity, (u32)buffer.count,
                                                CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 29, 0) &&
                   unwind_valid;
    unwind_valid = codegen_unwind_action_append(&result.descriptor, unwind_action_capacity, (u32)buffer.count,
                                                CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 30, 8) &&
                   unwind_valid;
    a64_emit_instruction_word(&buffer, 0x910003fd);
    unwind_valid = codegen_unwind_action_append(&result.descriptor, unwind_action_capacity, (u32)buffer.count,
                                                CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, 29, 0) &&
                   unwind_valid;
    if (!unwind_valid)
    {
        result.error = CODEGEN_ERROR_CAPACITY;
        return result;
    }
    if (frame_size)
    {
        a64_emit_stack_adjust_described(&buffer, frame_size, true, &result.descriptor, unwind_action_capacity,
                                        target.os == OPERATING_SYSTEM_WINDOWS);
    }
    result.descriptor.prolog_size = (u32)buffer.count;
    for (u32 index = 0; index < incoming_integer_register_count; index += 1)
    {
        a64_emit_store_offset(&buffer, index, incoming_integer_register_base + index * 8);
    }
    for (u32 index = 0; index < incoming_float_register_count; index += 1)
    {
        a64_emit_float_store_offset(&buffer, index, incoming_float_register_base + index * 16, 16);
    }
    if (function_signature.result.indirect)
    {
        a64_emit_store_offset(&buffer, 8, hidden_result_offset);
    }
    if (generated_function_type->as.function.is_variadic && (abi == CODEGEN_ABI_AARCH64_AAPCS64 || abi == CODEGEN_ABI_AARCH64_WINDOWS))
    {
        for (u32 index = 0; index < 8; index += 1)
        {
            a64_emit_store_offset(&buffer, index, va_register_save_base + index * 8);
            if (abi == CODEGEN_ABI_AARCH64_AAPCS64)
            {
                a64_emit_instruction_word(&buffer, 0xfd0003e0 | (((va_register_save_base + 64 + index * 16) / 8) << 10) | index);
            }
        }
    }
    u32 line_entry_capacity = function->instruction_count + 1;
    CodegenLineEntry* line_entries = record_lines ? arena_allocate(arena, CodegenLineEntry, line_entry_capacity) : 0;
    u32 line_entry_count = 0;
    ParserSourceRange declaration_range = codegen_analysis_declaration_range(analysis, function);
    // Parser lines are zero-based; recorded lines are one-based.
    codegen_record_line(line_entries, &line_entry_count, line_entry_capacity, 0, 0, declaration_range.line + 1, declaration_range.column + 1);
    for (u32 block_index = 0; block_index < function->block_count && buffer.error == CODEGEN_ERROR_NONE; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        block_offsets[block_index] = (u32)buffer.count;
        for (IrInstructionId id = block->first_instruction; id.value != IR_ID_UNDERLYING_INVALID; id = function->instructions[id.value].next)
        {
            IrInstruction* instruction = function->instructions + id.value;
            ParserSourceRange instruction_source = ir_instruction_source(function, id);
            // Parser lines are zero-based; recorded lines are one-based.
            codegen_record_line(line_entries, &line_entry_count, line_entry_capacity, (u32)buffer.count, 0, instruction_source.line + 1,
                                instruction_source.column + 1);
            switch (instruction->opcode)
            {
            case IR_OPCODE_LOCAL:
            {
                if (instruction->local.value >= function->local_count)
                {
                    buffer.error = CODEGEN_ERROR_CAPACITY;
                    break;
                }
                a64_emit_stack_address(&buffer, 0, local_storage_offsets[instruction->local.value]);
                a64_emit_store_value(&buffer, 0, instruction->result);
            }
            break;
            case IR_OPCODE_LOAD:
            {
                AnalysisType* type = analysis_type_from_id(analysis, instruction->type);
                a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                if (codegen_type_is_indirect_value(type))
                {
                    a64_emit_store_value(&buffer, 0, instruction->result);
                }
                else if (codegen_type_is_inline_collection(type))
                {
                    for (u32 component = 0; component < A64_VALUE_SLOT_COMPONENT_COUNT; component += 1)
                    {
                        a64_emit_instruction_word(&buffer, 0xf9400001 | (component << 10));
                        a64_emit_store_value_component(&buffer, 1, instruction->result, component);
                    }
                }
                else
                {
                    a64_emit_load_pointer(&buffer, 0, 0, codegen_type_storage_size(type));
                    a64_emit_store_value(&buffer, 0, instruction->result);
                }
            }
            break;
            case IR_OPCODE_STORE:
            {
                AnalysisType* type = analysis_type_from_id(analysis, function->values[instruction->operands[1].value].type);
                a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                if (codegen_type_is_indirect_value(type))
                {
                    a64_emit_load_value_component(&buffer, 1, instruction->operands[1], 0);
                    a64_emit_copy_memory(&buffer, codegen_type_storage_size(type));
                }
                else if (codegen_type_is_inline_collection(type))
                {
                    for (u32 component = 0; component < A64_VALUE_SLOT_COMPONENT_COUNT; component += 1)
                    {
                        a64_emit_load_value_component(&buffer, 1, instruction->operands[1], component);
                        a64_emit_instruction_word(&buffer, 0xf9000001 | (component << 10));
                    }
                }
                else
                {
                    a64_emit_load_value(&buffer, 1, instruction->operands[1]);
                    a64_emit_store_pointer(&buffer, 1, 0, codegen_type_storage_size(type));
                }
            }
            break;
            case IR_OPCODE_ARGUMENT:
            {
                if (!instruction->immediate_count)
                {
                    buffer.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                    break;
                }
                u32 argument = (u32)instruction->immediates[0];
                CodegenAbiSignature signature = codegen_classify_signature_for_target(arena, analysis, function->type, target);
                if (argument >= signature.argument_count)
                {
                    buffer.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                    break;
                }
                CodegenAbiLocation* location = signature.arguments + argument;
                AnalysisType* type = analysis_type_from_id(analysis, instruction->type);
                if (codegen_type_is_indirect_value(type) || codegen_type_is_inline_collection(type))
                {
                    if (codegen_type_is_indirect_value(type))
                    {
                        a64_emit_initialize_aggregate_result(&buffer, value_storage_offsets, instruction->result);
                    }
                    if (location->indirect)
                    {
                        CodegenAbiPart* part = location->parts;
                        if (part->kind == CODEGEN_ABI_LOCATION_STACK)
                        {
                            a64_emit_load_offset(&buffer, 16, frame_size + 16 + part->stack_offset);
                        }
                        else
                        {
                            a64_emit_load_offset(&buffer, 16, incoming_integer_register_base + part->index * 8);
                        }
                        a64_emit_load_value_component(&buffer, 17, instruction->result, 0);
                        a64_emit_copy_memory_registers(&buffer, 17, 16, 0, codegen_type_storage_size(type));
                        break;
                    }
                    if (codegen_type_is_indirect_value(type) && location->part_count && location->parts[0].kind == CODEGEN_ABI_LOCATION_STACK)
                    {
                        a64_emit_load_value_component(&buffer, 0, instruction->result, 0);
                        a64_emit_stack_address(&buffer, 1, frame_size + 16 + location->parts[0].stack_offset);
                        a64_emit_copy_memory(&buffer, codegen_type_storage_size(type));
                        break;
                    }
                    for (u32 part_index = 0; part_index < location->part_count; part_index += 1)
                    {
                        CodegenAbiPart* part = location->parts + part_index;
                        if (part->kind == CODEGEN_ABI_LOCATION_STACK)
                        {
                            a64_emit_load_offset(&buffer, 1, frame_size + 16 + part->stack_offset);
                        }
                        else if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                        {
                            a64_emit_float_load_offset(&buffer, 16, incoming_float_register_base + part->index * 16, part->size);
                        }
                        else
                        {
                            a64_emit_load_offset(&buffer, 17, incoming_integer_register_base + part->index * 8);
                        }
                        a64_emit_store_abi_part(&buffer, analysis, function, instruction->result, type, part, part->kind == CODEGEN_ABI_LOCATION_STACK ? 1 : 17,
                                                part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER ? 16 : part->index);
                    }
                    break;
                }
                if (location->part_count != 1 || (location->kind != CODEGEN_ABI_LOCATION_STACK && location->index >= 8))
                {
                    buffer.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                    break;
                }
                if (location->kind == CODEGEN_ABI_LOCATION_STACK)
                {
                    u32 incoming_offset = frame_size + 16 + location->stack_offset;
                    if (type->kind == ANALYSIS_TYPE_FLOAT)
                    {
                        u32 scale = type->as.float_bit_width == 32 ? 4 : 8;
                        if (incoming_offset / scale > 4095)
                        {
                            buffer.error = CODEGEN_ERROR_CAPACITY;
                            break;
                        }
                        a64_emit_instruction_word(&buffer, (type->as.float_bit_width == 32 ? 0xbd4003e0 : 0xfd4003e0) | ((incoming_offset / scale) << 10));
                        a64_emit_float_store_value(&buffer, 0, instruction->result, type->as.float_bit_width);
                    }
                    else
                    {
                        a64_emit_load_offset(&buffer, 0, incoming_offset);
                        a64_emit_store_value(&buffer, 0, instruction->result);
                    }
                }
                else if (type->kind == ANALYSIS_TYPE_FLOAT && location->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                {
                    a64_emit_float_load_offset(&buffer, 16, incoming_float_register_base + location->index * 16, type->as.float_bit_width == 32 ? 4 : 8);
                    a64_emit_float_store_value(&buffer, 16, instruction->result, type->as.float_bit_width);
                }
                else
                {
                    a64_emit_load_offset(&buffer, 16, incoming_integer_register_base + location->index * 8);
                    a64_emit_store_value(&buffer, 16, instruction->result);
                }
            }
            break;
            case IR_OPCODE_CONSTANT_INTEGER:
            case IR_OPCODE_CONSTANT_FLOAT:
            case IR_OPCODE_ENUM:
            {
                if (!instruction->immediate_count)
                {
                    buffer.error = CODEGEN_ERROR_INVALID_IR;
                    break;
                }
                u64 value = instruction->immediates[0];
                if (instruction->immediate_is_negative)
                {
                    value = 0 - value;
                }
                a64_emit_constant(&buffer, 0, value);
                a64_emit_store_value(&buffer, 0, instruction->result);
            }
            break;
            case IR_OPCODE_CONSTANT_STRING:
            {
                AnalysisType* type = analysis_type_from_id(analysis, instruction->type);
                String8 literal = ir_instruction_extra(function, instruction->id).literal;
                if (type->kind != ANALYSIS_TYPE_ARRAY || type->as.array.count != literal.length ||
                    literal.length > read_only_data.capacity - read_only_data.count)
                {
                    buffer.error = CODEGEN_ERROR_INVALID_IR;
                    break;
                }
                u32 data_offset = (u32)read_only_data.count;
                memcpy(read_only_data.bytes + read_only_data.count, literal.pointer, literal.length);
                read_only_data.count += literal.length;
                if (buffer.count & 7)
                {
                    a64_emit_instruction_word(&buffer, 0xd503201f);
                }
                a64_emit_instruction_word(&buffer, 0x58000040);
                a64_emit_instruction_word(&buffer, 0x14000003);
                CodegenDataRelocation* relocation = arena_allocate(arena, CodegenDataRelocation, 1);
                *relocation = (CodegenDataRelocation){
                    .code_offset = (u32)buffer.count,
                    .data_offset = data_offset,
                    .kind = CODEGEN_DATA_RELOCATION_ABSOLUTE64,
                };
                if (last_data_relocation)
                {
                    last_data_relocation->next = relocation;
                }
                else
                {
                    first_data_relocation = relocation;
                }
                last_data_relocation = relocation;
                codegen_emit_u64(&buffer, 0);
                a64_emit_store_value_component(&buffer, 0, instruction->result, 0);
                a64_emit_constant(&buffer, 0, literal.length);
                a64_emit_store_value_component(&buffer, 0, instruction->result, 1);
                a64_emit_constant(&buffer, 0, 1);
                a64_emit_store_value_component(&buffer, 0, instruction->result, 2);
                a64_emit_constant(&buffer, 0, 0);
                a64_emit_store_value_component(&buffer, 0, instruction->result, 3);
            }
            break;
            case IR_OPCODE_UNDEFINED:
                a64_emit_constant(&buffer, 0, 0);
                a64_emit_store_value(&buffer, 0, instruction->result);
                break;
            case IR_OPCODE_ARRAY:
            {
                AnalysisType* type = analysis_type_from_id(analysis, instruction->type);
                AnalysisTypeId element_type_id = type->kind == ANALYSIS_TYPE_ARRAY    ? type->as.array.element_type
                                                 : type->kind == ANALYSIS_TYPE_VECTOR ? type->as.vector.element_type
                                                                                      : type->as.element_type;
                AnalysisType* element_type = analysis_type_from_id(analysis, element_type_id);
                u32 element_size = codegen_type_storage_size(element_type);
                u32 storage = value_storage_offsets[instruction->result.value];
                if (!storage || !element_size)
                {
                    buffer.error = CODEGEN_ERROR_INVALID_IR;
                    break;
                }
                a64_emit_stack_address(&buffer, 0, storage);
                a64_emit_store_value_component(&buffer, 0, instruction->result, 0);
                for (u32 index = 0; index < instruction->operand_count; index += 1)
                {
                    a64_emit_stack_address(&buffer, 0, storage + index * element_size);
                    if (codegen_type_is_indirect_value(element_type))
                    {
                        a64_emit_load_value_component(&buffer, 1, instruction->operands[index], 0);
                        a64_emit_copy_memory(&buffer, element_size);
                    }
                    else
                    {
                        a64_emit_load_value(&buffer, 1, instruction->operands[index]);
                        a64_emit_store_pointer(&buffer, 1, 0, element_size);
                    }
                }
                a64_emit_constant(&buffer, 0, instruction->operand_count);
                a64_emit_store_value_component(&buffer, 0, instruction->result, 1);
                a64_emit_constant(&buffer, 0, element_size);
                a64_emit_store_value_component(&buffer, 0, instruction->result, 2);
                a64_emit_constant(&buffer, 0, 0);
                a64_emit_store_value_component(&buffer, 0, instruction->result, 3);
            }
            break;
            case IR_OPCODE_AGGREGATE:
            {
                AnalysisType* type = analysis_type_from_id(analysis, instruction->type);
                AnalysisEntitySemantic* semantic = codegen_type_semantic(analysis, type);
                u32 storage = value_storage_offsets[instruction->result.value];
                if (!semantic || !storage || instruction->immediate_count != instruction->operand_count)
                {
                    buffer.error = CODEGEN_ERROR_INVALID_IR;
                    break;
                }
                a64_emit_stack_address(&buffer, 0, storage);
                a64_emit_store_value(&buffer, 0, instruction->result);
                for (u32 index = 0; index < instruction->operand_count; index += 1)
                {
                    u64 field_index = instruction->immediates[index];
                    if (field_index >= semantic->field_count)
                    {
                        buffer.error = CODEGEN_ERROR_INVALID_IR;
                        break;
                    }
                    AnalysisField* field = semantic->fields + field_index;
                    AnalysisType* field_type = analysis_type_from_id(analysis, field->type);
                    u32 field_size = codegen_type_storage_size(field_type);
                    a64_emit_stack_address(&buffer, 0, storage + (u32)field->offset);
                    if (codegen_type_is_indirect_value(field_type))
                    {
                        a64_emit_load_value_component(&buffer, 1, instruction->operands[index], 0);
                        a64_emit_copy_memory(&buffer, field_size);
                    }
                    else
                    {
                        a64_emit_load_value(&buffer, 1, instruction->operands[index]);
                        a64_emit_store_pointer(&buffer, 1, 0, field_size);
                    }
                }
            }
            break;
            case IR_OPCODE_FUNCTION:
                if (codegen_value_requires_materialization(function, instruction->result))
                {
                    a64_call_relocation_add(arena, &buffer, &first_call_relocation, &last_call_relocation, instruction, true);
                }
                else
                {
                    a64_emit_constant(&buffer, 0, 0);
                }
                a64_emit_store_value(&buffer, 0, instruction->result);
                break;
            case IR_OPCODE_CALL:
            {
                if (!instruction->operand_count)
                {
                    buffer.error = CODEGEN_ERROR_INVALID_IR;
                    break;
                }
                AnalysisTypeId callee_type_id = function->values[instruction->operands[0].value].type;
                AnalysisType* callee_type = analysis_type_from_id(analysis, callee_type_id);
                bool indirect = callee_type->kind == ANALYSIS_TYPE_POINTER;
                AnalysisTypeId function_type_id = indirect ? callee_type->as.element_type : callee_type_id;
                AnalysisType* function_type = analysis_type_from_id(analysis, function_type_id);
                if (function_type->kind != ANALYSIS_TYPE_FUNCTION)
                {
                    buffer.error = CODEGEN_ERROR_INVALID_IR;
                    break;
                }
                u32 call_argument_count = instruction->operand_count - 1;
                AnalysisTypeId* call_argument_types = arena_allocate(arena, AnalysisTypeId, call_argument_count);
                for (u32 index = 0; index < call_argument_count; index += 1)
                {
                    call_argument_types[index] = function->values[instruction->operands[index + 1].value].type;
                }
                CodegenAbiSignature signature = codegen_classify_signature_with_arguments_prepared(
                    arena, analysis, function_type_id, function_type->as.function.is_variadic ? call_argument_types : 0, call_argument_count, target, false);
                if (!signature.valid)
                {
                    buffer.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                    break;
                }
                if (signature.argument_count != instruction->operand_count - 1)
                {
                    buffer.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                    break;
                }
                if (signature.stack_size > 4095)
                {
                    buffer.error = CODEGEN_ERROR_CAPACITY;
                    break;
                }
                AnalysisType* call_result_type = instruction->result.value != IR_ID_UNDERLYING_INVALID ? analysis_type_from_id(analysis, instruction->type) : 0;
                if (call_result_type && (codegen_type_is_indirect_value(call_result_type) || codegen_type_is_inline_collection(call_result_type)))
                {
                    if (codegen_type_is_indirect_value(call_result_type))
                    {
                        a64_emit_initialize_aggregate_result(&buffer, value_storage_offsets, instruction->result);
                    }
                    if (signature.result.indirect)
                    {
                        a64_emit_load_value_component(&buffer, 8, instruction->result, 0);
                    }
                }
                if (signature.stack_size)
                {
                    a64_emit_instruction_word(&buffer, 0xd10003ff | (signature.stack_size << 10));
                }
                for (u32 argument_index = 0; argument_index < signature.argument_count; argument_index += 1)
                {
                    CodegenAbiLocation* location = signature.arguments + argument_index;
                    IrValueId operand = instruction->operands[argument_index + 1];
                    AnalysisType* operand_type = analysis_type_from_id(analysis, function->values[operand.value].type);
                    if (location->indirect)
                    {
                        a64_emit_stack_address(&buffer, 0, location->indirect_copy_offset);
                        a64_emit_load_value_component(&buffer, 1, operand, 0);
                        a64_emit_copy_memory(&buffer, codegen_type_storage_size(operand_type));
                        CodegenAbiPart* part = location->parts;
                        if (part->kind == CODEGEN_ABI_LOCATION_STACK)
                        {
                            a64_emit_store_offset(&buffer, 0, part->stack_offset);
                        }
                        else if (part->index)
                        {
                            a64_emit_instruction_word(&buffer, 0xaa0003e0 | (0 << 16) | part->index);
                        }
                        continue;
                    }
                    if (codegen_type_is_indirect_value(operand_type) && location->part_count && location->parts[0].kind == CODEGEN_ABI_LOCATION_STACK)
                    {
                        a64_emit_stack_address(&buffer, 0, location->parts[0].stack_offset);
                        a64_emit_load_value_component(&buffer, 1, operand, 0);
                        a64_emit_copy_memory(&buffer, codegen_type_storage_size(operand_type));
                        continue;
                    }
                    if (codegen_type_is_indirect_value(operand_type) || codegen_type_is_inline_collection(operand_type))
                    {
                        for (u32 part_index = 0; part_index < location->part_count; part_index += 1)
                        {
                            CodegenAbiPart* part = location->parts + part_index;
                            if (part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                            {
                                a64_emit_load_abi_part(&buffer, analysis, function, operand, operand_type, part, 0, part->index);
                            }
                            else
                            {
                                a64_emit_load_abi_part(&buffer, analysis, function, operand, operand_type, part, 0, 0);
                                if (part->kind == CODEGEN_ABI_LOCATION_STACK)
                                {
                                    a64_emit_store_offset(&buffer, 0, part->stack_offset);
                                }
                                else if (part->index)
                                {
                                    a64_emit_instruction_word(&buffer, 0xaa0003e0 | part->index);
                                }
                            }
                        }
                        continue;
                    }
                    if (location->part_count != 1 || (location->kind != CODEGEN_ABI_LOCATION_STACK && location->index >= 8) ||
                        location->kind == CODEGEN_ABI_LOCATION_INDIRECT)
                    {
                        buffer.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                        break;
                    }
                    if (location->kind == CODEGEN_ABI_LOCATION_STACK)
                    {
                        if (operand_type->kind == ANALYSIS_TYPE_FLOAT)
                        {
                            a64_emit_float_load_value(&buffer, 0, operand, operand_type->as.float_bit_width);
                            u32 scale = operand_type->as.float_bit_width == 32 ? 4 : 8;
                            a64_emit_instruction_word(&buffer, (operand_type->as.float_bit_width == 32 ? 0xbd0003e0 : 0xfd0003e0) |
                                                                   ((location->stack_offset / scale) << 10));
                        }
                        else
                        {
                            a64_emit_load_value(&buffer, 0, operand);
                            a64_emit_store_offset(&buffer, 0, location->stack_offset);
                        }
                    }
                    else if (operand_type->kind == ANALYSIS_TYPE_FLOAT && location->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                    {
                        a64_emit_float_load_value(&buffer, location->index, operand, operand_type->as.float_bit_width);
                    }
                    else
                    {
                        a64_emit_load_value(&buffer, location->index, operand);
                    }
                }
                if (buffer.error != CODEGEN_ERROR_NONE)
                {
                    break;
                }
                if (indirect)
                {
                    a64_emit_load_value(&buffer, 16, instruction->operands[0]);
                    a64_emit_instruction_word(&buffer, 0xd63f0200);
                }
                else
                {
                    a64_call_relocation_add(arena, &buffer, &first_call_relocation, &last_call_relocation, instruction, false);
                }
                if (signature.stack_size)
                {
                    a64_emit_instruction_word(&buffer, 0x910003ff | (signature.stack_size << 10));
                }
                if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
                {
                    AnalysisType* result_type = analysis_type_from_id(analysis, instruction->type);
                    if (signature.result.indirect)
                    {
                        if (abi == CODEGEN_ABI_AARCH64_DARWIN)
                        {
                            a64_emit_load_value_component(&buffer, 0, instruction->result, 0);
                        }
                    }
                    else if (codegen_type_is_indirect_value(result_type) || codegen_type_is_inline_collection(result_type))
                    {
                        for (u32 part_index = 0; part_index < signature.result.part_count; part_index += 1)
                        {
                            CodegenAbiPart* part = signature.result.parts + part_index;
                            a64_emit_store_abi_part(&buffer, analysis, function, instruction->result, result_type, part, part->index, part->index);
                        }
                    }
                    else if (result_type->kind == ANALYSIS_TYPE_FLOAT)
                    {
                        a64_emit_float_store_value(&buffer, 0, instruction->result, result_type->as.float_bit_width);
                    }
                    else
                    {
                        a64_emit_store_value(&buffer, 0, instruction->result);
                    }
                }
            }
            break;
            case IR_OPCODE_UNARY:
            {
                AnalysisType* unary_type = analysis_type_from_id(analysis, instruction->type);
                if (unary_type->kind == ANALYSIS_TYPE_VECTOR)
                {
                    if (!target_cpu_feature_has(target, TARGET_CPU_FEATURE_AARCH64_NEON))
                    {
                        buffer.error = CODEGEN_ERROR_UNSUPPORTED_TARGET;
                        break;
                    }
                    if (unary_type->layout.size != 8 && unary_type->layout.size != 16 && unary_type->layout.size != 32 && unary_type->layout.size != 64)
                    {
                        buffer.error = CODEGEN_ERROR_UNSUPPORTED_TARGET;
                        break;
                    }
                    AnalysisType* element = analysis_type_from_id(analysis, unary_type->as.vector.element_type);
                    u32 width = element->kind == ANALYSIS_TYPE_FLOAT     ? element->as.float_bit_width
                                : element->kind == ANALYSIS_TYPE_INTEGER ? element->as.integer.bit_width
                                                                         : 0;
                    u32 target_register = 0;
                    if (unary_type->layout.size <= 16 && vector_allocation.registers[instruction->result.value] != CODEGEN_REGISTER_UNALLOCATED)
                    {
                        target_register = 2 + vector_allocation.registers[instruction->result.value];
                    }
                    a64_emit_initialize_aggregate_result(&buffer, value_storage_offsets, instruction->result);
                    a64_emit_load_value_component(&buffer, 0, instruction->operands[0], 0);
                    a64_emit_load_value_component(&buffer, 1, instruction->result, 0);
                    u32 encoded = 0;
                    if (instruction->unary_operation == IR_UNARY_VECTOR_FLOAT_NEGATE)
                    {
                        if (width != 32 && width != 64)
                        {
                            buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            break;
                        }
                        encoded = 0x2ea0f800 | (width == 64 ? 0x00400000 : 0);
                    }
                    else if (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE)
                    {
                        u32 size = width == 8 ? 0 : width == 16 ? 1 : width == 32 ? 2 : width == 64 ? 3 : UINT32_MAX;
                        if (size == UINT32_MAX)
                        {
                            buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            break;
                        }
                        encoded = 0x2e20b800 | (size << 22);
                    }
                    else if (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_BITWISE_NOT)
                    {
                        encoded = 0x2e205800;
                    }
                    else
                    {
                        buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                        break;
                    }
                    u32 chunk_count = (u32)((unary_type->layout.size + 15) / 16);
                    for (u32 chunk = 0; chunk < chunk_count; chunk += 1)
                    {
                        u32 chunk_size = unary_type->layout.size - (u64)chunk * 16 >= 16 ? 16 : 8;
                        u32 load = chunk_size == 16 ? 0x3dc00000 : 0xfd400000;
                        u32 store = chunk_size == 16 ? 0x3d800000 : 0xfd000000;
                        a64_emit_instruction_word(&buffer, load | target_register);
                        a64_emit_instruction_word(&buffer, encoded | (chunk_size == 16 ? 0x40000000 : 0) | (target_register << 5) | target_register);
                        a64_emit_instruction_word(&buffer, store | (1 << 5) | target_register);
                        if (chunk + 1 < chunk_count)
                        {
                            a64_emit_instruction_word(&buffer, 0x91004000);
                            a64_emit_instruction_word(&buffer, 0x91004021);
                        }
                    }
                    break;
                }
                if (instruction->unary_operation == IR_UNARY_FLOAT_NEGATE)
                {
                    u32 width = unary_type->as.float_bit_width;
                    a64_emit_float_load_value(&buffer, 0, instruction->operands[0], width);
                    a64_emit_instruction_word(&buffer, width == 32 ? 0x1e214000 : 0x1e614000);
                    a64_emit_float_store_value(&buffer, 0, instruction->result, width);
                    break;
                }
                a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                u32 encoded =
                    instruction->unary_operation == IR_UNARY_INTEGER_NEGATE                 ? 0xcb0003e0
                    : instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT          ? 0xaa2003e0
                    : instruction->unary_operation == IR_UNARY_BOOLEAN_NOT                  ? 0
                    : instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS  ? (unary_type->as.integer.bit_width > 32 ? 0xdac01000 : 0x5ac01000)
                    : instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS ? (unary_type->as.integer.bit_width > 32 ? 0xdac00000 : 0x5ac00000)
                                                                                            : UINT32_MAX;
                if (encoded == UINT32_MAX)
                {
                    buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                    break;
                }
                if (!encoded)
                {
                    a64_emit_instruction_word(&buffer, 0xf100001f);
                    encoded = 0x9a9f17e0;
                }
                else if (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS)
                {
                    a64_emit_instruction_word(&buffer, encoded);
                    encoded = unary_type->as.integer.bit_width > 32 ? 0xdac01000 : 0x5ac01000;
                }
                a64_emit_instruction_word(&buffer, encoded);
                a64_emit_store_value(&buffer, 0, instruction->result);
            }
            break;
            case IR_OPCODE_BINARY:
            {
                AnalysisType* binary_type = analysis_type_from_id(analysis, instruction->type);
                if (binary_type->kind == ANALYSIS_TYPE_VECTOR)
                {
                    if (!a64_emit_vector_binary(&buffer, analysis, target, function->values[instruction->operands[0].value].type, value_storage_offsets,
                                                vector_allocation.registers, instruction))
                    {
                        buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                    }
                    break;
                }
                if (codegen_binary_is_float(instruction->binary_operation))
                {
                    AnalysisType* operand_type = analysis_type_from_id(analysis, function->values[instruction->operands[0].value].type);
                    u32 width = operand_type->as.float_bit_width;
                    a64_emit_float_load_value(&buffer, 0, instruction->operands[0], width);
                    a64_emit_float_load_value(&buffer, 1, instruction->operands[1], width);
                    IrBinaryOperation operation = instruction->binary_operation;
                    if (operation >= IR_BINARY_FLOAT_ADD && operation <= IR_BINARY_FLOAT_DIVIDE)
                    {
                        u32 encoded = operation == IR_BINARY_FLOAT_ADD        ? 0x1e212800
                                      : operation == IR_BINARY_FLOAT_SUBTRACT ? 0x1e213800
                                      : operation == IR_BINARY_FLOAT_MULTIPLY ? 0x1e210800
                                                                              : 0x1e211800;
                        if (width == 64)
                        {
                            encoded |= 0x00400000;
                        }
                        a64_emit_instruction_word(&buffer, encoded);
                        a64_emit_float_store_value(&buffer, 0, instruction->result, width);
                        break;
                    }
                    a64_emit_instruction_word(&buffer, (width == 32 ? 0x1e212000 : 0x1e612000));
                    u32 condition = operation == IR_BINARY_FLOAT_EQUAL        ? 0
                                    : operation == IR_BINARY_FLOAT_NOT_EQUAL  ? 1
                                    : operation == IR_BINARY_FLOAT_LESS       ? 11
                                    : operation == IR_BINARY_FLOAT_LESS_EQUAL ? 13
                                    : operation == IR_BINARY_FLOAT_GREATER    ? 12
                                                                              : 10;
                    a64_emit_instruction_word(&buffer, 0x9a9f07e0 | ((condition ^ 1) << 12));
                    a64_emit_store_value(&buffer, 0, instruction->result);
                    break;
                }
                if (instruction->binary_operation == IR_BINARY_RANGE)
                {
                    a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                    a64_emit_store_value_component(&buffer, 0, instruction->result, 0);
                    a64_emit_load_value(&buffer, 0, instruction->operands[1]);
                    a64_emit_store_value_component(&buffer, 0, instruction->result, 1);
                    a64_emit_constant(&buffer, 0, 0);
                    a64_emit_store_value_component(&buffer, 0, instruction->result, 2);
                    break;
                }
                a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                a64_emit_load_value(&buffer, 1, instruction->operands[1]);
                IrBinaryOperation operation = instruction->binary_operation;
                if (operation == IR_BINARY_SIGNED_REMAINDER || operation == IR_BINARY_UNSIGNED_REMAINDER)
                {
                    a64_emit_instruction_word(&buffer, (operation == IR_BINARY_SIGNED_REMAINDER ? 0x9ac10c00 : 0x9ac10800) | 2);
                    a64_emit_instruction_word(&buffer, 0x9b018040);
                    a64_emit_store_value(&buffer, 0, instruction->result);
                    break;
                }
                u32 encoded = operation == IR_BINARY_INTEGER_ADD                                                 ? 0x8b010000
                              : operation == IR_BINARY_INTEGER_SUBTRACT                                          ? 0xcb010000
                              : operation == IR_BINARY_INTEGER_MULTIPLY                                          ? 0x9b017c00
                              : operation == IR_BINARY_SIGNED_DIVIDE                                             ? 0x9ac10c00
                              : operation == IR_BINARY_UNSIGNED_DIVIDE                                           ? 0x9ac10800
                              : operation == IR_BINARY_SHIFT_LEFT                                                ? 0x9ac12000
                              : operation == IR_BINARY_SIGNED_SHIFT_RIGHT                                        ? 0x9ac12800
                              : operation == IR_BINARY_UNSIGNED_SHIFT_RIGHT                                      ? 0x9ac12400
                              : operation == IR_BINARY_INTEGER_BITWISE_AND || operation == IR_BINARY_BOOLEAN_AND ? 0x8a010000
                              : operation == IR_BINARY_INTEGER_BITWISE_OR || operation == IR_BINARY_BOOLEAN_OR   ? 0xaa010000
                              : operation == IR_BINARY_INTEGER_BITWISE_XOR                                       ? 0xca010000
                                                                                                                 : 0;
                bool comparison = operation == IR_BINARY_INTEGER_EQUAL || operation == IR_BINARY_INTEGER_NOT_EQUAL || operation == IR_BINARY_POINTER_EQUAL ||
                                  operation == IR_BINARY_POINTER_NOT_EQUAL || operation == IR_BINARY_BOOLEAN_EQUAL ||
                                  operation == IR_BINARY_BOOLEAN_NOT_EQUAL ||
                                  (operation >= IR_BINARY_SIGNED_LESS && operation <= IR_BINARY_UNSIGNED_GREATER_EQUAL);
                if (comparison)
                {
                    a64_emit_instruction_word(&buffer, 0xeb01001f);
                    u32 condition =
                        operation == IR_BINARY_INTEGER_EQUAL || operation == IR_BINARY_BOOLEAN_EQUAL || operation == IR_BINARY_POINTER_EQUAL               ? 0
                        : operation == IR_BINARY_INTEGER_NOT_EQUAL || operation == IR_BINARY_BOOLEAN_NOT_EQUAL || operation == IR_BINARY_POINTER_NOT_EQUAL ? 1
                        : operation == IR_BINARY_SIGNED_LESS                                                                                               ? 11
                        : operation == IR_BINARY_SIGNED_LESS_EQUAL                                                                                         ? 13
                        : operation == IR_BINARY_SIGNED_GREATER                                                                                            ? 12
                        : operation == IR_BINARY_SIGNED_GREATER_EQUAL                                                                                      ? 10
                        : operation == IR_BINARY_UNSIGNED_LESS                                                                                             ? 3
                        : operation == IR_BINARY_UNSIGNED_LESS_EQUAL                                                                                       ? 9
                        : operation == IR_BINARY_UNSIGNED_GREATER                                                                                          ? 8
                                                                                                                                                           : 2;
                    encoded = 0x9a9f07e0 | ((condition ^ 1) << 12);
                }
                if (!encoded)
                {
                    buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                    break;
                }
                a64_emit_instruction_word(&buffer, encoded);
                a64_emit_store_value(&buffer, 0, instruction->result);
            }
            break;
            case IR_OPCODE_REVERSE:
            {
                AnalysisType* type = analysis_type_from_id(analysis, instruction->type);
                u32 reverse_component = type->kind == ANALYSIS_TYPE_RANGE ? 2 : 3;
                for (u32 component = 0; component < A64_VALUE_SLOT_COMPONENT_COUNT; component += 1)
                {
                    a64_emit_load_value_component(&buffer, 0, instruction->operands[0], component);
                    if (component == reverse_component)
                    {
                        a64_emit_instruction_word(&buffer, 0xd2400000);
                    }
                    a64_emit_store_value_component(&buffer, 0, instruction->result, component);
                }
            }
            break;
            case IR_OPCODE_LENGTH:
            {
                AnalysisType* base = analysis_type_from_id(analysis, function->values[instruction->operands[0].value].type);
                if (base->kind != ANALYSIS_TYPE_RANGE)
                {
                    a64_emit_collection_component(&buffer, analysis, function, instruction->operands[0], 1, 0);
                }
                else
                {
                    a64_emit_load_value_component(&buffer, 0, instruction->operands[0], 1);
                    a64_emit_load_value_component(&buffer, 1, instruction->operands[0], 0);
                    a64_emit_instruction_word(&buffer, 0xcb010000);
                }
                a64_emit_store_value(&buffer, 0, instruction->result);
            }
            break;
            case IR_OPCODE_VA_START:
            {
                u32 integer_register_count = function_signature.result.indirect ? 1 : 0;
                u32 float_register_count = 0;
                u32 stack_end = 0;
                for (u32 argument = 0; argument < generated_function_type->as.function.argument_count; argument += 1)
                {
                    CodegenAbiLocation* location = function_signature.arguments + argument;
                    for (u32 part = 0; part < location->part_count; part += 1)
                    {
                        CodegenAbiPart* abi_part = location->parts + part;
                        if (abi_part->kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER)
                        {
                            float_register_count = BUSTER_MAX(float_register_count, abi_part->index + 1);
                        }
                        else if (abi_part->kind == CODEGEN_ABI_LOCATION_INTEGER_REGISTER)
                        {
                            integer_register_count = BUSTER_MAX(integer_register_count, abi_part->index + 1);
                        }
                        else
                        {
                            stack_end = BUSTER_MAX(stack_end, abi_part->stack_offset + codegen_align_u32(abi_part->size, 8));
                        }
                    }
                }
                u32 incoming_offset = 16 + stack_end;
                if (incoming_offset > 4095)
                {
                    buffer.error = CODEGEN_ERROR_CAPACITY;
                    break;
                }
                a64_emit_instruction_word(&buffer, 0x91000000 | (incoming_offset << 10) | (29 << 5));
                a64_emit_store_value_component(&buffer, 0, instruction->result, 0);
                if (abi == CODEGEN_ABI_AARCH64_AAPCS64 || abi == CODEGEN_ABI_AARCH64_WINDOWS)
                {
                    a64_emit_stack_address(&buffer, 0, va_register_save_base + 64);
                    a64_emit_store_value_component(&buffer, 0, instruction->result, 1);
                    if (abi == CODEGEN_ABI_AARCH64_AAPCS64)
                    {
                        a64_emit_stack_address(&buffer, 0, va_register_save_base + 192);
                    }
                    else
                    {
                        a64_emit_constant(&buffer, 0, 0);
                    }
                    a64_emit_store_value_component(&buffer, 0, instruction->result, 2);
                    s32 gr_offset = -(s32)((8 - integer_register_count) * 8);
                    s32 vr_offset = abi == CODEGEN_ABI_AARCH64_AAPCS64 ? -(s32)((8 - float_register_count) * 16) : 0;
                    u64 packed = (u32)gr_offset | ((u64)(u32)vr_offset << 32);
                    a64_emit_constant(&buffer, 0, packed);
                    a64_emit_store_value_component(&buffer, 0, instruction->result, 3);
                }
                else
                {
                    a64_emit_constant(&buffer, 0, 0);
                    for (u32 component = 1; component < 4; component += 1)
                    {
                        a64_emit_store_value_component(&buffer, 0, instruction->result, component);
                    }
                }
            }
            break;
            case IR_OPCODE_VA_COPY:
            {
                a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                for (u32 component = 0; component < 4; component += 1)
                {
                    a64_emit_load_pointer_offset(&buffer, 1, 0, component * 8, 8);
                    a64_emit_store_value_component(&buffer, 1, instruction->result, component);
                }
            }
            break;
            case IR_OPCODE_VA_END:
            {
                a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                a64_emit_constant(&buffer, 1, 1);
                a64_emit_store_pointer_offset(&buffer, 1, 0, 24, 8);
            }
            break;
            case IR_OPCODE_VA_ARG:
            {
                AnalysisType* type = analysis_type_from_id(analysis, instruction->type);
                u32 size = codegen_type_storage_size(type);
                bool aggregate = codegen_type_is_indirect_value(type);
                bool scalar = type->kind == ANALYSIS_TYPE_INTEGER || type->kind == ANALYSIS_TYPE_FLOAT || type->kind == ANALYSIS_TYPE_BOOL ||
                              type->kind == ANALYSIS_TYPE_POINTER || type->kind == ANALYSIS_TYPE_ENUM;
                if (!size || (!aggregate && (!scalar || size > 8)))
                {
                    buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                    break;
                }
                AnalysisAbiConvention convention = codegen_analysis_abi_convention(abi);
                AnalysisAbiValue abi_value = abi == CODEGEN_ABI_AARCH64_WINDOWS
                                                 ? analysis_abi_value_classify_variadic_argument(arena, analysis, instruction->type, convention)
                                                 : analysis_abi_value_classify(arena, analysis, instruction->type, convention, false);
                if (!abi_value.part_count)
                {
                    buffer.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                    break;
                }
                if (aggregate)
                {
                    a64_emit_initialize_aggregate_result(&buffer, value_storage_offsets, instruction->result);
                    bool register_value = (abi == CODEGEN_ABI_AARCH64_AAPCS64 || abi == CODEGEN_ABI_AARCH64_WINDOWS) &&
                                          abi_value.parts[0].location != ANALYSIS_ABI_LOCATION_STACK;
                    u32 register_offset = 24;
                    u32 register_top_offset = 8;
                    u32 register_increment = 8;
                    if (register_value && abi_value.parts[0].abi_class == ANALYSIS_ABI_CLASS_FLOAT)
                    {
                        register_offset = 28;
                        register_top_offset = 16;
                        register_increment = 16;
                    }
                    u32 overflow_patch = 0;
                    if (register_value)
                    {
                        u32 required = abi_value.part_count * register_increment;
                        a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                        a64_emit_load_pointer_offset(&buffer, 1, 0, register_offset, 4);
                        a64_emit_instruction_word(&buffer, 0x11000023 | (required << 10));
                        a64_emit_instruction_word(&buffer, 0x7100007f);
                        overflow_patch = (u32)buffer.count;
                        a64_emit_instruction_word(&buffer, 0x5400000c);
                        a64_emit_store_pointer_offset(&buffer, 3, 0, register_offset, 4);
                        a64_emit_instruction_word(&buffer, 0x93407c21);
                        a64_emit_load_pointer_offset(&buffer, 2, 0, register_top_offset, 8);
                        a64_emit_instruction_word(&buffer, 0x8b010042);
                        if (abi_value.indirect)
                        {
                            a64_emit_load_pointer(&buffer, 1, 2, 8);
                            a64_emit_load_value_component(&buffer, 0, instruction->result, 0);
                            a64_emit_copy_memory(&buffer, size);
                        }
                        else
                        {
                            for (u32 part = 0; part < abi_value.part_count; part += 1)
                            {
                                AnalysisAbiPart* abi_part = abi_value.parts + part;
                                a64_emit_load_pointer_offset(&buffer, 3, 2, part * register_increment, abi_part->size);
                                a64_emit_load_value_component(&buffer, 0, instruction->result, 0);
                                a64_emit_store_pointer_offset(&buffer, 3, 0, abi_part->value_offset, abi_part->size);
                            }
                        }
                        u32 end_branch = (u32)buffer.count;
                        a64_emit_instruction_word(&buffer, 0x14000000);
                        u32 overflow_offset = (u32)buffer.count;
                        u32 test_instruction = 0x5400000c | (((overflow_offset - overflow_patch) / 4) << 5);
                        memcpy(buffer.bytes + overflow_patch, &test_instruction, sizeof(test_instruction));

                        a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                        a64_emit_constant(&buffer, 3, 0);
                        a64_emit_store_pointer_offset(&buffer, 3, 0, register_offset, 4);
                        a64_emit_load_pointer_offset(&buffer, 1, 0, 0, 8);
                        u32 alignment = abi_value.indirect ? 8 : (u32)BUSTER_MAX(type->layout.alignment, 8);
                        if (alignment > 8)
                        {
                            a64_emit_constant(&buffer, 3, alignment - 1);
                            a64_emit_instruction_word(&buffer, 0x8b030021);
                            a64_emit_constant(&buffer, 3, ~(u64)(alignment - 1));
                            a64_emit_instruction_word(&buffer, 0x8a030021);
                        }
                        a64_emit_instruction_word(&buffer, 0xaa0103e2);
                        u32 stack_size = codegen_align_u32(abi_value.indirect ? 8 : size, 8);
                        while (stack_size)
                        {
                            u32 chunk = BUSTER_MIN(stack_size, 4095);
                            a64_emit_instruction_word(&buffer, 0x91000042 | (chunk << 10));
                            stack_size -= chunk;
                        }
                        a64_emit_store_pointer_offset(&buffer, 2, 0, 0, 8);
                        if (abi_value.indirect)
                        {
                            a64_emit_load_pointer(&buffer, 1, 1, 8);
                        }
                        a64_emit_load_value_component(&buffer, 0, instruction->result, 0);
                        a64_emit_copy_memory(&buffer, size);
                        u32 end_offset = (u32)buffer.count;
                        u32 end_instruction = 0x14000000 | ((end_offset - end_branch) / 4);
                        memcpy(buffer.bytes + end_branch, &end_instruction, sizeof(end_instruction));
                        break;
                    }

                    a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                    a64_emit_load_pointer_offset(&buffer, 1, 0, 0, 8);
                    u32 alignment = abi_value.indirect                  ? 8
                                    : abi == CODEGEN_ABI_AARCH64_DARWIN ? (u32)BUSTER_MAX(type->layout.alignment, 1)
                                                                        : (u32)BUSTER_MAX(type->layout.alignment, 8);
                    if (alignment > 1)
                    {
                        a64_emit_constant(&buffer, 3, alignment - 1);
                        a64_emit_instruction_word(&buffer, 0x8b030021);
                        a64_emit_constant(&buffer, 3, ~(u64)(alignment - 1));
                        a64_emit_instruction_word(&buffer, 0x8a030021);
                    }
                    a64_emit_instruction_word(&buffer, 0xaa0103e2);
                    u32 stack_size = codegen_align_u32(abi_value.indirect ? 8 : size, abi == CODEGEN_ABI_AARCH64_DARWIN ? alignment : 8);
                    while (stack_size)
                    {
                        u32 chunk = BUSTER_MIN(stack_size, 4095);
                        a64_emit_instruction_word(&buffer, 0x91000042 | (chunk << 10));
                        stack_size -= chunk;
                    }
                    a64_emit_store_pointer_offset(&buffer, 2, 0, 0, 8);
                    if (abi_value.indirect)
                    {
                        a64_emit_load_pointer(&buffer, 1, 1, 8);
                    }
                    a64_emit_load_value_component(&buffer, 0, instruction->result, 0);
                    a64_emit_copy_memory(&buffer, size);
                    break;
                }
                a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                if (abi == CODEGEN_ABI_AARCH64_DARWIN)
                {
                    a64_emit_load_pointer_offset(&buffer, 1, 0, 0, 8);
                    u32 alignment = (u32)BUSTER_MAX(type->layout.alignment, 1);
                    if (alignment > 1)
                    {
                        a64_emit_constant(&buffer, 3, alignment - 1);
                        a64_emit_instruction_word(&buffer, 0x8b030021);
                        a64_emit_constant(&buffer, 3, ~(u64)(alignment - 1));
                        a64_emit_instruction_word(&buffer, 0x8a030021);
                    }
                    a64_emit_instruction_word(&buffer, 0xaa0103e2);
                    u32 stack_size = codegen_align_u32(size, alignment);
                    a64_emit_instruction_word(&buffer, 0x91000042 | (stack_size << 10));
                    a64_emit_store_pointer_offset(&buffer, 2, 0, 0, 8);
                }
                else
                {
                    bool floating = type->kind == ANALYSIS_TYPE_FLOAT && abi != CODEGEN_ABI_AARCH64_WINDOWS;
                    u32 offset = floating ? 28 : 24;
                    u32 top_offset = floating ? 16 : 8;
                    u32 increment = floating ? 16 : 8;
                    a64_emit_load_pointer_offset(&buffer, 1, 0, offset, 4);
                    u32 test_offset = (u32)buffer.count;
                    a64_emit_instruction_word(&buffer, 0x36f80001);
                    a64_emit_instruction_word(&buffer, 0x93407c21);
                    a64_emit_load_pointer_offset(&buffer, 2, 0, top_offset, 8);
                    a64_emit_instruction_word(&buffer, 0x8b010042);
                    a64_emit_instruction_word(&buffer, 0x11000021 | (increment << 10));
                    a64_emit_store_pointer_offset(&buffer, 1, 0, offset, 4);
                    u32 end_branch = (u32)buffer.count;
                    a64_emit_instruction_word(&buffer, 0x14000000);
                    u32 overflow_offset = (u32)buffer.count;
                    a64_emit_load_pointer_offset(&buffer, 2, 0, 0, 8);
                    a64_emit_instruction_word(&buffer, 0x91002043);
                    a64_emit_store_pointer_offset(&buffer, 3, 0, 0, 8);
                    u32 end_offset = (u32)buffer.count;
                    a64_emit_instruction_word(&buffer, 0xaa0203e1);
                    u32 test_instruction = 0x36f80001 | (((overflow_offset - test_offset) / 4) << 5);
                    u32 end_instruction = 0x14000000 | ((end_offset - end_branch) / 4);
                    memcpy(buffer.bytes + test_offset, &test_instruction, sizeof(test_instruction));
                    memcpy(buffer.bytes + end_branch, &end_instruction, sizeof(end_instruction));
                }
                a64_emit_load_pointer(&buffer, 0, 1, size);
                a64_emit_store_value(&buffer, 0, instruction->result);
            }
            break;
            case IR_OPCODE_INDEX:
            {
                AnalysisType* base = analysis_type_from_id(analysis, function->values[instruction->operands[0].value].type);
                if (base->kind != ANALYSIS_TYPE_RANGE)
                {
                    a64_emit_collection_component(&buffer, analysis, function, instruction->operands[0], 1, 1);
                    a64_emit_load_value(&buffer, 2, instruction->operands[1]);
                    a64_emit_collection_component(&buffer, analysis, function, instruction->operands[0], 3, 3);
                    a64_emit_instruction_word(&buffer, 0xcb020024);
                    a64_emit_instruction_word(&buffer, 0xd1000484);
                    a64_emit_instruction_word(&buffer, 0xf100007f);
                    a64_emit_instruction_word(&buffer, 0x9a821082);
                    a64_emit_collection_component(&buffer, analysis, function, instruction->operands[0], 2, 3);
                    a64_emit_instruction_word(&buffer, 0x9b037c42);
                    a64_emit_collection_component(&buffer, analysis, function, instruction->operands[0], 0, 0);
                    a64_emit_instruction_word(&buffer, 0x8b020000);
                    if (function->values[instruction->result.value].category == IR_VALUE_PLACE)
                    {
                        a64_emit_store_value(&buffer, 0, instruction->result);
                    }
                    else
                    {
                        AnalysisType* result_type = analysis_type_from_id(analysis, instruction->type);
                        if (!codegen_type_is_indirect_value(result_type))
                        {
                            a64_emit_load_pointer(&buffer, 0, 0, codegen_type_storage_size(result_type));
                        }
                        a64_emit_store_value(&buffer, 0, instruction->result);
                    }
                    break;
                }
                a64_emit_load_value_component(&buffer, 0, instruction->operands[0], 0);
                a64_emit_load_value_component(&buffer, 1, instruction->operands[0], 1);
                a64_emit_load_value(&buffer, 2, instruction->operands[1]);
                a64_emit_load_value_component(&buffer, 3, instruction->operands[0], 2);
                a64_emit_instruction_word(&buffer, 0xcb000021);
                a64_emit_instruction_word(&buffer, 0xcb020024);
                a64_emit_instruction_word(&buffer, 0xd1000484);
                a64_emit_instruction_word(&buffer, 0xf100007f);
                a64_emit_instruction_word(&buffer, 0x9a821082);
                a64_emit_load_value_component(&buffer, 0, instruction->operands[0], 0);
                a64_emit_instruction_word(&buffer, 0x8b020000);
                a64_emit_store_value(&buffer, 0, instruction->result);
            }
            break;
            case IR_OPCODE_SLICE:
            {
                if (instruction->immediate_count != 2)
                {
                    buffer.error = CODEGEN_ERROR_INVALID_IR;
                    break;
                }
                bool has_start = instruction->immediates[0] != 0;
                bool has_end = instruction->immediates[1] != 0;
                u32 operand_index = 1;
                if (has_start)
                {
                    a64_emit_load_value(&buffer, 1, instruction->operands[operand_index++]);
                }
                else
                {
                    a64_emit_constant(&buffer, 1, 0);
                }
                if (has_end)
                {
                    a64_emit_load_value(&buffer, 2, instruction->operands[operand_index]);
                }
                else
                {
                    a64_emit_collection_component(&buffer, analysis, function, instruction->operands[0], 1, 2);
                }
                a64_emit_instruction_word(&buffer, 0xcb010042);
                a64_emit_collection_component(&buffer, analysis, function, instruction->operands[0], 2, 3);
                a64_emit_instruction_word(&buffer, 0x9b037c21);
                a64_emit_collection_component(&buffer, analysis, function, instruction->operands[0], 0, 0);
                a64_emit_instruction_word(&buffer, 0x8b010000);
                a64_emit_store_value_component(&buffer, 0, instruction->result, 0);
                a64_emit_store_value_component(&buffer, 2, instruction->result, 1);
                a64_emit_store_value_component(&buffer, 3, instruction->result, 2);
                a64_emit_constant(&buffer, 0, 0);
                a64_emit_store_value_component(&buffer, 0, instruction->result, 3);
            }
            break;
            case IR_OPCODE_FIELD:
            {
                AnalysisType* base_type = analysis_type_from_id(analysis, function->values[instruction->operands[0].value].type);
                AnalysisEntitySemantic* semantic = codegen_type_semantic(analysis, base_type);
                if (!semantic || instruction->immediate_count != 1 || instruction->immediates[0] >= semantic->field_count)
                {
                    buffer.error = CODEGEN_ERROR_INVALID_IR;
                    break;
                }
                AnalysisField* field = semantic->fields + instruction->immediates[0];
                a64_emit_load_value_component(&buffer, 0, instruction->operands[0], 0);
                if (field->offset)
                {
                    a64_emit_constant(&buffer, 1, field->offset);
                    a64_emit_instruction_word(&buffer, 0x8b010000);
                }
                if (function->values[instruction->result.value].category == IR_VALUE_PLACE)
                {
                    a64_emit_store_value(&buffer, 0, instruction->result);
                }
                else
                {
                    AnalysisType* result_type = analysis_type_from_id(analysis, instruction->type);
                    if (!codegen_type_is_indirect_value(result_type))
                    {
                        a64_emit_load_pointer(&buffer, 0, 0, codegen_type_storage_size(result_type));
                    }
                    a64_emit_store_value(&buffer, 0, instruction->result);
                }
            }
            break;
            case IR_OPCODE_BRANCH:
                a64_emit_edge_copies(&buffer, function, block->id, instruction->targets[0], temporary_base);
                a64_relocation_add(arena, &buffer, &first_relocation, &last_relocation, instruction->targets[0], false);
                break;
            case IR_OPCODE_BRANCH_IF:
            {
                a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                u32 false_branch_offset = (u32)buffer.count;
                a64_emit_instruction_word(&buffer, 0xb4000000);
                a64_emit_edge_copies(&buffer, function, block->id, instruction->targets[0], temporary_base);
                a64_relocation_add(arena, &buffer, &first_relocation, &last_relocation, instruction->targets[0], false);
                s64 byte_delta = (s64)buffer.count - (s64)false_branch_offset;
                s64 instruction_delta = byte_delta / 4;
                if (instruction_delta >= (1 << 18))
                {
                    buffer.error = CODEGEN_ERROR_CAPACITY;
                    break;
                }
                u32 encoded = 0xb4000000 | (((u32)instruction_delta & 0x7ffff) << 5);
                memcpy(buffer.bytes + false_branch_offset, &encoded, sizeof(encoded));
                a64_emit_edge_copies(&buffer, function, block->id, instruction->targets[1], temporary_base);
                a64_relocation_add(arena, &buffer, &first_relocation, &last_relocation, instruction->targets[1], false);
            }
            break;
            case IR_OPCODE_SWITCH:
            {
                if (!instruction->operand_count || instruction->target_count != instruction->immediate_count + 1)
                {
                    buffer.error = CODEGEN_ERROR_INVALID_IR;
                    break;
                }
                for (u32 index = 0; index < instruction->immediate_count; index += 1)
                {
                    a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                    a64_emit_constant(&buffer, 1, instruction->immediates[index]);
                    a64_emit_instruction_word(&buffer, 0xeb01001f);
                    u32 next_case = (u32)buffer.count;
                    a64_emit_instruction_word(&buffer, 0x54000001);
                    a64_emit_edge_copies(&buffer, function, block->id, instruction->targets[index], temporary_base);
                    a64_relocation_add(arena, &buffer, &first_relocation, &last_relocation, instruction->targets[index], false);
                    s64 instruction_delta = ((s64)buffer.count - (s64)next_case) / 4;
                    if (instruction_delta >= (1 << 18))
                    {
                        buffer.error = CODEGEN_ERROR_CAPACITY;
                        break;
                    }
                    u32 encoded = 0x54000001 | (((u32)instruction_delta & 0x7ffff) << 5);
                    memcpy(buffer.bytes + next_case, &encoded, sizeof(encoded));
                }
                if (buffer.error != CODEGEN_ERROR_NONE)
                {
                    break;
                }
                IrBlockId default_target = instruction->targets[instruction->immediate_count];
                a64_emit_edge_copies(&buffer, function, block->id, default_target, temporary_base);
                a64_relocation_add(arena, &buffer, &first_relocation, &last_relocation, default_target, false);
            }
            break;
            case IR_OPCODE_ADDRESS_OF:
            case IR_OPCODE_DEREFERENCE:
                a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                a64_emit_store_value(&buffer, 0, instruction->result);
                break;
            case IR_OPCODE_CAST:
            {
                AnalysisType* source_type = analysis_type_from_id(analysis, function->values[instruction->operands[0].value].type);
                AnalysisType* target_type = analysis_type_from_id(analysis, instruction->type);
                IrConversionOperation conversion = instruction->conversion_operation;
                if (conversion == IR_CONVERSION_FLOAT_EXTEND || conversion == IR_CONVERSION_FLOAT_TRUNCATE)
                {
                    a64_emit_float_load_value(&buffer, 0, instruction->operands[0], source_type->as.float_bit_width);
                    a64_emit_instruction_word(&buffer, conversion == IR_CONVERSION_FLOAT_EXTEND ? 0x1e22c000 : 0x1e624000);
                    a64_emit_float_store_value(&buffer, 0, instruction->result, target_type->as.float_bit_width);
                }
                else if (conversion == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT || conversion == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT)
                {
                    a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                    u32 encoded = conversion == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT ? 0x9e220000 : 0x9e230000;
                    if (target_type->as.float_bit_width == 64)
                    {
                        encoded |= 0x00400000;
                    }
                    a64_emit_instruction_word(&buffer, encoded);
                    a64_emit_float_store_value(&buffer, 0, instruction->result, target_type->as.float_bit_width);
                }
                else if (conversion == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER || conversion == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER)
                {
                    a64_emit_float_load_value(&buffer, 0, instruction->operands[0], source_type->as.float_bit_width);
                    u32 encoded = conversion == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER ? 0x9e380000 : 0x9e390000;
                    if (source_type->as.float_bit_width == 64)
                    {
                        encoded |= 0x00400000;
                    }
                    a64_emit_instruction_word(&buffer, encoded);
                    a64_emit_store_value(&buffer, 0, instruction->result);
                }
                else
                {
                    a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                    a64_emit_store_value(&buffer, 0, instruction->result);
                }
            }
            break;
            case IR_OPCODE_RETURN:
            {
                if (instruction->operand_count)
                {
                    AnalysisType* return_type = analysis_type_from_id(analysis, function->values[instruction->operands[0].value].type);
                    IrValueId return_value = instruction->operands[0];
                    if (function_signature.result.indirect)
                    {
                        a64_emit_load_offset(&buffer, 0, hidden_result_offset);
                        a64_emit_load_value_component(&buffer, 1, return_value, 0);
                        a64_emit_copy_memory(&buffer, codegen_type_storage_size(return_type));
                    }
                    else if (codegen_type_is_indirect_value(return_type) || codegen_type_is_inline_collection(return_type))
                    {
                        for (u32 part_index = 0; part_index < function_signature.result.part_count; part_index += 1)
                        {
                            CodegenAbiPart* part = function_signature.result.parts + part_index;
                            a64_emit_load_abi_part(&buffer, analysis, function, return_value, return_type, part, part->index, part->index);
                        }
                    }
                    else if (return_type->kind == ANALYSIS_TYPE_FLOAT)
                    {
                        a64_emit_float_load_value(&buffer, 0, instruction->operands[0], return_type->as.float_bit_width);
                    }
                    else
                    {
                        a64_emit_load_value(&buffer, 0, instruction->operands[0]);
                    }
                }
                if (!codegen_epilog_offset_append(&result.descriptor, function->instruction_count, (u32)buffer.count))
                {
                    buffer.error = CODEGEN_ERROR_CAPACITY;
                    break;
                }
                if (frame_size)
                {
                    a64_emit_stack_adjust_described(&buffer, frame_size, false, 0, 0, target.os == OPERATING_SYSTEM_WINDOWS);
                }
                a64_emit_instruction_word(&buffer, 0xa8c17bfd);
                a64_emit_instruction_word(&buffer, 0xd65f03c0);
            }
            break;
            case IR_OPCODE_DEBUG_TRAP:
                a64_emit_instruction_word(&buffer, 0xd4200000);
                break;
            case IR_OPCODE_UNREACHABLE:
                a64_emit_instruction_word(&buffer, 0xd4200000);
                break;
            default:
                buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                break;
            }
            if (buffer.error != CODEGEN_ERROR_NONE)
            {
                break;
            }
        }
    }
    for (A64Relocation* relocation = first_relocation; relocation && buffer.error == CODEGEN_ERROR_NONE; relocation = relocation->next)
    {
        if (relocation->target.value >= function->block_count)
        {
            buffer.error = CODEGEN_ERROR_INVALID_IR;
            break;
        }
        s64 byte_delta = (s64)block_offsets[relocation->target.value] - (s64)relocation->instruction_offset;
        s64 instruction_delta = byte_delta / 4;
        if (byte_delta % 4 || instruction_delta < -(1 << 25) || instruction_delta >= (1 << 25))
        {
            buffer.error = CODEGEN_ERROR_CAPACITY;
            break;
        }
        u32 encoded = 0x14000000 | ((u32)instruction_delta & 0x03ffffff);
        memcpy(buffer.bytes + relocation->instruction_offset, &encoded, sizeof(encoded));
    }
    result.code = (ByteSlice){
        .pointer = buffer.bytes,
        .length = buffer.count,
    };
    result.descriptor.code_size = (u32)result.code.length;
    result.error = buffer.error;
    result.stack_frame_size = frame_size;
    result.first_call_relocation = first_call_relocation;
    result.read_only_data = (ByteSlice){
        .pointer = read_only_data.bytes,
        .length = read_only_data.count,
    };
    result.first_data_relocation = first_data_relocation;
    result.line_entries = line_entries;
    result.line_entry_count = line_entry_count;
    result.register_value_count = allocation.allocated_count + vector_allocation.allocated_count;
    result.spilled_value_count = allocation.spilled_count + vector_allocation.spilled_count;
    if (record_lines)
    {
        codegen_record_analysis_locations(arena, &result, analysis, function, local_storage_offsets, value_storage_offsets,
                                          allocation.registers, vector_allocation.registers, block_offsets, target, false, frame_size);
    }
    return result;
}

CodegenFunction codegen_generate_function(Arena* arena, AnalysisResult* analysis, IrFunction* function, Target target)
{
    CodegenFunction result = {
        .error = CODEGEN_ERROR_UNSUPPORTED_TARGET,
        .abi = codegen_abi_for_target(target),
    };
    if (!analysis || !function || function->state != IR_FUNCTION_LOWERED)
    {
        result.error = CODEGEN_ERROR_INVALID_IR;
        return result;
    }
    analysis_compute_layouts(analysis, (AnalysisLayoutOptions){
                                           .pointer_size = 8,
                                           .pointer_alignment = 8,
                                       });
    IrModule validation_module = {
        .functions = function,
        .function_count = 1,
    };
    IrValidationResult validation = ir_validate_module(analysis, &validation_module);
    if (validation.error != IR_VALIDATION_NONE)
    {
        result.error = CODEGEN_ERROR_INVALID_IR;
        return result;
    }
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        return codegen_generate_x86_64(arena, analysis, function, target, false);
    }
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        return codegen_generate_aarch64(arena, analysis, function, target, false);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void codegen_function_descriptor_copy(Arena* arena, CodegenFunctionDescriptor* destination,
                                                           CodegenFunctionDescriptor const* source, u32 code_offset);
BUSTER_GLOBAL_LOCAL u64 codegen_module_lane_count(CodegenModuleOptions options, u32 function_count);

typedef struct CodegenModuleParallelState CodegenModuleParallelState;
struct CodegenModuleParallelState
{
    Arena* arena;
    Arena* fragment_arena;
    OsMutexHandle* fragment_mutex;
    AnalysisResult* analysis;
    IrModule* module;
    CodegenFunction* generated;
    AtomicU64 take_index;
    AtomicU64 worker_arena_failures;
    CodegenModule result;
    Target target;
    CodegenModuleOptions options;
    u64 worker_arena_reserve;
    u64 worker_arena_granularity;
};

Arena* codegen_worker_arena_create(u64 reserved_size, u64 granularity)
{
    return arena_create((ArenaCreation){
        .reserved_size = reserved_size,
        .granularity = granularity,
        // A caller may deliberately use a small arena to exercise an empty or
        // capacity-limited module. Creating the parallel scaffolding must not
        // reject a reservation the public serial entry point already accepts.
        .initial_size = BUSTER_MIN(BUSTER_KB(64), reserved_size),
        .flags.no_pool = true,
    });
}

BUSTER_GLOBAL_LOCAL bool codegen_worker_arena_reset(Arena** arena, u64 reserved_size, u64 granularity)
{
    // Rewind ordinary functions without a syscall. An outlier that committed
    // substantial transient storage is unmapped so all lanes do not retain
    // their respective high-water marks until the module merge.
    if ((*arena)->os_position <= BUSTER_MB(16))
    {
        arena_reset_to_start(*arena);
        return true;
    }
    BUSTER_CHECK(arena_destroy(*arena, 1));
    *arena = codegen_worker_arena_create(reserved_size, granularity);
    return *arena != 0;
}

BUSTER_GLOBAL_LOCAL void codegen_fragment_arena_align(Arena* arena)
{
    // Each exact fragment consumes a 64-byte-rounded extent. Consequently the
    // shared arena's total footprint is independent of mutex acquisition order
    // even though workers finish in an intentionally nondeterministic order.
    arena_allocate_bytes(arena, 0, 64);
}

BUSTER_GLOBAL_LOCAL ByteSlice codegen_fragment_bytes_copy(Arena* arena, ByteSlice source)
{
    ByteSlice result = {.length = source.length};
    if (source.length)
    {
        result.pointer = arena_allocate(arena, u8, source.length);
        memcpy(result.pointer, source.pointer, source.length);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CodegenFunction codegen_function_fragment_compact(Arena* arena, CodegenFunction source)
{
    CodegenFunction result = source;
    result.code = (ByteSlice){0};
    result.read_only_data = (ByteSlice){0};
    result.descriptor.unwind_actions = 0;
    result.descriptor.epilog_offsets = 0;
    result.first_call_relocation = 0;
    result.first_data_relocation = 0;
    result.line_entries = 0;
    result.debug_locations = 0;
    if (source.error != CODEGEN_ERROR_NONE)
    {
        return result;
    }
    result.code = codegen_fragment_bytes_copy(arena, source.code);
    result.read_only_data = codegen_fragment_bytes_copy(arena, source.read_only_data);
    codegen_function_descriptor_copy(arena, &result.descriptor, &source.descriptor, 0);
    CodegenCallRelocation** call_relocation = &result.first_call_relocation;
    for (CodegenCallRelocation* source_relocation = source.first_call_relocation; source_relocation; source_relocation = source_relocation->next)
    {
        *call_relocation = arena_allocate(arena, CodegenCallRelocation, 1);
        **call_relocation = *source_relocation;
        (*call_relocation)->next = 0;
        call_relocation = &(*call_relocation)->next;
    }
    CodegenDataRelocation** data_relocation = &result.first_data_relocation;
    for (CodegenDataRelocation* source_relocation = source.first_data_relocation; source_relocation; source_relocation = source_relocation->next)
    {
        *data_relocation = arena_allocate(arena, CodegenDataRelocation, 1);
        **data_relocation = *source_relocation;
        (*data_relocation)->next = 0;
        data_relocation = &(*data_relocation)->next;
    }
    if (source.line_entry_count)
    {
        result.line_entries = arena_allocate(arena, CodegenLineEntry, source.line_entry_count);
        memcpy(result.line_entries, source.line_entries, sizeof(*source.line_entries) * source.line_entry_count);
    }
    if (source.debug_location_count)
    {
        result.debug_locations = arena_allocate(arena, DebugLocationSeed, source.debug_location_count);
        memcpy(result.debug_locations, source.debug_locations, sizeof(*source.debug_locations) * source.debug_location_count);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CodegenModule codegen_module_fragments_merge(Arena* arena, IrModule* module, CodegenFunction* generated, Target target,
                                                                  CodegenModuleOptions options)
{
    CodegenModule result = {
        .ir_module = module,
        .abi = codegen_abi_for_target(target),
        .failed_function = IR_FUNCTION_ID_INVALID,
        .failed_instruction = IR_INSTRUCTION_ID_INVALID,
        .failed_opcode = IR_OPCODE_COUNT,
    };
    result.entries = arena_allocate(arena, CodegenModuleEntry, module->function_count);
    result.functions = arena_allocate(arena, CodegenFunctionDescriptor, module->function_count);
    u64 total_size = 0;
    u64 total_read_only_data_size = 0;
    u32 relocation_capacity = 0;
    u32 data_relocation_capacity = 0;
    u32 line_entry_capacity = 0;
    u32 debug_location_capacity = 0;
    for (u32 index = 0; index < module->function_count; index += 1)
    {
        IrFunction* function = module->functions + index;
        if (function->state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        // The machine selector does not exist yet: a non-NONE allocator mode
        // still emits through the direct backend, counted as a fallback.
        result.statistics.fallback_function_count += options.register_allocator != CODEGEN_REGISTER_ALLOCATOR_NONE;
        if (generated[index].error != CODEGEN_ERROR_NONE)
        {
            result.error = generated[index].error;
            return result;
        }
        for (CodegenCallRelocation* relocation = generated[index].first_call_relocation; relocation; relocation = relocation->next)
        {
            relocation_capacity += 1;
        }
        line_entry_capacity += generated[index].line_entry_count;
        debug_location_capacity += generated[index].debug_location_count;
        if (total_read_only_data_size > UINT32_MAX)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        total_read_only_data_size = codegen_align_u32((u32)total_read_only_data_size, 16);
        total_read_only_data_size += generated[index].read_only_data.length;
        for (CodegenDataRelocation* relocation = generated[index].first_data_relocation; relocation; relocation = relocation->next)
        {
            data_relocation_capacity += 1;
        }
        if (total_size > UINT32_MAX)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        total_size = codegen_align_u32((u32)total_size, 16);
        result.entries[result.entry_count++] = (CodegenModuleEntry){
            .entity = function->entity,
            .instantiation = function->instantiation,
            .symbol = function->symbol,
            .offset = (u32)total_size,
        };
        codegen_function_descriptor_copy(arena, result.functions + result.function_count, &generated[index].descriptor, (u32)total_size);
        result.function_count += 1;
        total_size += generated[index].code.length;
        if (total_size > UINT32_MAX)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
    }
    CodegenBuffer buffer = {
        .bytes = arena_allocate(arena, u8, total_size),
        .capacity = total_size,
    };
    result.relocations = arena_allocate(arena, CodegenModuleRelocation, relocation_capacity);
    result.data_relocations = arena_allocate(arena, CodegenModuleDataRelocation, data_relocation_capacity);
    result.line_entries = arena_allocate(arena, CodegenLineEntry, line_entry_capacity);
    result.debug_locations = arena_allocate(arena, DebugLocationSeed, debug_location_capacity);
    result.debug_info = options.debug_info;
    CodegenBuffer read_only_data_buffer = {
        .bytes = arena_allocate(arena, u8, total_read_only_data_size),
        .capacity = total_read_only_data_size,
    };
    u32 entry_index = 0;
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        CodegenFunction* function = generated + function_index;
        if (module->functions[function_index].state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        while (buffer.count < result.entries[entry_index].offset)
        {
            codegen_emit_u8(&buffer, 0x90);
        }
        u32 function_offset = (u32)buffer.count;
        while (read_only_data_buffer.count & 15)
        {
            codegen_emit_u8(&read_only_data_buffer, 0);
        }
        u32 function_data_offset = (u32)read_only_data_buffer.count;
        for (u64 byte_index = 0; byte_index < function->read_only_data.length; byte_index += 1)
        {
            codegen_emit_u8(&read_only_data_buffer, function->read_only_data.pointer[byte_index]);
        }
        for (u64 byte_index = 0; byte_index < function->code.length; byte_index += 1)
        {
            codegen_emit_u8(&buffer, function->code.pointer[byte_index]);
        }
        for (CodegenCallRelocation* relocation = function->first_call_relocation; relocation; relocation = relocation->next)
        {
            result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                .entity = relocation->entity,
                .instantiation = relocation->instantiation,
                .offset = function_offset + relocation->displacement_offset,
                .aarch64 = relocation->aarch64,
                .absolute = relocation->absolute,
            };
            CodegenModuleEntry* target_entry = 0;
            for (u32 target_index = 0; target_index < result.entry_count; target_index += 1)
            {
                CodegenModuleEntry* candidate = result.entries + target_index;
                if (candidate->entity.module.value == relocation->entity.module.value && candidate->entity.index.value == relocation->entity.index.value &&
                    candidate->instantiation.value == relocation->instantiation.value)
                {
                    target_entry = candidate;
                    break;
                }
            }
            if (!target_entry)
            {
                continue;
            }
            u32 displacement_offset = function_offset + relocation->displacement_offset;
            s64 displacement = (s64)target_entry->offset - (s64)(relocation->aarch64 ? displacement_offset : displacement_offset + 4);
            if (relocation->absolute)
            {
                memset(buffer.bytes + displacement_offset, 0, 8);
            }
            else if (relocation->aarch64)
            {
                s64 instruction_delta = displacement / 4;
                if (displacement % 4 || instruction_delta < -(1 << 25) || instruction_delta >= (1 << 25))
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
                u32 encoded = 0x94000000 | ((u32)instruction_delta & 0x03ffffff);
                memcpy(buffer.bytes + displacement_offset, &encoded, sizeof(encoded));
            }
            else
            {
                if (displacement < INT32_MIN || displacement > INT32_MAX)
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
                s32 displacement_32 = (s32)displacement;
                memcpy(buffer.bytes + displacement_offset, &displacement_32, sizeof(displacement_32));
            }
        }
        for (CodegenDataRelocation* relocation = function->first_data_relocation; relocation; relocation = relocation->next)
        {
            result.data_relocations[result.data_relocation_count++] = (CodegenModuleDataRelocation){
                .code_offset = function_offset + relocation->code_offset,
                .data_offset = function_data_offset + relocation->data_offset,
                .kind = relocation->kind,
            };
        }
        for (u32 line_index = 0; line_index < function->line_entry_count; line_index += 1)
        {
            CodegenLineEntry entry = function->line_entries[line_index];
            entry.code_offset += function_offset;
            result.line_entries[result.line_entry_count++] = entry;
        }
        for (u32 location_index = 0; location_index < function->debug_location_count; location_index += 1)
        {
            DebugLocationSeed location = function->debug_locations[location_index];
            location.start += function_offset;
            location.end += function_offset;
            result.debug_locations[result.debug_location_count++] = location;
        }
        entry_index += 1;
    }
    result.code = (ByteSlice){
        .pointer = buffer.bytes,
        .length = buffer.count,
    };
    result.read_only_data = (ByteSlice){
        .pointer = read_only_data_buffer.bytes,
        .length = read_only_data_buffer.count,
    };
    result.error = buffer.error;
    return result;
}

BUSTER_GLOBAL_LOCAL ThreadReturnType codegen_module_parallel_lane(void* argument)
{
    CodegenModuleParallelState* state = (CodegenModuleParallelState*)argument;
    Arena* emission_arena = codegen_worker_arena_create(state->worker_arena_reserve, state->worker_arena_granularity);
    if (!emission_arena)
    {
        atomic_u64_increment(&state->worker_arena_failures);
    }
    while (emission_arena)
    {
        u64 function_index = atomic_u64_increment(&state->take_index);
        if (function_index >= state->module->function_count)
        {
            break;
        }
        IrFunction* function = state->module->functions + function_index;
        if (function->state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        CodegenFunction emitted =
            state->target.cpu_arch == CPU_ARCH_X86_64
                ? codegen_generate_x86_64(emission_arena, state->analysis, function, state->target, state->options.debug_info)
            : state->target.cpu_arch == CPU_ARCH_AARCH64
                ? codegen_generate_aarch64(emission_arena, state->analysis, function, state->target, state->options.debug_info)
                : (CodegenFunction){
                      .error = CODEGEN_ERROR_UNSUPPORTED_TARGET,
                  };
        os_mutex_lock(state->fragment_mutex);
        codegen_fragment_arena_align(state->fragment_arena);
        state->generated[function_index] = codegen_function_fragment_compact(state->fragment_arena, emitted);
        codegen_fragment_arena_align(state->fragment_arena);
        os_mutex_unlock(state->fragment_mutex);
        if (!codegen_worker_arena_reset(&emission_arena, state->worker_arena_reserve, state->worker_arena_granularity))
        {
            atomic_u64_increment(&state->worker_arena_failures);
        }
    }
    if (emission_arena)
    {
        BUSTER_CHECK(arena_destroy(emission_arena, 1));
    }
    lane_sync();
    if (lane_index() == 0)
    {
        if (state->worker_arena_failures)
        {
            state->result.error = CODEGEN_ERROR_CAPACITY;
        }
        else
        {
            state->result = codegen_module_fragments_merge(state->arena, state->module, state->generated, state->target, state->options);
        }
    }
    lane_sync();
}

CodegenModule codegen_generate_module(Arena* arena, AnalysisResult* analysis, IrModule* module, Target target, CodegenModuleOptions options)
{
    CodegenModule result = {
        .ir_module = module,
        .abi = codegen_abi_for_target(target),
        .failed_function = IR_FUNCTION_ID_INVALID,
        .failed_instruction = IR_INSTRUCTION_ID_INVALID,
        .failed_opcode = IR_OPCODE_COUNT,
    };
    if (!analysis || !module || result.abi >= CODEGEN_ABI_COUNT)
    {
        result.error = CODEGEN_ERROR_INVALID_IR;
        return result;
    }
    // Freeze the first-use ABI target table before this call can leave a
    // resident gang that would make a later public classification too late.
    codegen_prewarm();
    analysis_compute_layouts(analysis, (AnalysisLayoutOptions){
                                           .pointer_size = 8,
                                           .pointer_alignment = 8,
                                       });
    IrValidationResult validation = ir_validate_module(analysis, module);
    if (validation.error != IR_VALIDATION_NONE)
    {
        result.error = CODEGEN_ERROR_INVALID_IR;
        return result;
    }
    CodegenFunction* generated = arena_allocate(arena, CodegenFunction, module->function_count);
    if (module->function_count)
    {
        memset(generated, 0, sizeof(*generated) * module->function_count);
    }
    Arena* fragment_arena = codegen_worker_arena_create(arena->reserved_size, arena->granularity);
    OsMutexHandle* fragment_mutex = os_mutex_create();
    if (!fragment_arena || !fragment_mutex)
    {
        if (fragment_mutex)
        {
            os_mutex_destroy(fragment_mutex);
        }
        if (fragment_arena)
        {
            BUSTER_CHECK(arena_destroy(fragment_arena, 1));
        }
        result.error = CODEGEN_ERROR_CAPACITY;
        return result;
    }
    CodegenModuleParallelState state = {
        .arena = arena,
        .fragment_arena = fragment_arena,
        .fragment_mutex = fragment_mutex,
        .analysis = analysis,
        .module = module,
        .generated = generated,
        .result = result,
        .target = target,
        .options = options,
        .worker_arena_reserve = arena->reserved_size,
        .worker_arena_granularity = arena->granularity,
    };
    lane_run(codegen_module_lane_count(options, module->function_count), &codegen_module_parallel_lane, &state);
    os_mutex_destroy(fragment_mutex);
    BUSTER_CHECK(arena_destroy(fragment_arena, 1));
    return state.result;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_register_is_64_bit(IrProgram* program, IrTypeId type_id)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    return type && (type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_FUNCTION || (type->kind == IR_TYPE_INTEGER && type->bit_width > 32) ||
                    (type->kind == IR_TYPE_FLOAT && type->bit_width > 32));
}

BUSTER_GLOBAL_LOCAL IrAbiConvention codegen_canonical_ir_abi_convention(CodegenAbi abi)
{
    return ir_abi_convention_for_target(codegen_target_for_abi(abi));
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_abi_part_is_float(IrAbiClass abi_class)
{
    return abi_class == IR_ABI_CLASS_FLOAT || abi_class == IR_ABI_CLASS_VECTOR;
}

// How many consecutive vector registers one ABI part occupies on this target,
// and how much of it each one carries. The IR ABI classifies a vector by the
// psABI rule alone -- a 512-bit vector is one vector part whatever the machine
// -- so a part can be wider than any register the target owns. It then travels
// in as many registers as it takes, which is the lowering clang emits for the
// same declaration: xmm0 through xmm3 for a 64-byte vector without AVX, ymm0
// and ymm1 with it. Zero means the target cannot carry the part at all.
BUSTER_GLOBAL_LOCAL u32 codegen_canonical_x64_vector_part_registers(Target const* target, u32 size, u32* register_size)
{
    // Every x86-64 target has a sixteen-byte vector register -- the psABI puts
    // one in the baseline -- so only a part wider than that has to ask the
    // target what it owns, and every part of a scalar signature can skip a
    // question whose answer is a feature-set walk.
    if (size && size <= 16)
    {
        *register_size = size;
        return 1;
    }
    u32 width = target_vector_register_size(*target);
    if (!width || !size)
    {
        return 0;
    }
    if (size <= width)
    {
        *register_size = size;
        return 1;
    }
    if (size % width)
    {
        return 0;
    }
    *register_size = width;
    return size / width;
}

// Whether this target hands the value over in the registers the classification
// named. A part it has to split is one the psABI expected a single register to
// hold, and the split is only available to a return, whose registers are its
// own; an argument competing for the shared pool is passed in memory instead,
// which is again what clang does for the same declaration.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_abi_value_in_registers(CodegenCanonicalAbiValue const* value, Target const* target)
{
    for (u32 part_index = 0; part_index < value->part_count; part_index += 1)
    {
        CodegenCanonicalAbiPart const* part = value->parts + part_index;
        u32 register_size = 0;
        if (part->size > 16 && codegen_canonical_abi_part_is_float(part->abi_class) &&
            codegen_canonical_x64_vector_part_registers(target, part->size, &register_size) != 1)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL CodegenCanonicalAbiValue codegen_canonical_aggregate_abi(IrProgram* program, IrTypeId type_id, CodegenAbi abi, bool is_result,
                                                                             bool variadic_argument)
{
    BUSTER_CHECK(abi < CODEGEN_ABI_COUNT);
    IrAbiConvention convention = codegen_canonical_ir_abi_convention(abi);
    IrAbiUse use = is_result ? IR_ABI_USE_RESULT : variadic_argument ? IR_ABI_USE_VARIADIC_ARGUMENT : IR_ABI_USE_ARGUMENT;
    return ir_type_abi_value(program, type_id, convention, use);
}

BUSTER_GLOBAL_LOCAL u32 codegen_canonical_va_list_component_count(IrProgram* program, IrTypeId type_id)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    if (!type || type->kind != IR_TYPE_VA_LIST || !type->layout.resolved || !type->layout.size || type->layout.size > 32 || (type->layout.size & 7))
    {
        return 0;
    }
    return (u32)(type->layout.size / 8);
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_integer_aggregate_parts(IrProgram* program, IrTypeId type_id, u32* part_count)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    if (type && type->kind == IR_TYPE_INTEGER && type->layout.resolved && type->bit_width > 64 && type->bit_width <= 128 && type->layout.size <= 16)
    {
        *part_count = (u32)((type->layout.size + 7) / 8);
        return true;
    }
    if (type && type->kind == IR_TYPE_VA_LIST && type->layout.resolved && type->layout.size > 8 && type->layout.size <= 32 && !(type->layout.size & 7))
    {
        *part_count = (u32)(type->layout.size / 8);
        return true;
    }
    if (type && type->kind == IR_TYPE_VECTOR && type->layout.resolved && type->layout.size && type->layout.size <= 64)
    {
        *part_count = (u32)((type->layout.size + 7) / 8);
        return true;
    }
    if (!type || (type->kind != IR_TYPE_STRUCT && type->kind != IR_TYPE_UNION && type->kind != IR_TYPE_ARRAY) || !type->layout.resolved || !type->layout.size ||
        type->layout.size > (u64)UINT32_MAX * 8)
    {
        return false;
    }
    if (type->layout.size > 16)
    {
        *part_count = (u32)((type->layout.size + 7) / 8);
        return true;
    }
    TemporalArena temporary = scratch_begin(0, 0);
    IrTypeId* worklist = arena_allocate(temporary.arena, IrTypeId, program->types.count);
    bool* visited = arena_allocate(temporary.arena, bool, program->types.count);
    memset(visited, 0, sizeof(*visited) * program->types.count);
    u32 work_count = 1;
    worklist[0] = type_id;
    visited[type_id.value] = true;
    bool integer_only = true;
    while (work_count && integer_only)
    {
        IrTypeId current_id = worklist[--work_count];
        if (current_id.value >= program->types.count)
        {
            integer_only = false;
            break;
        }
        IrType* current = ir_type_from_id(&program->types, current_id);
        if (!current)
        {
            integer_only = false;
        }
        else if (current->kind == IR_TYPE_BOOLEAN || current->kind == IR_TYPE_INTEGER || current->kind == IR_TYPE_FLOAT || current->kind == IR_TYPE_POINTER ||
                 current->kind == IR_TYPE_VECTOR || current->kind == IR_TYPE_ENUM)
        {
            continue;
        }
        else if (current->kind == IR_TYPE_ARRAY)
        {
            if (current->element_type.value >= program->types.count)
            {
                integer_only = false;
                break;
            }
            visited[current->element_type.value] = true;
            worklist[work_count++] = current->element_type;
        }
        else if (current->kind == IR_TYPE_STRUCT || current->kind == IR_TYPE_UNION)
        {
            for (u32 field_index = 0; field_index < current->field_count; field_index += 1)
            {
                IrTypeId field_type = current->fields[field_index].type;
                if (field_type.value >= program->types.count)
                {
                    integer_only = false;
                    break;
                }
                if (!visited[field_type.value])
                {
                    visited[field_type.value] = true;
                    worklist[work_count++] = field_type;
                }
            }
        }
        else
        {
            integer_only = false;
        }
    }
    scratch_end(temporary);
    if (!integer_only)
    {
        return false;
    }
    *part_count = (u32)((type->layout.size + 7) / 8);
    return true;
}

// What one argument of this type wants from the outgoing area it lands in. The
// area is addressed in eightbytes, so that is the floor; a type that wants more
// -- a 256- or 512-bit vector, an `_Alignas(64)` aggregate -- is read back by a
// callee with an alignment-requiring move and has to get what it asked for.
u32 codegen_canonical_x64_stack_argument_alignment(IrType* type)
{
    u64 alignment = type && type->layout.resolved ? type->layout.alignment : 0;
    if (alignment < 8 || alignment > INT32_MAX || (alignment & (alignment - 1)))
    {
        return 8;
    }
    return (u32)alignment;
}

// Where the next argument starts. The System V convention places a stack
// argument at an address respecting its alignment rather than immediately after
// the one before it, so the gap this opens is padding the caller writes nothing
// into and the callee reads nothing out of.
BUSTER_GLOBAL_LOCAL u64 codegen_canonical_x64_stack_argument_offset(u64 cursor, u32 alignment)
{
    u64 remainder = cursor & (alignment - 1);
    return remainder ? cursor + alignment - remainder : cursor;
}

CodegenError codegen_canonical_x64_call_layout(Arena* arena, IrProgram* program, IrFunction* function, IrInstruction* instruction,
                                                                  CodegenAbi abi, Target target, CodegenCanonicalCallLayout* layout)
{
    if (!layout)
    {
        return CODEGEN_ERROR_INVALID_IR;
    }
    *layout = (CodegenCanonicalCallLayout){0};
    if (!arena || !program || !function || !function->values || !instruction || instruction->opcode != IR_OPCODE_CALL || !instruction->operand_count ||
        !instruction->operands || (abi != CODEGEN_ABI_X86_64_SYSTEM_V && abi != CODEGEN_ABI_X86_64_WINDOWS) ||
        instruction->operands[0].value >= function->value_count)
    {
        return CODEGEN_ERROR_INVALID_IR;
    }
    IrType* callee_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
    if (!callee_type)
    {
        return CODEGEN_ERROR_INVALID_IR;
    }
    if (callee_type->kind == IR_TYPE_POINTER)
    {
        callee_type = ir_type_from_id(&program->types, callee_type->element_type);
        if (!callee_type)
        {
            return CODEGEN_ERROR_INVALID_IR;
        }
    }
    if (callee_type->kind != IR_TYPE_FUNCTION)
    {
        return CODEGEN_ERROR_UNSUPPORTED_ABI;
    }
    u32 argument_count = instruction->operand_count - 1;
    if ((!callee_type->is_variadic && argument_count != callee_type->parameter_count) ||
        (callee_type->is_variadic && argument_count < callee_type->parameter_count) ||
        (callee_type->parameter_count && !callee_type->parameter_types))
    {
        return CODEGEN_ERROR_INVALID_IR;
    }
    for (u32 parameter_index = 0; parameter_index < callee_type->parameter_count; parameter_index += 1)
    {
        if (!ir_type_from_id(&program->types, callee_type->parameter_types[parameter_index]))
        {
            return CODEGEN_ERROR_INVALID_IR;
        }
    }
    if (!ir_type_from_id(&program->types, instruction->canonical_type))
    {
        return CODEGEN_ERROR_INVALID_IR;
    }
    layout->argument_count = argument_count;
    layout->return_abi = codegen_canonical_aggregate_abi(program, instruction->canonical_type, abi, true, false);
    layout->indirect_return = layout->return_abi.indirect;
    layout->windows_indirect_return = abi == CODEGEN_ABI_X86_64_WINDOWS && layout->return_abi.indirect;
    layout->simulated_registers = layout->indirect_return ? 1 : 0;
    if (argument_count)
    {
        layout->arguments = arena_allocate(arena, CodegenCanonicalCallArgument, argument_count);
    }
    static u8 const system_v[] = {
        7, 6, 2, 1, 8, 9,
    };
    static u8 const windows[] = {
        1,
        2,
        8,
        9,
    };
    u32 register_count = abi == CODEGEN_ABI_X86_64_WINDOWS ? BUSTER_ARRAY_LENGTH(windows) : BUSTER_ARRAY_LENGTH(system_v);
    u64 stack_part_count = 0;
    for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
    {
        IrValueId argument = instruction->operands[argument_index + 1];
        if (argument.value >= function->value_count)
        {
            return CODEGEN_ERROR_INVALID_IR;
        }
        IrTypeId type_id = function->values[argument.value].canonical_type;
        IrType* type = ir_type_from_id(&program->types, type_id);
        if (!type)
        {
            return CODEGEN_ERROR_INVALID_IR;
        }
        u32 part_count = 1;
        bool aggregate = codegen_canonical_integer_aggregate_parts(program, type_id, &part_count);
        CodegenCanonicalAbiValue argument_abi = codegen_canonical_aggregate_abi(program, type_id, abi, false, false);
        // A value the target cannot carry in the registers its classification
        // named goes on the stack, and its stack image is the eightbyte count
        // the aggregate walk already produced rather than the register count.
        bool argument_in_registers = codegen_canonical_x64_abi_value_in_registers(&argument_abi, &target);
        if (argument_abi.part_count && !argument_abi.memory && !argument_abi.indirect)
        {
            aggregate = true;
            if (argument_in_registers)
            {
                part_count = argument_abi.part_count;
            }
        }
        if (!type || ((type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION) && !aggregate))
        {
            return CODEGEN_ERROR_UNSUPPORTED_ABI;
        }
        bool windows_indirect = abi == CODEGEN_ABI_X86_64_WINDOWS && argument_abi.indirect;
        if (aggregate && abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            part_count = 1;
        }
        if (windows_indirect)
        {
            part_count = 1;
        }
        CodegenCanonicalCallArgument call_argument = {
            .abi = argument_abi,
            .type = type,
            .part_count = part_count,
            .stack_part_count = (u32)((type->layout.size + 7) / 8),
            .float_register = UINT8_MAX,
            .aggregate = aggregate,
            .windows_indirect = windows_indirect,
            .system_v_aggregate = abi == CODEGEN_ABI_X86_64_SYSTEM_V && argument_abi.part_count && !argument_abi.memory && argument_in_registers,
        };
        u64 argument_stack_parts = 0;
        if (abi == CODEGEN_ABI_X86_64_SYSTEM_V && type->kind == IR_TYPE_FLOAT)
        {
            if (layout->simulated_float_registers < 8)
            {
                call_argument.float_register = (u8)layout->simulated_float_registers++;
            }
            else
            {
                call_argument.on_stack = true;
                argument_stack_parts = 1;
            }
        }
        else if (call_argument.system_v_aggregate)
        {
            u32 integer_count = 0;
            u32 float_count = 0;
            for (u32 part = 0; part < argument_abi.part_count; part += 1)
            {
                if (codegen_canonical_abi_part_is_float(argument_abi.parts[part].abi_class))
                {
                    float_count += 1;
                }
                else
                {
                    integer_count += 1;
                }
            }
            if (layout->simulated_registers <= register_count && integer_count <= register_count - layout->simulated_registers &&
                layout->simulated_float_registers <= 8 && float_count <= 8 - layout->simulated_float_registers)
            {
                call_argument.float_register = (u8)layout->simulated_float_registers;
                layout->simulated_registers += integer_count;
                layout->simulated_float_registers += float_count;
            }
            else
            {
                call_argument.on_stack = true;
                argument_stack_parts = (type->layout.size + 7) / 8;
            }
        }
        else
        {
            bool system_v_memory = aggregate && abi == CODEGEN_ABI_X86_64_SYSTEM_V && type->layout.size > 16;
            if (!system_v_memory && layout->simulated_registers <= register_count && part_count <= register_count - layout->simulated_registers)
            {
                layout->simulated_registers += part_count;
            }
            else
            {
                call_argument.on_stack = true;
                argument_stack_parts = part_count;
            }
        }
        if (call_argument.on_stack)
        {
            // Windows gives every stack argument one eightbyte and passes
            // anything wider by reference, so only System V has an argument
            // whose alignment the area has to answer for.
            u32 argument_alignment = abi == CODEGEN_ABI_X86_64_SYSTEM_V ? codegen_canonical_x64_stack_argument_alignment(type) : 8;
            u64 offset = codegen_canonical_x64_stack_argument_offset(stack_part_count * 8, argument_alignment);
            if (offset > UINT32_MAX || argument_stack_parts > (UINT32_MAX - offset) / 8)
            {
                return CODEGEN_ERROR_CAPACITY;
            }
            call_argument.stack_offset = (u32)offset;
            stack_part_count = (offset + argument_stack_parts * 8) / 8;
            layout->stack_alignment = BUSTER_MAX(layout->stack_alignment, argument_alignment);
        }
        if (layout->arguments)
        {
            layout->arguments[argument_index] = call_argument;
        }
    }
    layout->stack_part_count = (u32)stack_part_count;
    layout->stack_alignment = BUSTER_MAX(layout->stack_alignment, (u32)CODEGEN_X64_STACK_ALIGNMENT);
    layout->stack_padding = abi == CODEGEN_ABI_X86_64_SYSTEM_V && (layout->stack_part_count & 1) != 0;
    if (abi == CODEGEN_ABI_X86_64_WINDOWS)
    {
        u64 stack_bytes = 32 + stack_part_count * 8;
        u64 copy_cursor = stack_bytes;
        for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
        {
            CodegenCanonicalCallArgument* call_argument = layout->arguments ? layout->arguments + argument_index : 0;
            if (!call_argument || !call_argument->windows_indirect)
            {
                continue;
            }
            u64 copy_size = call_argument->type->layout.size;
            // The same question a System V stack argument asks, with the
            // outgoing area's own floor under it: this slot is measured from
            // the stack pointer, so nothing below sixteen buys anything.
            u64 copy_alignment =
                BUSTER_MAX(codegen_canonical_x64_stack_argument_alignment(call_argument->type), (u32)CODEGEN_X64_STACK_ALIGNMENT);
            if (!call_argument->type->layout.resolved || !copy_size || copy_size > UINT32_MAX)
            {
                return CODEGEN_ERROR_INVALID_IR;
            }
            // The slot starts sixteen-aligned like the stack pointer it is
            // measured from; a wider argument -- a 512-bit vector wants sixty
            // four -- is rounded up to its own alignment at the call, so the
            // reserve carries the bytes that round-up can consume.
            u64 copy_slack = copy_alignment - CODEGEN_X64_STACK_ALIGNMENT;
            u64 remainder = copy_cursor & (CODEGEN_X64_STACK_ALIGNMENT - 1);
            if (remainder)
            {
                copy_cursor += CODEGEN_X64_STACK_ALIGNMENT - remainder;
            }
            if (copy_cursor > UINT32_MAX || copy_size > UINT32_MAX - copy_cursor || copy_slack > UINT32_MAX - copy_cursor - copy_size)
            {
                return CODEGEN_ERROR_CAPACITY;
            }
            if (call_argument)
            {
                call_argument->copy_offset = (u32)copy_cursor;
                call_argument->copy_size = (u32)copy_size;
                call_argument->copy_alignment = (u32)copy_alignment;
            }
            copy_cursor += copy_size + copy_slack;
        }
        if (copy_cursor > UINT32_MAX - 15)
        {
            return CODEGEN_ERROR_CAPACITY;
        }
        layout->windows_stack_size = (u32)((copy_cursor + 15) & ~(u64)15);
        if (layout->windows_stack_size > INT32_MAX)
        {
            return CODEGEN_ERROR_CAPACITY;
        }
        layout->windows_copy_storage_size = (u32)(copy_cursor - stack_bytes);
    }
    return CODEGEN_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_a64_adjust_stack_described(CodegenBuffer* buffer, u32 byte_count, bool subtract,
                                                                      CodegenFunctionDescriptor* descriptor, u32 action_capacity, bool windows)
{
    if (windows && a64_emit_windows_large_stack_adjust(buffer, byte_count, subtract, descriptor, action_capacity))
    {
        return;
    }
    while (byte_count)
    {
        u32 chunk = BUSTER_MIN(byte_count, 4080u);
        codegen_emit_u32(buffer, (subtract ? 0xd10003ff : 0x910003ff) | (chunk << 10));
        if (subtract && descriptor &&
            !codegen_unwind_action_append(descriptor, action_capacity, (u32)buffer->count - descriptor->code_offset,
                                          CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, chunk))
        {
            buffer->error = CODEGEN_ERROR_CAPACITY;
            return;
        }
        if (subtract)
        {
            codegen_emit_u32(buffer, 0xf90003ff);
            if (windows && descriptor &&
                !codegen_unwind_action_append(descriptor, action_capacity, (u32)buffer->count - descriptor->code_offset, CODEGEN_UNWIND_ACTION_NOP, 0, 0))
            {
                buffer->error = CODEGEN_ERROR_CAPACITY;
                return;
            }
        }
        byte_count -= chunk;
    }
}

void codegen_canonical_a64_adjust_stack(CodegenBuffer* buffer, u32 byte_count, bool subtract)
{
    codegen_canonical_a64_adjust_stack_described(buffer, byte_count, subtract, 0, 0, false);
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_adjust_stack_described(CodegenBuffer* buffer, u32 byte_count, bool subtract,
                                                                      CodegenFunctionDescriptor* descriptor, u32 action_capacity, bool windows)
{
    if (!subtract)
    {
        if (!byte_count)
        {
            return;
        }
        codegen_emit_u8(buffer, 0x48);
        codegen_emit_u8(buffer, byte_count <= INT8_MAX ? 0x83 : 0x81);
        codegen_emit_u8(buffer, 0xc4);
        if (byte_count <= INT8_MAX)
        {
            codegen_emit_u8(buffer, (u8)byte_count);
        }
        else
        {
            codegen_emit_u32(buffer, byte_count);
        }
        return;
    }
    if (windows && x64_emit_windows_stack_allocate(buffer, byte_count, descriptor, action_capacity, descriptor ? descriptor->code_offset : 0))
    {
        return;
    }
    while (byte_count)
    {
        u32 chunk = BUSTER_MIN(byte_count, 4096u);
        codegen_emit_u8(buffer, 0x48);
        codegen_emit_u8(buffer, chunk <= INT8_MAX ? 0x83 : 0x81);
        codegen_emit_u8(buffer, 0xec);
        if (chunk <= INT8_MAX)
        {
            codegen_emit_u8(buffer, (u8)chunk);
        }
        else
        {
            codegen_emit_u32(buffer, chunk);
        }
        if (descriptor && !codegen_unwind_action_append(descriptor, action_capacity, (u32)buffer->count - descriptor->code_offset,
                                                        CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, chunk))
        {
            buffer->error = CODEGEN_ERROR_CAPACITY;
            return;
        }
        codegen_emit_u8(buffer, 0xf6);
        codegen_emit_u8(buffer, 0x04);
        codegen_emit_u8(buffer, 0x24);
        codegen_emit_u8(buffer, 0);
        byte_count -= chunk;
    }
}

void codegen_canonical_x64_adjust_stack(CodegenBuffer* buffer, u32 byte_count, bool subtract)
{
    codegen_canonical_x64_adjust_stack_described(buffer, byte_count, subtract, 0, 0, false);
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_emit_return(CodegenBuffer* buffer, u32 frame_size, CodegenAbi abi, bool dynamic_stack)
{
    if (abi == CODEGEN_ABI_X86_64_WINDOWS)
    {
        if (dynamic_stack)
        {
            codegen_emit_u8(buffer, 0x48);
            codegen_emit_u8(buffer, 0x8d);
            if (frame_size <= INT8_MAX)
            {
                codegen_emit_u8(buffer, 0x65);
                codegen_emit_u8(buffer, (u8)frame_size);
            }
            else
            {
                codegen_emit_u8(buffer, 0xa5);
                codegen_emit_u32(buffer, frame_size);
            }
        }
        else if (frame_size)
        {
            codegen_emit_u8(buffer, 0x48);
            codegen_emit_u8(buffer, frame_size <= INT8_MAX ? 0x83 : 0x81);
            codegen_emit_u8(buffer, 0xc4);
            if (frame_size <= INT8_MAX)
            {
                codegen_emit_u8(buffer, (u8)frame_size);
            }
            else
            {
                codegen_emit_u32(buffer, frame_size);
            }
        }
        codegen_emit_u8(buffer, 0x5d);
        codegen_emit_u8(buffer, 0xc3);
        return;
    }
    codegen_emit_u8(buffer, 0xc9);
    codegen_emit_u8(buffer, 0xc3);
}

void codegen_canonical_a64_base_address(CodegenBuffer* buffer, u32 register_number, u32 base_register, u32 byte_offset)
{
    if (byte_offset <= 4095)
    {
        codegen_emit_u32(buffer, 0x91000000 | (byte_offset << 10) | (base_register << 5) | register_number);
        return;
    }
    u32 offset_register = register_number == base_register ? (register_number == 16 ? 17 : 16) : register_number;
    a64_emit_constant(buffer, offset_register, byte_offset);
    if (base_register == 31)
    {
        u32 stack_register = register_number == 16 || offset_register == 16 ? 17 : 16;
        codegen_emit_u32(buffer, 0x910003e0 | stack_register);
        base_register = stack_register;
    }
    codegen_emit_u32(buffer, 0x8b000000 | (offset_register << 16) | (base_register << 5) | register_number);
}

// EVEX encoding for the target-fixed 512-bit vocabulary. Everything here is
// L'L=10 (512-bit), never broadcasts, and never reaches the extended register
// halves, so the three prefix payload bytes reduce to a handful of fields.
typedef struct X64Evex X64Evex;
struct X64Evex
{
    u8 map;     // 1 = 0F, 2 = 0F38, 3 = 0F3A
    u8 prefix;  // 0 = none, 1 = 66, 2 = F3, 3 = F2
    u8 opcode;
    u8 reg;     // reg field: a zmm, a k register, or an opcode extension
    u8 vvvv;    // the encoded non-destructive source, 0 when the form has none
    u8 mask;    // k1..k7, or 0 for an unmasked operation
    bool zeroing;
    bool wide;  // EVEX.W — every operation in this vocabulary is W0 today
};

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_evex_prefix(CodegenBuffer* buffer, X64Evex evex)
{
    codegen_emit_u8(buffer, 0x62);
    codegen_emit_u8(buffer, (u8)(0xf0 | evex.map));
    codegen_emit_u8(buffer, (u8)((evex.wide ? 0x80 : 0) | ((~evex.vvvv & 0xf) << 3) | 0x04 | evex.prefix));
    codegen_emit_u8(buffer, (u8)((evex.zeroing ? 0x80 : 0) | 0x48 | evex.mask));
    codegen_emit_u8(buffer, evex.opcode);
}

// A frame slot, always as mod=10/disp32: EVEX would otherwise read a disp8 as
// a multiple of the operand size, and mod=00 with an RBP base means
// RIP-relative rather than the frame.
BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_evex_frame(CodegenBuffer* buffer, X64Evex evex, s32 displacement)
{
    codegen_canonical_x64_evex_prefix(buffer, evex);
    codegen_emit_u8(buffer, (u8)(0x85 | ((evex.reg & 7) << 3)));
    codegen_emit_u32(buffer, (u32)displacement);
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_evex_indirect(CodegenBuffer* buffer, X64Evex evex, X64Register base)
{
    codegen_canonical_x64_evex_prefix(buffer, evex);
    codegen_emit_u8(buffer, (u8)((((u32)evex.reg & 7) << 3) | ((u32)base & 7)));
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_evex_register(CodegenBuffer* buffer, X64Evex evex, u8 rm)
{
    codegen_canonical_x64_evex_prefix(buffer, evex);
    codegen_emit_u8(buffer, (u8)(0xc0 | ((evex.reg & 7) << 3) | (rm & 7)));
}

// KMOVQ moves a whole 64-lane mask between a k register and a frame slot in
// one instruction, so a mask never needs a general-purpose register on the way
// through memory. VEX.L0.W1 0F 90 loads, 91 stores.
// mov reg, [rbp+disp32] — the address operand of a SIMD load or store lives in
// a frame slot like every other canonical value.
BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_load_frame_pointer(CodegenBuffer* buffer, X64Register target, s32 displacement)
{
    codegen_emit_u8(buffer, (u8)(target >= X64_REGISTER_R8 ? 0x4c : 0x48));
    codegen_emit_u8(buffer, 0x8b);
    codegen_emit_u8(buffer, (u8)(0x85 | ((target & 7) << 3)));
    codegen_emit_u32(buffer, (u32)displacement);
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_kmov_frame(CodegenBuffer* buffer, u8 mask, bool store, s32 displacement)
{
    codegen_emit_u8(buffer, 0xc4);
    codegen_emit_u8(buffer, 0xe1);
    codegen_emit_u8(buffer, 0xf8);
    codegen_emit_u8(buffer, store ? 0x91 : 0x90);
    codegen_emit_u8(buffer, (u8)(0x85 | ((mask & 7) << 3)));
    codegen_emit_u32(buffer, (u32)displacement);
}

// Moves one ABI part between an SSE/AVX register and a frame slot. This is the
// only thing that decides which part sizes the canonical ABI can carry in a
// vector register — every caller reports CODEGEN_ERROR_UNSUPPORTED_ABI on a
// false return rather than repeating the size test, so the two cannot drift.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_float_memory(CodegenBuffer* buffer, Target target, u32 vector_register, s32 displacement, u32 size, bool store)
{
    if (vector_register >= 8 || (size != 4 && size != 8 && size != 16 && size != 32 && size != 64))
    {
        return false;
    }
    // A part only travels in a vector register if the target has one that
    // wide. The IR ABI classifies a vector by the psABI rule alone, which is
    // right for a target that can hold it and would otherwise have us encode
    // a zmm move for a machine with no zmm.
    if (size > 16 && size > target_vector_register_size(target))
    {
        return false;
    }
    if (size == 64)
    {
        // vmovdqu8 zmm, m512 — the same move every other 512-bit spill and
        // reload in this file uses.
        X64Evex move = {.map = 1, .prefix = 3, .opcode = store ? 0x7f : 0x6f, .reg = (u8)vector_register};
        codegen_canonical_x64_evex_frame(buffer, move, displacement);
        return true;
    }
    if (size == 32)
    {
        // vmovdqu ymm, m256 — VEX.256.F3.0F.WIG 6F/7F.
        codegen_emit_u8(buffer, 0xc5);
        codegen_emit_u8(buffer, 0xfe);
        codegen_emit_u8(buffer, store ? 0x7f : 0x6f);
        codegen_emit_u8(buffer, (u8)(0x85 | (vector_register << 3)));
        codegen_emit_u32(buffer, (u32)displacement);
        return true;
    }
    codegen_emit_u8(buffer, size == 4 || size == 16 ? 0xf3 : 0xf2);
    codegen_emit_u8(buffer, 0x0f);
    codegen_emit_u8(buffer, size == 16 ? (store ? 0x7f : 0x6f) : (store ? 0x11 : 0x10));
    codegen_emit_u8(buffer, (u8)(0x85 | (vector_register << 3)));
    codegen_emit_u32(buffer, (u32)displacement);
    return true;
}

BUSTER_GLOBAL_LOCAL String8 codegen_global_assembly_trim(String8 value)
{
    while (value.length && (value.pointer[0] == ' ' || value.pointer[0] == '\t' || value.pointer[0] == '\r'))
    {
        value.pointer += 1;
        value.length -= 1;
    }
    while (value.length && (value.pointer[value.length - 1] == ' ' || value.pointer[value.length - 1] == '\t' || value.pointer[value.length - 1] == '\r'))
    {
        value.length -= 1;
    }
    return value;
}

BUSTER_GLOBAL_LOCAL bool codegen_global_assembly_unsigned(String8 value, u64* result)
{
    value = codegen_global_assembly_trim(value);
    if (!value.length)
    {
        return false;
    }
    u32 base = 10;
    u64 index = 0;
    if (value.length > 2 && value.pointer[0] == '0' && (value.pointer[1] == 'x' || value.pointer[1] == 'X'))
    {
        base = 16;
        index = 2;
    }
    u64 number = 0;
    for (; index < value.length; index += 1)
    {
        char8 character = value.pointer[index];
        u32 digit = character >= '0' && character <= '9'   ? (u32)(character - '0')
                    : character >= 'a' && character <= 'f' ? (u32)(character - 'a') + 10
                    : character >= 'A' && character <= 'F' ? (u32)(character - 'A') + 10
                                                           : UINT32_MAX;
        if (digit >= base || number > (UINT64_MAX - digit) / base)
        {
            return false;
        }
        number = number * base + digit;
    }
    *result = number;
    return true;
}

// Prices one `.p2align` directive from the text following the directive name.
// Both the emitter and the module code buffer's reserve go through here, so the
// exponent's spelling and its accepted range cannot drift apart and leave a
// directive demanding more padding than was reserved for it.
BUSTER_GLOBAL_LOCAL bool codegen_global_assembly_alignment(String8 operand, u64* alignment)
{
    u64 exponent = 0;
    if (!codegen_global_assembly_unsigned(operand, &exponent) || exponent > 12)
    {
        return false;
    }
    *alignment = UINT64_C(1) << exponent;
    return true;
}

// Upper bound on the padding a source's `.p2align` directives can demand: each
// one pads to its own boundary, so it can ask for one byte less than its
// alignment. The scan matches the directive spelling anywhere in the source
// instead of re-walking lines the way the emitter does — a match inside a
// comment only over-reserves, and no directive the emitter would honor can
// escape it. Assembly with no alignment directives is charged nothing.
BUSTER_GLOBAL_LOCAL u64 codegen_global_assembly_alignment_padding(String8 source)
{
    String8 directive = S8(".p2align");
    u64 padding = 0;
    u64 index = 0;
    while (index + directive.length <= source.length)
    {
        if (source.pointer[index] != '.' || memcmp(source.pointer + index, directive.pointer, directive.length) != 0)
        {
            index += 1;
            continue;
        }
        u64 operand = index + directive.length;
        u64 line_end = operand;
        while (line_end < source.length && source.pointer[line_end] != '\n')
        {
            line_end += 1;
        }
        u64 alignment = 0;
        // The emitter reads at most one directive per line, so charging the
        // line once and resuming after it cannot miss padding.
        if (codegen_global_assembly_alignment(
                (String8){
                    .pointer = source.pointer + operand,
                    .length = line_end - operand,
                },
                &alignment))
        {
            padding += alignment - 1;
        }
        index = line_end + 1;
    }
    return padding;
}

BUSTER_GLOBAL_LOCAL IrSymbolId codegen_global_assembly_symbol(IrProgram* program, String8 name, Target target)
{
    String8 alternate = name;
    if ((target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS) && alternate.length && alternate.pointer[0] == '_')
    {
        alternate.pointer += 1;
        alternate.length -= 1;
    }
    for (u32 symbol_index = 0; symbol_index < program->symbols.count; symbol_index += 1)
    {
        IrSymbol* symbol = &program->symbols.symbols[symbol_index];
        String8 link_name = symbol->link_name.length ? symbol->link_name : symbol->name;
        if (symbol->kind == IR_SYMBOL_FUNCTION && (string_equal(link_name, name) || string_equal(link_name, alternate)))
        {
            return (IrSymbolId){
                .value = symbol_index,
            };
        }
    }
    return IR_SYMBOL_ID_INVALID;
}

BUSTER_GLOBAL_LOCAL bool codegen_emit_global_assembly(Arena* arena, IrProgram* program, IrModuleAssembly assembly, Target target, CodegenBuffer* buffer,
                                                      CodegenModule* result)
{
    u64 line_start = 0;
    while (line_start < assembly.source.length)
    {
        u64 line_end = line_start;
        while (line_end < assembly.source.length && assembly.source.pointer[line_end] != '\n')
        {
            line_end += 1;
        }
        String8 line = codegen_global_assembly_trim((String8){
            .pointer = assembly.source.pointer + line_start,
            .length = line_end - line_start,
        });
        line_start = line_end < assembly.source.length ? line_end + 1 : assembly.source.length;
        if (!line.length || line.pointer[0] == '#')
        {
            continue;
        }
        for (;;)
        {
            u64 colon = UINT64_MAX;
            for (u64 index = 0; index < line.length; index += 1)
            {
                if (line.pointer[index] == ':')
                {
                    colon = index;
                    break;
                }
            }
            if (colon == UINT64_MAX)
            {
                break;
            }
            String8 name = codegen_global_assembly_trim((String8){
                .pointer = line.pointer,
                .length = colon,
            });
            IrSymbolId symbol = codegen_global_assembly_symbol(program, name, target);
            if (symbol.value == IR_ID_UNDERLYING_INVALID)
            {
                return false;
            }
            program->symbols.symbols[symbol.value].is_definition = true;
            result->entries[result->entry_count++] = (CodegenModuleEntry){
                .entity = ANALYSIS_ENTITY_ID_INVALID,
                .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
                .symbol = symbol,
                .offset = (u32)buffer->count,
            };
            line.pointer += colon + 1;
            line.length -= colon + 1;
            line = codegen_global_assembly_trim(line);
            if (!line.length)
            {
                break;
            }
        }
        if (!line.length)
        {
            continue;
        }
        if (line.pointer[0] == '.')
        {
            if ((line.length >= 5 && memcmp(line.pointer, ".byte", 5) == 0))
            {
                String8 values = {
                    .pointer = line.pointer + 5,
                    .length = line.length - 5,
                };
                while (values.length)
                {
                    u64 comma = values.length;
                    for (u64 index = 0; index < values.length; index += 1)
                    {
                        if (values.pointer[index] == ',')
                        {
                            comma = index;
                            break;
                        }
                    }
                    u64 value = 0;
                    if (!codegen_global_assembly_unsigned(
                            (String8){
                                .pointer = values.pointer,
                                .length = comma,
                            },
                            &value) ||
                        value > UINT8_MAX)
                    {
                        return false;
                    }
                    codegen_emit_u8(buffer, (u8)value);
                    if (comma == values.length)
                    {
                        break;
                    }
                    values.pointer += comma + 1;
                    values.length -= comma + 1;
                }
                continue;
            }
            if (line.length >= 8 && memcmp(line.pointer, ".p2align", 8) == 0)
            {
                u64 alignment = 0;
                if (!codegen_global_assembly_alignment(
                        (String8){
                            .pointer = line.pointer + 8,
                            .length = line.length - 8,
                        },
                        &alignment))
                {
                    return false;
                }
                // The emit helpers refuse a byte the buffer cannot hold without
                // advancing its count, so a reserve too small for this padding
                // has to end the loop here instead of asking forever.
                while ((buffer->count & (alignment - 1)) && buffer->error == CODEGEN_ERROR_NONE)
                {
                    if (target.cpu_arch == CPU_ARCH_X86_64)
                    {
                        codegen_emit_u8(buffer, 0x90);
                    }
                    else
                    {
                        if (buffer->count & 3)
                        {
                            return false;
                        }
                        codegen_emit_u32(buffer, 0xd503201f);
                    }
                }
                continue;
            }
            bool recognized = (line.length >= 5 && memcmp(line.pointer, ".text", 5) == 0) || (line.length >= 6 && memcmp(line.pointer, ".globl", 6) == 0) ||
                              (line.length >= 7 && memcmp(line.pointer, ".global", 7) == 0) || (line.length >= 5 && memcmp(line.pointer, ".type", 5) == 0) ||
                              (line.length >= 5 && memcmp(line.pointer, ".size", 5) == 0);
            if (!recognized)
            {
                return false;
            }
            continue;
        }
        char8* normalized = arena_allocate(arena, char8, line.length);
        u64 normalized_length = 0;
        for (u64 index = 0; index < line.length; index += 1)
        {
            if (line.pointer[index] != ' ' && line.pointer[index] != '\t')
            {
                normalized[normalized_length++] = line.pointer[index];
            }
        }
        String8 instruction = {
            .pointer = normalized,
            .length = normalized_length,
        };
        if (target.cpu_arch == CPU_ARCH_X86_64)
        {
            if (string_equal(instruction, S8("ret")) || string_equal(instruction, S8("retq")))
            {
                codegen_emit_u8(buffer, 0xc3);
            }
            else if (string_equal(instruction, S8("nop")))
            {
                codegen_emit_u8(buffer, 0x90);
            }
            else if (string_equal(instruction, S8("ud2")))
            {
                codegen_emit_u8(buffer, 0x0f);
                codegen_emit_u8(buffer, 0x0b);
            }
            else if (string_equal(instruction, S8("pause")))
            {
                codegen_emit_u8(buffer, 0xf3);
                codegen_emit_u8(buffer, 0x90);
            }
            else
            {
                String8 prefixes[] = {
                    S8("mov$"),
                    S8("movl$"),
                };
                bool emitted = false;
                for (u32 prefix_index = 0; prefix_index < BUSTER_ARRAY_LENGTH(prefixes); prefix_index += 1)
                {
                    String8 prefix = prefixes[prefix_index];
                    String8 suffix = S8(",%eax");
                    if (instruction.length <= prefix.length + suffix.length || memcmp(instruction.pointer, prefix.pointer, prefix.length) != 0 ||
                        memcmp(instruction.pointer + instruction.length - suffix.length, suffix.pointer, suffix.length) != 0)
                    {
                        continue;
                    }
                    u64 immediate = 0;
                    if (!codegen_global_assembly_unsigned(
                            (String8){
                                .pointer = instruction.pointer + prefix.length,
                                .length = instruction.length - prefix.length - suffix.length,
                            },
                            &immediate) ||
                        immediate > UINT32_MAX)
                    {
                        return false;
                    }
                    codegen_emit_u8(buffer, 0xb8);
                    codegen_emit_u32(buffer, (u32)immediate);
                    emitted = true;
                    break;
                }
                if (!emitted)
                {
                    return false;
                }
            }
        }
        else
        {
            if (string_equal(instruction, S8("ret")))
            {
                codegen_emit_u32(buffer, 0xd65f03c0);
            }
            else if (string_equal(instruction, S8("nop")))
            {
                codegen_emit_u32(buffer, 0xd503201f);
            }
            else if (string_equal(instruction, S8("brk#0")))
            {
                codegen_emit_u32(buffer, 0xd4200000);
            }
            else
            {
                String8 prefix = S8("movw0,#");
                if (instruction.length <= prefix.length || memcmp(instruction.pointer, prefix.pointer, prefix.length) != 0)
                {
                    return false;
                }
                u64 immediate = 0;
                if (!codegen_global_assembly_unsigned(
                        (String8){
                            .pointer = instruction.pointer + prefix.length,
                            .length = instruction.length - prefix.length,
                        },
                        &immediate) ||
                    immediate > UINT16_MAX)
                {
                    return false;
                }
                codegen_emit_u32(buffer, 0x52800000 | ((u32)immediate << 5));
            }
        }
        if (buffer->error != CODEGEN_ERROR_NONE)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_address(CodegenBuffer* buffer, X64Register target, s32 displacement)
{
    u8 rex = target >= X64_REGISTER_R8 ? 0x4c : 0x48;
    codegen_emit_u8(buffer, rex);
    codegen_emit_u8(buffer, 0x8d);
    codegen_emit_u8(buffer, (u8)(0x85 | ((target & 7) << 3)));
    codegen_emit_u32(buffer, (u32)displacement);
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_sign_extend(CodegenBuffer* buffer, X64Register value, u32 width)
{
    if (width >= 64)
    {
        return;
    }
    u8 register_bits = (u8)(value & 7);
    u8 rex = (u8)(0x48 | (value >= X64_REGISTER_R8 ? 1 : 0));
    codegen_emit_u8(buffer, rex);
    codegen_emit_u8(buffer, 0xc1);
    codegen_emit_u8(buffer, (u8)(0xe0 | register_bits));
    codegen_emit_u8(buffer, (u8)(64 - width));
    codegen_emit_u8(buffer, rex);
    codegen_emit_u8(buffer, 0xc1);
    codegen_emit_u8(buffer, (u8)(0xf8 | register_bits));
    codegen_emit_u8(buffer, (u8)(64 - width));
}

enum
{
    // The canonical path allocates no registers: every operand is reloaded
    // from its frame slot and every result is stored back. These are the fixed
    // scratch names the vocabulary lowers through.
    X64_SIMD_VECTOR_FIRST = 0,
    X64_SIMD_VECTOR_SECOND = 1,
    X64_SIMD_VECTOR_THIRD = 2,
    X64_SIMD_MASK = 1,
};

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_simd_supported(Target target, IrSimdOperation operation)
{
    if (target.cpu_arch != CPU_ARCH_X86_64 || !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512F) ||
        !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512BW))
    {
        return false;
    }
    if (operation == IR_SIMD_PERMUTE2_BYTE)
    {
        return target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512VBMI);
    }
    if (operation == IR_SIMD_COMPRESS_BYTE || operation == IR_SIMD_COMPRESS_STORE_BYTE)
    {
        return target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512VBMI2);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL s32 codegen_canonical_x64_rebase_frame_displacement(CodegenBuffer* buffer, s64 displacement, u32 frame_base_offset);

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_simd_operation(CodegenBuffer* buffer, IrInstruction* instruction, u32 const* value_offsets,
                                                              u32 frame_base_offset, Target target)
{
    IrSimdOperation operation = (IrSimdOperation)instruction->simd_operation;
    IrSimdShape shape = ir_simd_operation_shape(operation);
    if (!codegen_canonical_x64_simd_supported(target, operation) || instruction->operand_count != shape.operand_count ||
        instruction->immediate_count != shape.immediate_count)
    {
        return false;
    }
    s32 slots[4] = {0};
    for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
    {
        slots[operand_index] =
            codegen_canonical_x64_rebase_frame_displacement(buffer, -(s64)value_offsets[instruction->operands[operand_index].value], frame_base_offset);
    }
    s32 result_slot = shape.has_result
                          ? codegen_canonical_x64_rebase_frame_displacement(buffer, -(s64)value_offsets[instruction->result.value], frame_base_offset)
                          : 0;
    u8 immediate = instruction->immediate_count ? (u8)instruction->immediates[0] : 0;
    // vmovdqu8, the one move this vocabulary uses in either direction.
    X64Evex const move_load = {.map = 1, .prefix = 3, .opcode = 0x6f};
    X64Evex const move_store = {.map = 1, .prefix = 3, .opcode = 0x7f};
    switch (operation)
    {
    case IR_SIMD_LOAD:
    case IR_SIMD_LOAD_MASKED:
    {
        bool masked = operation == IR_SIMD_LOAD_MASKED;
        if (masked)
        {
            codegen_canonical_x64_kmov_frame(buffer, X64_SIMD_MASK, false, slots[1]);
        }
        codegen_canonical_x64_load_frame_pointer(buffer, X64_REGISTER_RAX, slots[0]);
        X64Evex load = move_load;
        load.reg = X64_SIMD_VECTOR_FIRST;
        load.mask = masked ? X64_SIMD_MASK : 0;
        load.zeroing = masked;
        codegen_canonical_x64_evex_indirect(buffer, load, X64_REGISTER_RAX);
        X64Evex spill = move_store;
        spill.reg = X64_SIMD_VECTOR_FIRST;
        codegen_canonical_x64_evex_frame(buffer, spill, result_slot);
        return true;
    }
    case IR_SIMD_STORE:
    case IR_SIMD_STORE_MASKED:
    case IR_SIMD_COMPRESS_STORE_BYTE:
    {
        bool masked = operation != IR_SIMD_STORE;
        u32 vector_operand = masked ? 2 : 1;
        if (masked)
        {
            codegen_canonical_x64_kmov_frame(buffer, X64_SIMD_MASK, false, slots[1]);
        }
        codegen_canonical_x64_load_frame_pointer(buffer, X64_REGISTER_RAX, slots[0]);
        X64Evex load = move_load;
        load.reg = X64_SIMD_VECTOR_FIRST;
        codegen_canonical_x64_evex_frame(buffer, load, slots[vector_operand]);
        // vpcompressb writes its destination through the rm operand, so the
        // compressing store and the plain store share this shape exactly.
        X64Evex store = operation == IR_SIMD_COMPRESS_STORE_BYTE ? (X64Evex){.map = 2, .prefix = 1, .opcode = 0x63} : move_store;
        store.reg = X64_SIMD_VECTOR_FIRST;
        store.mask = masked ? X64_SIMD_MASK : 0;
        codegen_canonical_x64_evex_indirect(buffer, store, X64_REGISTER_RAX);
        return true;
    }
    case IR_SIMD_SPLAT_BYTE:
    {
        // movzx eax, byte [rbp+slot]
        codegen_emit_u8(buffer, 0x0f);
        codegen_emit_u8(buffer, 0xb6);
        codegen_emit_u8(buffer, 0x85);
        codegen_emit_u32(buffer, (u32)slots[0]);
        X64Evex broadcast = {.map = 2, .prefix = 1, .opcode = 0x7a, .reg = X64_SIMD_VECTOR_FIRST};
        codegen_canonical_x64_evex_register(buffer, broadcast, X64_REGISTER_RAX);
        X64Evex spill = move_store;
        spill.reg = X64_SIMD_VECTOR_FIRST;
        codegen_canonical_x64_evex_frame(buffer, spill, result_slot);
        return true;
    }
    case IR_SIMD_COMPARE_EQUAL_BYTE:
    case IR_SIMD_COMPARE_LESS_BYTE:
    case IR_SIMD_TEST_MASK_BYTE:
    {
        X64Evex load = move_load;
        load.reg = X64_SIMD_VECTOR_FIRST;
        codegen_canonical_x64_evex_frame(buffer, load, slots[0]);
        // vpcmpeqb and vptestmb take the second source from memory directly;
        // vpcmpub is the three-operand compare with an explicit predicate, 1
        // being unsigned less-than.
        X64Evex compare = operation == IR_SIMD_COMPARE_EQUAL_BYTE ? (X64Evex){.map = 1, .prefix = 1, .opcode = 0x74}
                          : operation == IR_SIMD_COMPARE_LESS_BYTE ? (X64Evex){.map = 3, .prefix = 1, .opcode = 0x3e}
                                                                   : (X64Evex){.map = 2, .prefix = 1, .opcode = 0x26};
        compare.reg = X64_SIMD_MASK;
        compare.vvvv = X64_SIMD_VECTOR_FIRST;
        codegen_canonical_x64_evex_frame(buffer, compare, slots[1]);
        if (operation == IR_SIMD_COMPARE_LESS_BYTE)
        {
            codegen_emit_u8(buffer, 1);
        }
        codegen_canonical_x64_kmov_frame(buffer, X64_SIMD_MASK, true, result_slot);
        return true;
    }
    case IR_SIMD_SIGN_MASK_BYTE:
    {
        X64Evex load = move_load;
        load.reg = X64_SIMD_VECTOR_FIRST;
        codegen_canonical_x64_evex_frame(buffer, load, slots[0]);
        // vpmovb2m has no memory form; the source has to be in a register.
        X64Evex extract = {.map = 2, .prefix = 2, .opcode = 0x29, .reg = X64_SIMD_MASK};
        codegen_canonical_x64_evex_register(buffer, extract, X64_SIMD_VECTOR_FIRST);
        codegen_canonical_x64_kmov_frame(buffer, X64_SIMD_MASK, true, result_slot);
        return true;
    }
    case IR_SIMD_PERMUTE2_BYTE:
    {
        codegen_canonical_x64_kmov_frame(buffer, X64_SIMD_MASK, false, slots[0]);
        X64Evex load = move_load;
        load.reg = X64_SIMD_VECTOR_FIRST;
        codegen_canonical_x64_evex_frame(buffer, load, slots[1]);
        load.reg = X64_SIMD_VECTOR_SECOND;
        codegen_canonical_x64_evex_frame(buffer, load, slots[2]);
        // vpermt2b reads the low table from its destination and the high table
        // from rm, so the destination is loaded with the low table first and
        // the masked write lands on top of it.
        X64Evex permute = {
            .map = 2,
            .prefix = 1,
            .opcode = 0x7d,
            .reg = X64_SIMD_VECTOR_FIRST,
            .vvvv = X64_SIMD_VECTOR_SECOND,
            .mask = X64_SIMD_MASK,
            .zeroing = true,
        };
        codegen_canonical_x64_evex_frame(buffer, permute, slots[3]);
        X64Evex spill = move_store;
        spill.reg = X64_SIMD_VECTOR_FIRST;
        codegen_canonical_x64_evex_frame(buffer, spill, result_slot);
        return true;
    }
    case IR_SIMD_COMPRESS_BYTE:
    {
        codegen_canonical_x64_kmov_frame(buffer, X64_SIMD_MASK, false, slots[0]);
        X64Evex load = move_load;
        load.reg = X64_SIMD_VECTOR_SECOND;
        codegen_canonical_x64_evex_frame(buffer, load, slots[1]);
        // Register form: rm is the destination and reg is the source, so the
        // source is the one that gets loaded and the result lands in the
        // first register like every other operation's does.
        X64Evex compress = {
            .map = 2,
            .prefix = 1,
            .opcode = 0x63,
            .reg = X64_SIMD_VECTOR_SECOND,
            .mask = X64_SIMD_MASK,
            .zeroing = true,
        };
        codegen_canonical_x64_evex_register(buffer, compress, X64_SIMD_VECTOR_FIRST);
        X64Evex spill = move_store;
        spill.reg = X64_SIMD_VECTOR_FIRST;
        codegen_canonical_x64_evex_frame(buffer, spill, result_slot);
        return true;
    }
    case IR_SIMD_WIDEN_BYTE_TO_WORD:
    {
        // The source already lives in memory, so the quarter selection is an
        // address offset and vpmovzxbd reads its 16 bytes straight from there
        // — no vextracti32x4 in front of it.
        X64Evex widen = {.map = 2, .prefix = 1, .opcode = 0x31, .reg = X64_SIMD_VECTOR_FIRST};
        codegen_canonical_x64_evex_frame(buffer, widen, slots[0] + (s32)immediate * 16);
        X64Evex spill = move_store;
        spill.reg = X64_SIMD_VECTOR_FIRST;
        codegen_canonical_x64_evex_frame(buffer, spill, result_slot);
        return true;
    }
    case IR_SIMD_SHIFT_LEFT_WORD:
    {
        // vpslld with an immediate names its destination in vvvv and takes the
        // source through rm, with /6 in the reg field.
        X64Evex shift = {.map = 1, .prefix = 1, .opcode = 0x72, .reg = 6, .vvvv = X64_SIMD_VECTOR_FIRST};
        codegen_canonical_x64_evex_frame(buffer, shift, slots[0]);
        codegen_emit_u8(buffer, immediate);
        X64Evex spill = move_store;
        spill.reg = X64_SIMD_VECTOR_FIRST;
        codegen_canonical_x64_evex_frame(buffer, spill, result_slot);
        return true;
    }
    case IR_SIMD_TERNARY_WORD:
    {
        X64Evex load = move_load;
        load.reg = X64_SIMD_VECTOR_FIRST;
        codegen_canonical_x64_evex_frame(buffer, load, slots[0]);
        load.reg = X64_SIMD_VECTOR_SECOND;
        codegen_canonical_x64_evex_frame(buffer, load, slots[1]);
        X64Evex ternary = {
            .map = 3,
            .prefix = 1,
            .opcode = 0x25,
            .reg = X64_SIMD_VECTOR_FIRST,
            .vvvv = X64_SIMD_VECTOR_SECOND,
        };
        codegen_canonical_x64_evex_frame(buffer, ternary, slots[2]);
        codegen_emit_u8(buffer, immediate);
        X64Evex spill = move_store;
        spill.reg = X64_SIMD_VECTOR_FIRST;
        codegen_canonical_x64_evex_frame(buffer, spill, result_slot);
        return true;
    }
    case IR_SIMD_COUNT:
        break;
    }
    return false;
}


BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_instruction_uses_wide_vector(IrProgram* program, IrFunction* function, IrInstruction* instruction, Target target)
{
    if (instruction->opcode == IR_OPCODE_SIMD)
    {
        // A run of these is the whole point of the vocabulary; splitting it
        // with a vzeroupper between every pair would cost more than the
        // transition it avoids.
        return codegen_canonical_x64_simd_supported(target, (IrSimdOperation)instruction->simd_operation);
    }
    if (instruction->opcode != IR_OPCODE_BINARY || instruction->operand_count != 2 || instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_EQUAL)
    {
        return false;
    }
    IrType* vector = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
    IrType* element = vector && vector->kind == IR_TYPE_VECTOR ? ir_type_from_id(&program->types, vector->element_type) : 0;
    if (!element || (element->kind != IR_TYPE_INTEGER && element->kind != IR_TYPE_FLOAT) ||
        !x64_target_supports_native_vector(target, vector->layout.size, element->bit_width, element->kind == IR_TYPE_INTEGER))
    {
        return false;
    }
    switch (instruction->binary_operation)
    {
    case IR_BINARY_VECTOR_FLOAT_ADD:
    case IR_BINARY_VECTOR_FLOAT_SUBTRACT:
    case IR_BINARY_VECTOR_FLOAT_MULTIPLY:
    case IR_BINARY_VECTOR_FLOAT_DIVIDE:
    case IR_BINARY_VECTOR_INTEGER_ADD:
    case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
    case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
    case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
    case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
        return true;
    default:
        return false;
    }
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_instruction_preserves_wide_vector(IrProgram* program, IrInstruction* instruction)
{
    if (instruction->opcode == IR_OPCODE_FIELD)
    {
        return true;
    }
    if (instruction->opcode != IR_OPCODE_LOAD || instruction->result.value == IR_ID_UNDERLYING_INVALID)
    {
        return false;
    }
    IrType* result_type = ir_type_from_id(&program->types, instruction->canonical_type);
    return result_type && result_type->kind == IR_TYPE_VECTOR && result_type->layout.resolved && result_type->layout.size && result_type->layout.size <= 64;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_vector_operation(CodegenBuffer* output, IrProgram* program, IrFunction* function, IrInstruction* instruction,
                                                                u32 const* value_offsets, u32 frame_base_offset, Target target, u64* native_operation_count,
                                                                u64* split_operation_count, bool* upper_vector_dirty, IrValueId* last_wide_vector_result,
                                                                u32* last_wide_vector_size, u64* forwarded_wide_vector_load_count)
{
    if (!instruction->operand_count)
    {
        return false;
    }
    IrTypeId operand_type_id = function->values[instruction->operands[0].value].canonical_type;
    IrType* vector = ir_type_from_id(&program->types, operand_type_id);
    IrType* element = vector ? ir_type_from_id(&program->types, vector->element_type) : 0;
    if (!vector || vector->kind != IR_TYPE_VECTOR || !element || (element->kind != IR_TYPE_INTEGER && element->kind != IR_TYPE_FLOAT) ||
        (element->bit_width != 8 && element->bit_width != 16 && element->bit_width != 32 && element->bit_width != 64) ||
        instruction->result.value == IR_ID_UNDERLYING_INVALID)
    {
        return false;
    }
    u32 lane_size = element->bit_width / 8;
    if ((u64)lane_size * vector->element_count != vector->layout.size || vector->element_count > UINT32_MAX)
    {
        return false;
    }
    // Frame slots are addressed through the same rebase every other canonical
    // emission uses: a Win64 function with a dynamic stack sets rbp to the
    // bottom of the frame and reaches its values at positive displacements,
    // where every other target keeps rbp at the top and uses negative ones.
    // Spelling the displacement as a bare negation is only right where the
    // rebase is the identity, so it silently addressed outside the frame for
    // exactly the Windows functions this path is reached from.
    s32 left_displacement =
        codegen_canonical_x64_rebase_frame_displacement(output, -(s64)value_offsets[instruction->operands[0].value], frame_base_offset);
    s32 result_displacement =
        codegen_canonical_x64_rebase_frame_displacement(output, -(s64)value_offsets[instruction->result.value], frame_base_offset);
    s32 right_displacement =
        instruction->operand_count == 2
            ? codegen_canonical_x64_rebase_frame_displacement(output, -(s64)value_offsets[instruction->operands[1].value], frame_base_offset)
            : 0;
    X64Builder builder = {
        .buffer = *output,
    };
    codegen_canonical_x64_address(&builder.buffer, X64_REGISTER_R8, left_displacement);
    if (instruction->operand_count == 2)
    {
        codegen_canonical_x64_address(&builder.buffer, X64_REGISTER_R9, right_displacement);
    }
    codegen_canonical_x64_address(&builder.buffer, X64_REGISTER_R10, result_displacement);
    bool comparison = instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_EQUAL &&
                      instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
    u8 condition = 0;
    bool ordered = false;
    bool unordered = false;
    if (comparison && !x64_vector_comparison_condition(instruction->binary_operation, &condition, &ordered, &unordered))
    {
        return false;
    }
    bool wide_native = x64_target_supports_native_vector(target, vector->layout.size, element->bit_width, element->kind == IR_TYPE_INTEGER) &&
                       instruction->opcode == IR_OPCODE_BINARY && instruction->operand_count == 2 && !comparison;
    if (wide_native)
    {
        u8 operation = 0;
        u8 prefix = 0;
        if (element->kind == IR_TYPE_FLOAT)
        {
            prefix = element->bit_width == 64 ? 0x66 : 0;
            operation = instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_ADD        ? 0x58
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_SUBTRACT ? 0x5c
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_MULTIPLY ? 0x59
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_DIVIDE   ? 0x5e
                                                                                           : 0;
        }
        else
        {
            prefix = 0x66;
            IrBinaryOperation binary = instruction->binary_operation;
            if (binary == IR_BINARY_VECTOR_INTEGER_ADD)
            {
                operation = element->bit_width == 8 ? 0xfc : element->bit_width == 16 ? 0xfd : element->bit_width == 32 ? 0xfe : 0xd4;
            }
            else if (binary == IR_BINARY_VECTOR_INTEGER_SUBTRACT)
            {
                operation = element->bit_width == 8 ? 0xf8 : element->bit_width == 16 ? 0xf9 : element->bit_width == 32 ? 0xfa : 0xfb;
            }
            else if (binary == IR_BINARY_VECTOR_INTEGER_BITWISE_AND || binary == IR_BINARY_VECTOR_INTEGER_BITWISE_OR ||
                     binary == IR_BINARY_VECTOR_INTEGER_BITWISE_XOR)
            {
                operation = binary == IR_BINARY_VECTOR_INTEGER_BITWISE_AND ? 0xdb : binary == IR_BINARY_VECTOR_INTEGER_BITWISE_OR ? 0xeb : 0xef;
            }
        }
        if (operation)
        {
            bool forwarded_left =
                *upper_vector_dirty && last_wide_vector_result->value == instruction->operands[0].value && *last_wide_vector_size == vector->layout.size;
            bool forwarded_right = *upper_vector_dirty && x64_vector_binary_is_commutative(instruction->binary_operation) &&
                                   last_wide_vector_result->value == instruction->operands[1].value && *last_wide_vector_size == vector->layout.size;
            if (forwarded_left || forwarded_right)
            {
                *forwarded_wide_vector_load_count += 1;
            }
            else
            {
                x64_emit_vector_native_memory(&builder, false, (u32)vector->layout.size, X64_REGISTER_R8);
            }
            x64_emit_vector_native_binary_operation(&builder, prefix, operation, (u32)vector->layout.size, forwarded_right ? X64_REGISTER_R8 : X64_REGISTER_R9);
            x64_emit_vector_native_memory(&builder, true, (u32)vector->layout.size, X64_REGISTER_R10);
            *output = builder.buffer;
            *native_operation_count += 1;
            *upper_vector_dirty = true;
            *last_wide_vector_result = instruction->result;
            *last_wide_vector_size = (u32)vector->layout.size;
            return true;
        }
    }
    if (vector->layout.size == 16 && instruction->opcode == IR_OPCODE_BINARY && instruction->operand_count == 2 && !comparison)
    {
        u8 operation = 0;
        bool native = false;
        if (element->kind == IR_TYPE_FLOAT)
        {
            operation = instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_ADD        ? 0x58
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_SUBTRACT ? 0x5c
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_MULTIPLY ? 0x59
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_DIVIDE   ? 0x5e
                                                                                           : 0;
            native = operation != 0;
            if (native)
            {
                codegen_emit_u8(&builder.buffer, 0x41);
                codegen_emit_u8(&builder.buffer, 0x0f);
                codegen_emit_u8(&builder.buffer, 0x10);
                codegen_emit_u8(&builder.buffer, 0x00);
                codegen_emit_u8(&builder.buffer, 0x41);
                codegen_emit_u8(&builder.buffer, 0x0f);
                codegen_emit_u8(&builder.buffer, 0x10);
                codegen_emit_u8(&builder.buffer, 0x09);
                if (element->bit_width == 64)
                {
                    codegen_emit_u8(&builder.buffer, 0x66);
                }
                codegen_emit_u8(&builder.buffer, 0x0f);
                codegen_emit_u8(&builder.buffer, operation);
                codegen_emit_u8(&builder.buffer, 0xc1);
                codegen_emit_u8(&builder.buffer, 0x41);
                codegen_emit_u8(&builder.buffer, 0x0f);
                codegen_emit_u8(&builder.buffer, 0x11);
                codegen_emit_u8(&builder.buffer, 0x02);
            }
        }
        else
        {
            IrBinaryOperation binary = instruction->binary_operation;
            if (binary == IR_BINARY_VECTOR_INTEGER_ADD)
            {
                operation = element->bit_width == 8 ? 0xfc : element->bit_width == 16 ? 0xfd : element->bit_width == 32 ? 0xfe : 0xd4;
                native = true;
            }
            else if (binary == IR_BINARY_VECTOR_INTEGER_SUBTRACT)
            {
                operation = element->bit_width == 8 ? 0xf8 : element->bit_width == 16 ? 0xf9 : element->bit_width == 32 ? 0xfa : 0xfb;
                native = true;
            }
            else if (binary == IR_BINARY_VECTOR_INTEGER_BITWISE_AND || binary == IR_BINARY_VECTOR_INTEGER_BITWISE_OR ||
                     binary == IR_BINARY_VECTOR_INTEGER_BITWISE_XOR)
            {
                operation = binary == IR_BINARY_VECTOR_INTEGER_BITWISE_AND ? 0xdb : binary == IR_BINARY_VECTOR_INTEGER_BITWISE_OR ? 0xeb : 0xef;
                native = true;
            }
            if (native)
            {
                codegen_emit_u8(&builder.buffer, 0xf3);
                codegen_emit_u8(&builder.buffer, 0x41);
                codegen_emit_u8(&builder.buffer, 0x0f);
                codegen_emit_u8(&builder.buffer, 0x6f);
                codegen_emit_u8(&builder.buffer, 0x00);
                codegen_emit_u8(&builder.buffer, 0xf3);
                codegen_emit_u8(&builder.buffer, 0x41);
                codegen_emit_u8(&builder.buffer, 0x0f);
                codegen_emit_u8(&builder.buffer, 0x6f);
                codegen_emit_u8(&builder.buffer, 0x09);
                codegen_emit_u8(&builder.buffer, 0x66);
                codegen_emit_u8(&builder.buffer, 0x0f);
                codegen_emit_u8(&builder.buffer, operation);
                codegen_emit_u8(&builder.buffer, 0xc1);
                codegen_emit_u8(&builder.buffer, 0xf3);
                codegen_emit_u8(&builder.buffer, 0x41);
                codegen_emit_u8(&builder.buffer, 0x0f);
                codegen_emit_u8(&builder.buffer, 0x7f);
                codegen_emit_u8(&builder.buffer, 0x02);
            }
        }
        if (native)
        {
            *output = builder.buffer;
            *native_operation_count += 1;
            return true;
        }
    }
    if (vector->layout.size > 16)
    {
        *split_operation_count += 1;
    }
    for (u32 lane = 0; lane < (u32)vector->element_count; lane += 1)
    {
        u32 offset = lane * lane_size;
        if (instruction->opcode == IR_OPCODE_UNARY)
        {
            x64_emit_load_memory(&builder, X64_REGISTER_RAX, X64_REGISTER_R8, offset, lane_size);
            if (instruction->unary_operation == IR_UNARY_VECTOR_FLOAT_NEGATE)
            {
                if (element->kind != IR_TYPE_FLOAT || (element->bit_width != 32 && element->bit_width != 64))
                {
                    return false;
                }
                codegen_emit_u8(&builder.buffer, 0x48);
                codegen_emit_u8(&builder.buffer, 0xb9);
                codegen_emit_u64(&builder.buffer, element->bit_width == 32 ? (u64)1 << 31 : (u64)1 << 63);
                codegen_emit_u8(&builder.buffer, 0x48);
                codegen_emit_u8(&builder.buffer, 0x31);
                codegen_emit_u8(&builder.buffer, 0xc8);
            }
            else if (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE)
            {
                if (element->kind != IR_TYPE_INTEGER)
                {
                    return false;
                }
                codegen_emit_u8(&builder.buffer, 0x48);
                codegen_emit_u8(&builder.buffer, 0xf7);
                codegen_emit_u8(&builder.buffer, 0xd8);
            }
            else if (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_BITWISE_NOT)
            {
                if (element->kind != IR_TYPE_INTEGER)
                {
                    return false;
                }
                codegen_emit_u8(&builder.buffer, 0x48);
                codegen_emit_u8(&builder.buffer, 0xf7);
                codegen_emit_u8(&builder.buffer, 0xd0);
            }
            else
            {
                return false;
            }
            x64_emit_store_memory(&builder, X64_REGISTER_R10, offset, X64_REGISTER_RAX, lane_size);
            continue;
        }
        if (instruction->operand_count != 2)
        {
            return false;
        }
        if (element->kind == IR_TYPE_FLOAT)
        {
            if (element->bit_width != 32 && element->bit_width != 64)
            {
                return false;
            }
            x64_emit_load_float_bits(&builder, 0, X64_REGISTER_R8, offset, lane_size);
            x64_emit_load_float_bits(&builder, 1, X64_REGISTER_R9, offset, lane_size);
            if (!comparison)
            {
                u8 opcode = instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_ADD        ? 0x58
                            : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_SUBTRACT ? 0x5c
                            : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_MULTIPLY ? 0x59
                            : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_DIVIDE   ? 0x5e
                                                                                               : 0;
                if (!opcode)
                {
                    return false;
                }
                codegen_emit_u8(&builder.buffer, element->bit_width == 32 ? 0xf3 : 0xf2);
                codegen_emit_u8(&builder.buffer, 0x0f);
                codegen_emit_u8(&builder.buffer, opcode);
                codegen_emit_u8(&builder.buffer, 0xc1);
                x64_emit_store_float_bits(&builder, X64_REGISTER_R10, offset, 0, lane_size);
                continue;
            }
            if (element->bit_width == 64)
            {
                codegen_emit_u8(&builder.buffer, 0x66);
            }
            codegen_emit_u8(&builder.buffer, 0x0f);
            codegen_emit_u8(&builder.buffer, 0x2e);
            codegen_emit_u8(&builder.buffer, 0xc1);
        }
        else
        {
            x64_emit_load_memory(&builder, X64_REGISTER_RAX, X64_REGISTER_R8, offset, lane_size);
            x64_emit_load_memory(&builder, X64_REGISTER_RCX, X64_REGISTER_R9, offset, lane_size);
            IrBinaryOperation operation = instruction->binary_operation;
            bool signed_semantics = operation == IR_BINARY_VECTOR_SIGNED_DIVIDE || operation == IR_BINARY_VECTOR_SIGNED_REMAINDER ||
                                    (operation >= IR_BINARY_VECTOR_SIGNED_LESS && operation <= IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL);
            if (signed_semantics)
            {
                codegen_canonical_x64_sign_extend(&builder.buffer, X64_REGISTER_RAX, element->bit_width);
                codegen_canonical_x64_sign_extend(&builder.buffer, X64_REGISTER_RCX, element->bit_width);
            }
            if (!comparison)
            {
                switch (operation)
                {
                case IR_BINARY_VECTOR_INTEGER_ADD:
                    codegen_emit_u8(&builder.buffer, 0x48);
                    codegen_emit_u8(&builder.buffer, 0x01);
                    codegen_emit_u8(&builder.buffer, 0xc8);
                    break;
                case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
                    codegen_emit_u8(&builder.buffer, 0x48);
                    codegen_emit_u8(&builder.buffer, 0x29);
                    codegen_emit_u8(&builder.buffer, 0xc8);
                    break;
                case IR_BINARY_VECTOR_INTEGER_MULTIPLY:
                    codegen_emit_u8(&builder.buffer, 0x48);
                    codegen_emit_u8(&builder.buffer, 0x0f);
                    codegen_emit_u8(&builder.buffer, 0xaf);
                    codegen_emit_u8(&builder.buffer, 0xc1);
                    break;
                case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
                case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
                case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
                    codegen_emit_u8(&builder.buffer, 0x48);
                    codegen_emit_u8(&builder.buffer, operation == IR_BINARY_VECTOR_INTEGER_BITWISE_AND  ? 0x21
                                                     : operation == IR_BINARY_VECTOR_INTEGER_BITWISE_OR ? 0x09
                                                                                                        : 0x31);
                    codegen_emit_u8(&builder.buffer, 0xc8);
                    break;
                case IR_BINARY_VECTOR_SHIFT_LEFT:
                case IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT:
                case IR_BINARY_VECTOR_UNSIGNED_SHIFT_RIGHT:
                    codegen_emit_u8(&builder.buffer, 0x48);
                    codegen_emit_u8(&builder.buffer, 0xd3);
                    codegen_emit_u8(&builder.buffer, operation == IR_BINARY_VECTOR_SHIFT_LEFT           ? 0xe0
                                                     : operation == IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT ? 0xf8
                                                                                                        : 0xe8);
                    break;
                case IR_BINARY_VECTOR_SIGNED_DIVIDE:
                case IR_BINARY_VECTOR_SIGNED_REMAINDER:
                    codegen_emit_u8(&builder.buffer, 0x48);
                    codegen_emit_u8(&builder.buffer, 0x99);
                    codegen_emit_u8(&builder.buffer, 0x48);
                    codegen_emit_u8(&builder.buffer, 0xf7);
                    codegen_emit_u8(&builder.buffer, 0xf9);
                    if (operation == IR_BINARY_VECTOR_SIGNED_REMAINDER)
                    {
                        codegen_emit_u8(&builder.buffer, 0x48);
                        codegen_emit_u8(&builder.buffer, 0x89);
                        codegen_emit_u8(&builder.buffer, 0xd0);
                    }
                    break;
                case IR_BINARY_VECTOR_UNSIGNED_DIVIDE:
                case IR_BINARY_VECTOR_UNSIGNED_REMAINDER:
                    codegen_emit_u8(&builder.buffer, 0x31);
                    codegen_emit_u8(&builder.buffer, 0xd2);
                    codegen_emit_u8(&builder.buffer, 0x48);
                    codegen_emit_u8(&builder.buffer, 0xf7);
                    codegen_emit_u8(&builder.buffer, 0xf1);
                    if (operation == IR_BINARY_VECTOR_UNSIGNED_REMAINDER)
                    {
                        codegen_emit_u8(&builder.buffer, 0x48);
                        codegen_emit_u8(&builder.buffer, 0x89);
                        codegen_emit_u8(&builder.buffer, 0xd0);
                    }
                    break;
                default:
                    return false;
                }
                x64_emit_store_memory(&builder, X64_REGISTER_R10, offset, X64_REGISTER_RAX, lane_size);
                continue;
            }
            if (lane_size == 2)
            {
                codegen_emit_u8(&builder.buffer, 0x66);
            }
            codegen_emit_u8(&builder.buffer, lane_size == 8 ? 0x48 : 0x40);
            codegen_emit_u8(&builder.buffer, lane_size == 1 ? 0x38 : 0x39);
            codegen_emit_u8(&builder.buffer, 0xc8);
        }
        codegen_emit_u8(&builder.buffer, 0x0f);
        codegen_emit_u8(&builder.buffer, condition);
        codegen_emit_u8(&builder.buffer, 0xc0);
        if (ordered || unordered)
        {
            codegen_emit_u8(&builder.buffer, 0x0f);
            codegen_emit_u8(&builder.buffer, unordered ? 0x9a : 0x9b);
            codegen_emit_u8(&builder.buffer, 0xc2);
            codegen_emit_u8(&builder.buffer, unordered ? 0x08 : 0x20);
            codegen_emit_u8(&builder.buffer, 0xd0);
        }
        codegen_emit_u8(&builder.buffer, 0x0f);
        codegen_emit_u8(&builder.buffer, 0xb6);
        codegen_emit_u8(&builder.buffer, 0xc0);
        codegen_emit_u8(&builder.buffer, 0x48);
        codegen_emit_u8(&builder.buffer, 0xf7);
        codegen_emit_u8(&builder.buffer, 0xd8);
        x64_emit_store_memory(&builder, X64_REGISTER_R10, offset, X64_REGISTER_RAX, lane_size);
    }
    *output = builder.buffer;
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_memory_operation_base(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store,
                                                                     bool sign_extend, u32 base_register)
{
    u32 scale = size == 8 ? 8 : size == 4 ? 4 : size == 2 ? 2 : size == 1 ? 1 : 0;
    if (!scale || offset % scale || register_number > 31)
    {
        return false;
    }
    bool indirect = offset / scale > 4095;
    if (indirect)
    {
        codegen_canonical_a64_base_address(buffer, 16, base_register, offset);
        offset = 0;
    }
    u32 instruction = 0;
    if (store)
    {
        instruction = size == 8 ? 0xf90003e0 : size == 4 ? 0xb90003e0 : size == 2 ? 0x790003e0 : 0x390003e0;
    }
    else if (sign_extend)
    {
        instruction = size == 4 ? 0xb98003e0 : size == 2 ? 0x798003e0 : size == 1 ? 0x398003e0 : 0xf94003e0;
    }
    else
    {
        instruction = size == 8 ? 0xf94003e0 : size == 4 ? 0xb94003e0 : size == 2 ? 0x794003e0 : 0x394003e0;
    }
    codegen_emit_u32(buffer, (instruction & ~(31u << 5)) | ((offset / scale) << 10) | ((indirect ? 16u : base_register) << 5) | register_number);
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_memory_operation(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store, bool sign_extend)
{
    return codegen_canonical_a64_memory_operation_base(buffer, register_number, offset, size, store, sign_extend, 31);
}

bool codegen_canonical_a64_frame_memory_operation(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store,
                                                                      bool sign_extend)
{
    return codegen_canonical_a64_memory_operation_base(buffer, register_number, offset, size, store, sign_extend, 28);
}

u32 codegen_canonical_a64_remainder_divide_instruction(bool signed_remainder, bool wide)
{
    return (signed_remainder ? 0x1aca0d2b : 0x1aca092b) | (wide ? 0x80000000 : 0);
}

BUSTER_GLOBAL_LOCAL u32 codegen_canonical_copy_chunk(u64 remaining, u64 source_offset, u64 destination_offset)
{
    if (remaining >= 8 && source_offset % 8 == 0 && destination_offset % 8 == 0)
    {
        return 8;
    }
    if (remaining >= 4 && source_offset % 4 == 0 && destination_offset % 4 == 0)
    {
        return 4;
    }
    if (remaining >= 2 && source_offset % 2 == 0 && destination_offset % 2 == 0)
    {
        return 2;
    }
    return 1;
}

// The address of an outgoing-argument slot. The stack pointer is only sixteen
// aligned through the body, so a slot an argument needs more alignment than
// that is reserved with room to spare and rounded up here, the same lea/add/and
// an over-aligned local is given.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_rsp_address(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 alignment)
{
    if (!buffer || register_number > 15 || offset > INT32_MAX ||
        (alignment > CODEGEN_X64_STACK_ALIGNMENT && (alignment > INT32_MAX || (alignment & (alignment - 1)))))
    {
        return false;
    }
    codegen_emit_u8(buffer, register_number >= 8 ? 0x4c : 0x48);
    codegen_emit_u8(buffer, 0x8d);
    codegen_emit_u8(buffer, (u8)(0x84 | ((register_number & 7) << 3)));
    codegen_emit_u8(buffer, 0x24);
    codegen_emit_u32(buffer, offset);
    if (alignment > CODEGEN_X64_STACK_ALIGNMENT)
    {
        codegen_emit_u8(buffer, register_number >= 8 ? 0x49 : 0x48);
        codegen_emit_u8(buffer, 0x81);
        codegen_emit_u8(buffer, (u8)(0xc0 | (register_number & 7)));
        codegen_emit_u32(buffer, alignment - 1);
        codegen_emit_u8(buffer, register_number >= 8 ? 0x49 : 0x48);
        codegen_emit_u8(buffer, 0x81);
        codegen_emit_u8(buffer, (u8)(0xe0 | (register_number & 7)));
        codegen_emit_u32(buffer, 0 - alignment);
    }
    return buffer->error == CODEGEN_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL s32 codegen_canonical_x64_rebase_frame_displacement(CodegenBuffer* buffer, s64 displacement, u32 frame_base_offset)
{
    s64 rebased = (s64)frame_base_offset + displacement;
    if (rebased < INT32_MIN || rebased > INT32_MAX)
    {
        if (buffer)
        {
            buffer->error = CODEGEN_ERROR_CAPACITY;
        }
        return 0;
    }
    return (s32)rebased;
}

// The caller-owned copy of one indirectly passed argument, moved from its frame
// slot into the outgoing area an eightbyte at a time. An argument that wants
// more than the stack pointer's sixteen bytes of alignment is written through
// r11 instead, holding the rounded-up address of its over-reserved slot: r11 is
// volatile, carries no argument, and every copy re-materializes it.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_copy_frame_to_rsp(CodegenBuffer* buffer, u32 source_offset, u32 frame_base_offset,
                                                                 u32 destination_offset, u32 size, u32 alignment)
{
    if (!buffer || !size || source_offset > INT32_MAX || destination_offset > INT32_MAX)
    {
        return false;
    }
    bool through_scratch = alignment > CODEGEN_X64_STACK_ALIGNMENT;
    if (through_scratch)
    {
        if (!codegen_canonical_x64_rsp_address(buffer, X64_REGISTER_R11, destination_offset, alignment))
        {
            return false;
        }
        destination_offset = 0;
    }
    u64 copied = 0;
    while (copied < size)
    {
        u64 source_offset_within_value = (u64)source_offset + copied;
        u64 destination = (u64)destination_offset + copied;
        s32 source = codegen_canonical_x64_rebase_frame_displacement(buffer, -(s64)source_offset + (s64)copied, frame_base_offset);
        u32 chunk = codegen_canonical_copy_chunk((u64)size - copied, source_offset_within_value, destination);
        if (source_offset_within_value > INT32_MAX || destination > INT32_MAX || buffer->error != CODEGEN_ERROR_NONE)
        {
            return false;
        }
        if (chunk == 8)
        {
            codegen_emit_u8(buffer, 0x48);
        }
        if (chunk == 1 || chunk == 2)
        {
            codegen_emit_u8(buffer, 0x0f);
            codegen_emit_u8(buffer, chunk == 1 ? 0xb6 : 0xb7);
        }
        else
        {
            codegen_emit_u8(buffer, 0x8b);
        }
        codegen_emit_u8(buffer, 0x85);
        codegen_emit_u32(buffer, (u32)source);
        if (chunk == 2)
        {
            codegen_emit_u8(buffer, 0x66);
        }
        if (chunk == 8 || through_scratch)
        {
            codegen_emit_u8(buffer, (u8)((chunk == 8 ? 0x48 : 0x40) | (through_scratch ? 0x01 : 0x00)));
        }
        codegen_emit_u8(buffer, chunk == 1 ? 0x88 : 0x89);
        if (through_scratch)
        {
            codegen_emit_u8(buffer, 0x83);
        }
        else
        {
            codegen_emit_u8(buffer, 0x84);
            codegen_emit_u8(buffer, 0x24);
        }
        codegen_emit_u32(buffer, (u32)destination);
        if (buffer->error != CODEGEN_ERROR_NONE)
        {
            return false;
        }
        copied += chunk;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_float_memory_operation_base(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store,
                                                                           u32 base_register)
{
    u32 scale = size == 16 ? 16 : size == 8 ? 8 : size == 4 ? 4 : 0;
    if (!scale || offset % scale || register_number > 31)
    {
        return false;
    }
    bool indirect = offset / scale > 4095;
    if (indirect)
    {
        codegen_canonical_a64_base_address(buffer, 16, base_register, offset);
        offset = 0;
    }
    u32 instruction = store ? (size == 16 ? 0x3d8003e0 : size == 8 ? 0xfd0003e0 : 0xbd0003e0) : (size == 16 ? 0x3dc003e0 : size == 8 ? 0xfd4003e0 : 0xbd4003e0);
    codegen_emit_u32(buffer, (instruction & ~(31u << 5)) | ((offset / scale) << 10) | ((indirect ? 16u : base_register) << 5) | register_number);
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_float_memory_operation(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store)
{
    return codegen_canonical_a64_float_memory_operation_base(buffer, register_number, offset, size, store, 31);
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_frame_float_memory_operation(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store)
{
    return codegen_canonical_a64_float_memory_operation_base(buffer, register_number, offset, size, store, 28);
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_vector_operation(CodegenBuffer* buffer, IrProgram* program, IrFunction* function, IrInstruction* instruction,
                                                                u32 const* value_offsets)
{
    if (!instruction->operand_count)
    {
        return false;
    }
    IrTypeId operand_type_id = function->values[instruction->operands[0].value].canonical_type;
    IrType* vector = ir_type_from_id(&program->types, operand_type_id);
    IrType* element = vector ? ir_type_from_id(&program->types, vector->element_type) : 0;
    if (!vector || vector->kind != IR_TYPE_VECTOR || !element || (element->kind != IR_TYPE_INTEGER && element->kind != IR_TYPE_FLOAT) ||
        (element->bit_width != 8 && element->bit_width != 16 && element->bit_width != 32 && element->bit_width != 64) ||
        instruction->result.value == IR_ID_UNDERLYING_INVALID || vector->element_count > UINT32_MAX)
    {
        return false;
    }
    u32 lane_size = element->bit_width / 8;
    if ((u64)lane_size * vector->element_count != vector->layout.size)
    {
        return false;
    }
    u32 left_base = value_offsets[instruction->operands[0].value];
    u32 right_base = instruction->operand_count == 2 ? value_offsets[instruction->operands[1].value] : 0;
    u32 result_base = value_offsets[instruction->result.value];
    bool comparison = instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_EQUAL &&
                      instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
    for (u32 lane = 0; lane < (u32)vector->element_count; lane += 1)
    {
        u32 lane_offset = lane * lane_size;
        u32 left_offset = left_base + lane_offset;
        u32 result_offset = result_base + lane_offset;
        if (instruction->opcode == IR_OPCODE_UNARY)
        {
            if (element->kind == IR_TYPE_FLOAT)
            {
                if ((element->bit_width != 32 && element->bit_width != 64) || instruction->unary_operation != IR_UNARY_VECTOR_FLOAT_NEGATE ||
                    !codegen_canonical_a64_float_memory_operation(buffer, 0, left_offset, lane_size, false))
                {
                    return false;
                }
                codegen_emit_u32(buffer, element->bit_width == 32 ? 0x1e214000 : 0x1e614000);
                if (!codegen_canonical_a64_float_memory_operation(buffer, 0, result_offset, lane_size, true))
                {
                    return false;
                }
                continue;
            }
            if (!codegen_canonical_a64_memory_operation(buffer, 9, left_offset, lane_size, false, false))
            {
                return false;
            }
            u32 encoded = instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE        ? 0xcb0903e9
                          : instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_BITWISE_NOT ? 0xaa2903e9
                                                                                                : 0;
            if (!encoded)
            {
                return false;
            }
            codegen_emit_u32(buffer, encoded);
            if (!codegen_canonical_a64_memory_operation(buffer, 9, result_offset, lane_size, true, false))
            {
                return false;
            }
            continue;
        }
        if (instruction->operand_count != 2)
        {
            return false;
        }
        u32 right_offset = right_base + lane_offset;
        IrBinaryOperation operation = instruction->binary_operation;
        if (element->kind == IR_TYPE_FLOAT)
        {
            if ((element->bit_width != 32 && element->bit_width != 64) ||
                !codegen_canonical_a64_float_memory_operation(buffer, 0, left_offset, lane_size, false) ||
                !codegen_canonical_a64_float_memory_operation(buffer, 1, right_offset, lane_size, false))
            {
                return false;
            }
            if (!comparison)
            {
                u32 encoded = operation == IR_BINARY_VECTOR_FLOAT_ADD        ? 0x1e212800
                              : operation == IR_BINARY_VECTOR_FLOAT_SUBTRACT ? 0x1e213800
                              : operation == IR_BINARY_VECTOR_FLOAT_MULTIPLY ? 0x1e210800
                              : operation == IR_BINARY_VECTOR_FLOAT_DIVIDE   ? 0x1e211800
                                                                             : 0;
                if (!encoded)
                {
                    return false;
                }
                if (element->bit_width == 64)
                {
                    encoded |= 0x00400000;
                }
                codegen_emit_u32(buffer, encoded);
                if (!codegen_canonical_a64_float_memory_operation(buffer, 0, result_offset, lane_size, true))
                {
                    return false;
                }
                continue;
            }
            codegen_emit_u32(buffer, element->bit_width == 32 ? 0x1e212000 : 0x1e612000);
            u32 condition = operation == IR_BINARY_VECTOR_FLOAT_EQUAL           ? 0
                            : operation == IR_BINARY_VECTOR_FLOAT_NOT_EQUAL     ? 1
                            : operation == IR_BINARY_VECTOR_FLOAT_LESS          ? 4
                            : operation == IR_BINARY_VECTOR_FLOAT_LESS_EQUAL    ? 9
                            : operation == IR_BINARY_VECTOR_FLOAT_GREATER       ? 12
                            : operation == IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL ? 10
                                                                                : UINT32_MAX;
            if (condition == UINT32_MAX)
            {
                return false;
            }
            codegen_emit_u32(buffer, 0x1a9f07e9 | ((condition ^ 1) << 12));
        }
        else
        {
            bool signed_semantics = operation == IR_BINARY_VECTOR_SIGNED_DIVIDE || operation == IR_BINARY_VECTOR_SIGNED_REMAINDER ||
                                    (operation >= IR_BINARY_VECTOR_SIGNED_LESS && operation <= IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL);
            if (!codegen_canonical_a64_memory_operation(buffer, 9, left_offset, lane_size, false, signed_semantics) ||
                !codegen_canonical_a64_memory_operation(buffer, 10, right_offset, lane_size, false, signed_semantics))
            {
                return false;
            }
            if (!comparison)
            {
                u32 encoded = 0;
                switch (operation)
                {
                case IR_BINARY_VECTOR_INTEGER_ADD:
                    encoded = 0x8b0a0129;
                    break;
                case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
                    encoded = 0xcb0a0129;
                    break;
                case IR_BINARY_VECTOR_INTEGER_MULTIPLY:
                    encoded = 0x9b0a7d29;
                    break;
                case IR_BINARY_VECTOR_SIGNED_DIVIDE:
                    encoded = 0x9aca0d29;
                    break;
                case IR_BINARY_VECTOR_UNSIGNED_DIVIDE:
                    encoded = 0x9aca0929;
                    break;
                case IR_BINARY_VECTOR_SIGNED_REMAINDER:
                    codegen_emit_u32(buffer, 0x9aca0d2b);
                    encoded = 0x9b0aa569;
                    break;
                case IR_BINARY_VECTOR_UNSIGNED_REMAINDER:
                    codegen_emit_u32(buffer, 0x9aca096b);
                    encoded = 0x9b0aa569;
                    break;
                case IR_BINARY_VECTOR_SHIFT_LEFT:
                    encoded = 0x9aca2129;
                    break;
                case IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT:
                    encoded = 0x9aca2929;
                    break;
                case IR_BINARY_VECTOR_UNSIGNED_SHIFT_RIGHT:
                    encoded = 0x9aca2529;
                    break;
                case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
                    encoded = 0x8a0a0129;
                    break;
                case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
                    encoded = 0xaa0a0129;
                    break;
                case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
                    encoded = 0xca0a0129;
                    break;
                default:
                    return false;
                }
                codegen_emit_u32(buffer, encoded);
                if (!codegen_canonical_a64_memory_operation(buffer, 9, result_offset, lane_size, true, false))
                {
                    return false;
                }
                continue;
            }
            codegen_emit_u32(buffer, 0xeb0a013f);
            u32 encoded = operation == IR_BINARY_VECTOR_INTEGER_EQUAL            ? 0x1a9f17e9
                          : operation == IR_BINARY_VECTOR_INTEGER_NOT_EQUAL      ? 0x1a9f07e9
                          : operation == IR_BINARY_VECTOR_SIGNED_LESS            ? 0x1a9fa7e9
                          : operation == IR_BINARY_VECTOR_SIGNED_LESS_EQUAL      ? 0x1a9fc7e9
                          : operation == IR_BINARY_VECTOR_SIGNED_GREATER         ? 0x1a9fd7e9
                          : operation == IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL   ? 0x1a9fb7e9
                          : operation == IR_BINARY_VECTOR_UNSIGNED_LESS          ? 0x1a9f27e9
                          : operation == IR_BINARY_VECTOR_UNSIGNED_LESS_EQUAL    ? 0x1a9f87e9
                          : operation == IR_BINARY_VECTOR_UNSIGNED_GREATER       ? 0x1a9f97e9
                          : operation == IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL ? 0x1a9f37e9
                                                                                 : 0;
            if (!encoded)
            {
                return false;
            }
            codegen_emit_u32(buffer, encoded);
        }
        codegen_emit_u32(buffer, 0xcb0903e9);
        if (!codegen_canonical_a64_memory_operation(buffer, 9, result_offset, lane_size, true, false))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u8* codegen_canonical_direct_call_uses(Arena* arena, IrFunction* function)
{
    u8* uses = arena_allocate(arena, u8, function->value_count);
    memset(uses, 0, sizeof(*uses) * function->value_count);
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
        {
            IrValueId operand = instruction->operands[operand_index];
            if (operand.value >= function->value_count || uses[operand.value] == 2)
            {
                continue;
            }
            bool direct = instruction->opcode == IR_OPCODE_CALL && operand_index == 0;
            if (direct)
            {
                IrInstructionId definition = function->values[operand.value].definition;
                IrInstruction* reference = definition.value < function->instruction_count ? function->instructions + definition.value : 0;
                direct = reference && reference->opcode == IR_OPCODE_FUNCTION && reference->symbol.value == instruction->symbol.value;
            }
            uses[operand.value] = direct ? 1 : 2;
        }
    }
    return uses;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_value_is_global_place(IrFunction* function, u32 value_index)
{
    if (!function || value_index >= function->value_count)
    {
        return false;
    }
    IrInstructionId definition = function->values[value_index].definition;
    return definition.value < function->instruction_count && function->instructions[definition.value].opcode == IR_OPCODE_GLOBAL;
}

typedef struct CodegenCanonicalFragmentInfo CodegenCanonicalFragmentInfo;
struct CodegenCanonicalFragmentInfo
{
    u32* block_offsets;
    u32 block_count;
};

// One generation of the module into a code buffer reserved at `capacity_scale`
// times the flat estimate below. Everything it produces comes out of `arena`,
// so a caller that does not like the answer can rewind and ask again; the
// target, the program ABI and the IR validation are its caller's business and
// are not repeated per attempt.
BUSTER_GLOBAL_LOCAL CodegenModule codegen_generate_canonical_module_attempt(Arena* arena, IrProgram* program, IrModule* module, Target target,
                                                                           CodegenModuleOptions options, u64 capacity_scale,
                                                                           bool* code_buffer_exhausted, bool allow_unresolved_label_addresses,
                                                                           CodegenCanonicalFragmentInfo* fragment_info)
{
    CodegenModule result = {
        .ir_module = module,
        .abi = codegen_abi_for_target(target),
    };
    BUSTER_CHECK(!fragment_info || module->function_count <= 1);
    result.globals = arena_allocate(arena, CodegenModuleGlobal, module->global_count);
    u64 read_only_capacity = 0;
    u64 writable_capacity = 0;
    u64 thread_local_capacity = 0;
    u64 zero_fill_capacity = 0;
    u64 thread_local_zero_capacity = 0;
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        IrGlobal* global = module->globals + global_index;
        IrType* type = ir_type_from_id(&program->types, global->type);
        if (!type || !type->layout.resolved || !type->layout.alignment || type->layout.size > UINT32_MAX)
        {
            result.error = CODEGEN_ERROR_INVALID_IR;
            return result;
        }
        u32 global_alignment = global->alignment ? global->alignment : type->layout.alignment;
        if (global_alignment < type->layout.alignment || (global_alignment & (global_alignment - 1)))
        {
            result.error = CODEGEN_ERROR_INVALID_IR;
            return result;
        }
        bool zero_fill = !global->is_read_only && global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO;
        u64* capacity = zero_fill && global->is_thread_local ? &thread_local_zero_capacity
                        : zero_fill                           ? &zero_fill_capacity
                        : global->is_thread_local ? &thread_local_capacity
                        : global->is_read_only    ? &read_only_capacity
                                                  : &writable_capacity;
        u64 remainder = *capacity % global_alignment;
        if (remainder)
        {
            *capacity += global_alignment - remainder;
        }
        *capacity += type->layout.size;
    }
    u8* read_only_bytes = arena_allocate(arena, u8, read_only_capacity);
    u8* writable_bytes = arena_allocate(arena, u8, writable_capacity);
    u8* thread_local_bytes = arena_allocate(arena, u8, thread_local_capacity);
    if (read_only_capacity)
    {
        memset(read_only_bytes, 0, read_only_capacity);
    }
    if (writable_capacity)
    {
        memset(writable_bytes, 0, writable_capacity);
    }
    if (thread_local_capacity)
    {
        memset(thread_local_bytes, 0, thread_local_capacity);
    }
    u64 read_only_count = 0;
    u64 writable_count = 0;
    u64 thread_local_count = 0;
    u64 zero_fill_count = 0;
    u64 thread_local_zero_count = 0;
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        IrGlobal* global = module->globals + global_index;
        IrType* type = ir_type_from_id(&program->types, global->type);
        u32 global_alignment = global->alignment ? global->alignment : type->layout.alignment;
        bool zero_fill = !global->is_read_only && global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO;
        u64* count = zero_fill && global->is_thread_local ? &thread_local_zero_count
                     : zero_fill                           ? &zero_fill_count
                     : global->is_thread_local ? &thread_local_count
                     : global->is_read_only    ? &read_only_count
                                               : &writable_count;
        u8* bytes = zero_fill ? 0 : global->is_thread_local ? thread_local_bytes : global->is_read_only ? read_only_bytes : writable_bytes;
        u64 remainder = *count % global_alignment;
        if (remainder)
        {
            *count += global_alignment - remainder;
        }
        u32 offset = (u32)*count;
        result.globals[result.global_count++] = (CodegenModuleGlobal){
            .symbol = global->symbol,
            .offset = offset,
            .size = (u32)type->layout.size,
            .alignment = global_alignment,
            .read_only = global->is_read_only,
            .is_thread_local = global->is_thread_local,
            .zero_fill = zero_fill,
        };
        if (global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER || global->initializer_kind == IR_GLOBAL_INITIALIZER_FLOAT)
        {
            u64 bits = global->initializer_bits;
            if (global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER && global->initializer_is_negative)
            {
                bits = 0 - bits;
            }
            u64 copy_size = BUSTER_MIN(type->layout.size, sizeof(bits));
            memcpy(bytes + offset, &bits, copy_size);
        }
        else if (global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES)
        {
            memcpy(bytes + offset, global->bytes.pointer, global->bytes.length);
        }
        else if (global->initializer_kind != IR_GLOBAL_INITIALIZER_ZERO && global->initializer_kind != IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS)
        {
            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
            return result;
        }
        *count += type->layout.size;
    }
    result.read_only_data = (ByteSlice){
        .pointer = read_only_bytes,
        .length = read_only_count,
    };
    result.writable_data = (ByteSlice){
        .pointer = writable_bytes,
        .length = writable_count,
    };
    result.zero_fill_size = zero_fill_count;
    result.thread_local_data = (ByteSlice){
        .pointer = thread_local_bytes,
        .length = thread_local_count,
    };
    result.thread_local_zero_size = thread_local_zero_count;
    u64 assembly_capacity = 0;
    // Alignment padding is the one part of global assembly whose size is not
    // bounded by the source that asks for it: a dozen source bytes of
    // `.p2align 12` can demand 4095 bytes of padding. It is reserved separately
    // so the source-length term keeps bounding the label entries below.
    u64 assembly_alignment_capacity = 0;
    for (u32 assembly_index = 0; assembly_index < module->assembly_count; assembly_index += 1)
    {
        assembly_capacity += module->assemblies[assembly_index].source.length;
        assembly_alignment_capacity += codegen_global_assembly_alignment_padding(module->assemblies[assembly_index].source);
    }
    u32 entry_capacity = module->function_count + (u32)BUSTER_MIN(assembly_capacity, UINT32_MAX - module->function_count);
    result.entries = arena_allocate(arena, CodegenModuleEntry, entry_capacity);
    result.functions = arena_allocate(arena, CodegenFunctionDescriptor, entry_capacity);
    u32 instruction_count = 0;
    u64 debug_location_capacity_64 = 0;
    u64 stack_probe_capacity = 0;
    u64 aligned_argument_capacity = 0;
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        instruction_count += function->instruction_count;
        u64 local_capacity = function->debug_local_count ? function->debug_local_count : function->local_count;
        debug_location_capacity_64 += local_capacity * ((u64)function->block_count + 1);
        if (debug_location_capacity_64 > UINT32_MAX)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        u64 function_value_bytes = 0;
        for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
        {
            IrType* value_type = ir_type_from_id(&program->types, function->values[value_index].canonical_type);
            if (!value_type || !value_type->layout.resolved || value_type->layout.size > UINT32_MAX - 7)
            {
                result.error = CODEGEN_ERROR_INVALID_IR;
                return result;
            }
            bool global_place = codegen_canonical_value_is_global_place(function, value_index);
            u64 slot_size = global_place ? 8 : (value_type->layout.size + 7) & ~(u64)7;
            slot_size = BUSTER_MAX(slot_size, 8u);
            u64 slot_alignment = global_place ? 8 : BUSTER_MAX(BUSTER_MAX(value_type->layout.alignment, function->values[value_index].alignment), 8u);
            if (target.cpu_arch == CPU_ARCH_X86_64)
            {
                function_value_bytes += slot_size;
            }
            u64 remainder = function_value_bytes % slot_alignment;
            if (remainder)
            {
                function_value_bytes += slot_alignment - remainder;
            }
            if (target.cpu_arch == CPU_ARCH_AARCH64)
            {
                function_value_bytes += slot_size;
            }
            if (function->values[value_index].alignment > 16 && function->values[value_index].definition.value < function->instruction_count &&
                function->instructions[function->values[value_index].definition.value].opcode == IR_OPCODE_LOCAL)
            {
                function_value_bytes += value_type->layout.size + function->values[value_index].alignment - 1;
            }
            // A value this wide can be handed to a call on the stack, and an
            // area aligned for it is filled an eightbyte at a time rather than
            // pushed. That is more code than the flat per-instruction reserve
            // below carries, so the value pays for the copy it can provoke.
            if (target.cpu_arch == CPU_ARCH_X86_64 && slot_alignment > CODEGEN_X64_STACK_ALIGNMENT)
            {
                aligned_argument_capacity += (slot_size / 8) * 15 + 32;
            }
        }
        u64 probe_count = (function_value_bytes + 4079) / 4080;
        stack_probe_capacity += probe_count * 11;
    }
    u32 debug_location_capacity = (u32)debug_location_capacity_64;
    u32 global_relocation_count = 0;
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        IrGlobal* global = module->globals + global_index;
        global_relocation_count += global->relocation_count;
        global_relocation_count += global->initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS;
    }
    result.relocations = arena_allocate(arena, CodegenModuleRelocation, instruction_count * 3 + global_relocation_count);
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        IrGlobal* global = module->globals + global_index;
        CodegenModuleGlobal generated = result.globals[global_index];
        if (global->initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS)
        {
            result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                .entity = ANALYSIS_ENTITY_ID_INVALID,
                .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
                .symbol = global->initializer_symbol,
                .addend = global->initializer_addend,
                .offset = generated.offset,
                .source = generated.is_thread_local ? CODEGEN_MODULE_RELOCATION_THREAD_LOCAL_DATA
                          : generated.read_only     ? CODEGEN_MODULE_RELOCATION_READ_ONLY_DATA
                                                    : CODEGEN_MODULE_RELOCATION_DATA,
                .absolute = true,
            };
        }
        for (u32 relocation_index = 0; relocation_index < global->relocation_count; relocation_index += 1)
        {
            IrGlobalRelocation relocation = global->relocations[relocation_index];
            if (relocation.offset > UINT32_MAX - generated.offset)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                .entity = ANALYSIS_ENTITY_ID_INVALID,
                .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
                .symbol = relocation.symbol,
                .label_block = relocation.label_block,
                .addend = relocation.addend,
                .offset = generated.offset + (u32)relocation.offset,
                .source = generated.is_thread_local ? CODEGEN_MODULE_RELOCATION_THREAD_LOCAL_DATA
                          : generated.read_only     ? CODEGEN_MODULE_RELOCATION_READ_ONLY_DATA
                                                    : CODEGEN_MODULE_RELOCATION_DATA,
                .absolute = true,
                .label_address = relocation.is_label_address,
            };
        }
    }
    // Label-address relocations only come from the global initializers just
    // emitted, and each is resolved exactly once by its owning function, so
    // the per-function resolution below walks this side list instead of
    // rescanning every module relocation.
    u32* label_address_relocation_indices = arena_allocate(arena, u32, result.relocation_count);
    u32 label_address_relocation_count = 0;
    for (u32 relocation_index = 0; relocation_index < result.relocation_count; relocation_index += 1)
    {
        if (result.relocations[relocation_index].label_address)
        {
            label_address_relocation_indices[label_address_relocation_count++] = relocation_index;
        }
    }
    u64 instruction_capacity = target.cpu_arch == CPU_ARCH_AARCH64 ? 128 : 48;
    u64 capacity = ((u64)instruction_count * instruction_capacity + (u64)module->function_count * 64 + stack_probe_capacity + aligned_argument_capacity +
                    assembly_capacity * 4 + assembly_alignment_capacity + 64) *
                   capacity_scale;
    // Every offset the module hands out is a u32, so a buffer past that is
    // unusable however much of it the arena would give. This is also what ends
    // the caller's retry: a scale that cannot fit stops here instead of
    // reporting the code buffer exhausted and being doubled again.
    if (capacity > UINT32_MAX)
    {
        result.error = CODEGEN_ERROR_CAPACITY;
        return result;
    }
    CodegenBuffer buffer = {
        .bytes = arena_allocate(arena, u8, capacity),
        .capacity = capacity,
        .exhausted = code_buffer_exhausted,
    };
    // Every function contributes a row for its own declaration on top of the
    // per-instruction rows.
    u32 line_entry_capacity = instruction_count + module->function_count;
    result.line_entries = options.debug_info ? arena_allocate(arena, CodegenLineEntry, line_entry_capacity) : 0;
    result.debug_locations = options.debug_info ? arena_allocate(arena, DebugLocationSeed, debug_location_capacity) : 0;
    result.debug_info = options.debug_info;
    typedef struct CCanonicalBranchPatch CCanonicalBranchPatch;
    struct CCanonicalBranchPatch
    {
        IrBlockId target;
        u32 offset;
        u32 secondary_offset;
        bool aarch64;
        bool conditional;
        bool label_address;
        u8 reserved[3];
    };
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        result.failed_function = (IrFunctionId){
            .value = function_index,
        };
        if (function->state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        u64 alignment = target.cpu_arch == CPU_ARCH_AARCH64 ? 4 : 16;
        // A full buffer stops accepting bytes without advancing its count, so
        // padding to an alignment it can no longer reach never terminates.
        while (buffer.error == CODEGEN_ERROR_NONE && buffer.count % alignment)
        {
            codegen_emit_u8(&buffer, target.cpu_arch == CPU_ARCH_X86_64 ? 0x90 : 0);
        }
        if (buffer.error != CODEGEN_ERROR_NONE)
        {
            result.error = buffer.error;
            return result;
        }
        result.entries[result.entry_count++] = (CodegenModuleEntry){
            .entity = ANALYSIS_ENTITY_ID_INVALID,
            .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
            .symbol = function->symbol,
            .offset = (u32)buffer.count,
        };
        if (function->source.source.value != IR_ID_UNDERLYING_INVALID)
        {
            // A row at the function start makes the prologue map to the
            // declaration line instead of falling outside the line table.
            IrSourcePosition declaration = ir_source_position(program, function->source);
            codegen_record_line(result.line_entries, &result.line_entry_count, line_entry_capacity, (u32)buffer.count, function->source.source.value,
                                declaration.line, declaration.column);
        }
        // The declaration row is not an instruction's; the next instruction
        // must still be able to record one.
        IrSourceRange recorded_source = {.source = IR_SOURCE_ID_INVALID, .offset = UINT32_MAX};
        IrBlock* entry = function->blocks + function->entry.value;
        if (entry->first_instruction.value >= function->instruction_count)
        {
            result.error = CODEGEN_ERROR_INVALID_IR;
            return result;
        }
        bool windows_aarch64 = target.cpu_arch == CPU_ARCH_AARCH64 && target.os == OPERATING_SYSTEM_WINDOWS;
        bool windows_dynamic_stack = false;
        if (target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = function->instructions[instruction_index].opcode;
                windows_dynamic_stack |= opcode == IR_OPCODE_STACK_ALLOCATE || opcode == IR_OPCODE_STACK_RESTORE;
            }
        }
        bool x64_aligned_argument_call = false;
        bool x64_save_rbx = target.cpu_arch == CPU_ARCH_X86_64 && codegen_canonical_x64_function_saves_rbx(function);
        if (target.cpu_arch == CPU_ARCH_X86_64 && !x64_save_rbx)
        {
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                IrInstructionExtra extra = ir_instruction_extra(function, instruction->id);
                if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY &&
                    ((instruction->operand_count && !instruction->immediates) || (extra.clobber_count && !extra.clobbers)))
                {
                    result.error = CODEGEN_ERROR_INVALID_IR;
                    return result;
                }
            }
        }
        u32* value_offsets = arena_allocate(arena, u32, function->value_count);
        u8* direct_call_uses = codegen_canonical_direct_call_uses(arena, function);
        // The Windows x64 sizing pass below already computes every call's
        // layout to find the outgoing stack area; keep those layouts so the
        // emission pass reuses them instead of recomputing per call. Other
        // ABIs compute layouts once during emission exactly as before.
        CodegenCanonicalCallLayout** call_layout_cache = 0;
        if (target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            call_layout_cache = arena_allocate(arena, CodegenCanonicalCallLayout*, function->instruction_count);
            memset(call_layout_cache, 0, sizeof(*call_layout_cache) * function->instruction_count);
        }
        // Keep the x28 frame-base save in the directly encodable ARM64
        // Windows unwind range. Ordinary values start after its reserved slot.
        u64 value_bytes = windows_aarch64 ? 16 : 0;
        for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
        {
            IrType* value_type = ir_type_from_id(&program->types, function->values[value_index].canonical_type);
            if (!value_type || !value_type->layout.resolved || value_type->layout.size > UINT32_MAX - 7)
            {
                result.error = CODEGEN_ERROR_INVALID_IR;
                return result;
            }
            bool global_place = codegen_canonical_value_is_global_place(function, value_index);
            u32 slot_size = global_place ? 8 : ((u32)value_type->layout.size + 7) & ~(u32)7;
            slot_size = BUSTER_MAX(slot_size, 8u);
            u64 slot_alignment = global_place ? 8 : BUSTER_MAX(BUSTER_MAX(value_type->layout.alignment, function->values[value_index].alignment), 8u);
            if (target.cpu_arch == CPU_ARCH_X86_64)
            {
                value_bytes += slot_size;
            }
            u64 remainder = value_bytes % slot_alignment;
            if (remainder)
            {
                value_bytes += slot_alignment - remainder;
            }
            if (value_bytes > UINT32_MAX)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            value_offsets[value_index] = (u32)value_bytes;
            // A value wanting more than sixteen bytes is a value a call can be
            // asked to pass on the stack, and such a call has to realign the
            // stack pointer and put it back afterwards from somewhere a call
            // cannot clobber. The slot alignment this loop already computed
            // answers that, and answers yes a little too often -- an
            // over-aligned local that is never an argument also reserves the
            // eight bytes -- which costs a frame slot and no work.
            x64_aligned_argument_call |= slot_alignment > CODEGEN_X64_STACK_ALIGNMENT;
            if (target.cpu_arch == CPU_ARCH_AARCH64)
            {
                value_bytes += slot_size;
                if (value_bytes > UINT32_MAX)
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
            }
        }
        u32 x64_stack_save_offset = 0;
        if (x64_aligned_argument_call && target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_SYSTEM_V)
        {
            if (value_bytes > UINT32_MAX - 8)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            value_bytes += 8;
            x64_stack_save_offset = (u32)value_bytes;
        }
        u32 x64_rbx_save_offset = 0;
        if (x64_save_rbx)
        {
            if (value_bytes > UINT32_MAX - 8)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            x64_rbx_save_offset = (u32)value_bytes + 8;
            value_bytes += 8;
        }
        u32* aligned_local_offsets = arena_allocate(arena, u32, function->value_count);
        for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
        {
            IrValue* value = function->values + value_index;
            if (value->alignment <= 16 || value->definition.value >= function->instruction_count ||
                function->instructions[value->definition.value].opcode != IR_OPCODE_LOCAL)
            {
                continue;
            }
            IrType* value_type = ir_type_from_id(&program->types, value->canonical_type);
            if (!value_type || !value_type->layout.resolved || value_type->layout.size > UINT32_MAX - (value->alignment - 1))
            {
                result.error = CODEGEN_ERROR_INVALID_IR;
                return result;
            }
            u64 raw_size = value_type->layout.size + value->alignment - 1;
            if (target.cpu_arch == CPU_ARCH_AARCH64)
            {
                aligned_local_offsets[value_index] = (u32)value_bytes;
            }
            value_bytes += raw_size;
            if (value_bytes > UINT32_MAX)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            if (target.cpu_arch == CPU_ARCH_X86_64)
            {
                aligned_local_offsets[value_index] = (u32)value_bytes;
            }
        }
        IrType* canonical_function_type = ir_type_from_id(&program->types, function->canonical_type);
        IrTypeId canonical_return_type =
            canonical_function_type && canonical_function_type->kind == IR_TYPE_FUNCTION ? canonical_function_type->return_type : IR_TYPE_ID_INVALID;
        bool canonical_variadic = canonical_function_type && canonical_function_type->kind == IR_TYPE_FUNCTION && canonical_function_type->is_variadic;
        CodegenCanonicalAbiValue canonical_return_abi = codegen_canonical_aggregate_abi(program, canonical_return_type, result.abi, true, false);
        bool windows_indirect_return = target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_WINDOWS && canonical_return_abi.indirect;
        bool system_v_indirect_return = target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && canonical_return_abi.indirect;
        bool x64_indirect_return = windows_indirect_return || system_v_indirect_return;
        bool aarch64_indirect_return = target.cpu_arch == CPU_ARCH_AARCH64 && canonical_return_abi.indirect;
        u64 frame_size_64 = (value_bytes + 7) & ~(u64)7;
        u32 aarch64_va_save_offset = 0;
        bool aarch64_darwin = target.cpu_arch == CPU_ARCH_AARCH64 &&
                              (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS);
        bool aarch64_darwin_variadic = canonical_variadic && aarch64_darwin;
        if (target.cpu_arch == CPU_ARCH_AARCH64 && canonical_variadic && !aarch64_darwin_variadic)
        {
            aarch64_va_save_offset = (u32)frame_size_64;
            frame_size_64 += 64;
        }
        s32 canonical_va_save_displacement = 0;
        if (target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && canonical_variadic)
        {
            canonical_va_save_displacement = -(s32)(frame_size_64 + 176);
            frame_size_64 += 176;
        }
        s32 hidden_result_displacement = -(s32)(frame_size_64 + 8);
        u32 aarch64_hidden_result_offset = (u32)frame_size_64;
        if (x64_indirect_return || aarch64_indirect_return)
        {
            frame_size_64 += 8;
        }
        u32 aarch64_frame_base_save_offset = 0;
        if (target.cpu_arch == CPU_ARCH_AARCH64)
        {
            if (!windows_aarch64)
            {
                aarch64_frame_base_save_offset = (u32)frame_size_64;
                frame_size_64 += 8;
            }
        }
        frame_size_64 = (frame_size_64 + 15) & ~(u64)15;
        if (frame_size_64 > UINT32_MAX)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        u32 frame_size = (u32)frame_size_64;
        u32 windows_outgoing_size = 0;
        if (target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                if (instruction->opcode != IR_OPCODE_CALL)
                {
                    continue;
                }
                CodegenCanonicalCallLayout* call_layout = arena_allocate(arena, CodegenCanonicalCallLayout, 1);
                *call_layout = (CodegenCanonicalCallLayout){0};
                CodegenError call_error = codegen_canonical_x64_call_layout(arena, program, function, instruction, result.abi, target, call_layout);
                if (call_error != CODEGEN_ERROR_NONE)
                {
                    // This pass runs before the emitting one that keeps the
                    // failing instruction up to date, so it has to name its own
                    // call or the diagnostic blames whatever ran last.
                    result.failed_instruction = instruction->id;
                    result.failed_opcode = instruction->opcode;
                    result.error = call_error;
                    return result;
                }
                call_layout_cache[instruction_index] = call_layout;
                windows_outgoing_size = BUSTER_MAX(windows_outgoing_size, call_layout->windows_stack_size);
            }
            if (frame_size > UINT32_MAX - windows_outgoing_size)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            frame_size += windows_outgoing_size;
        }
        if (windows_dynamic_stack && frame_size > INT32_MAX)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        u32 canonical_x64_frame_base_offset = windows_dynamic_stack ? frame_size : 0;
        u32 stack_action_count = target.cpu_arch == CPU_ARCH_X86_64
                                     ? result.abi == CODEGEN_ABI_X86_64_WINDOWS ? (frame_size != 0) : frame_size / 4096 + (frame_size % 4096 != 0)
                                     : frame_size / 4080 + (frame_size % 4080 != 0);
        u32 stack_action_capacity = windows_aarch64 ? (frame_size > 4080 ? 14u : stack_action_count * 2) : stack_action_count;
        // x86_64 holds the frame-pointer pair, up to five machine-path
        // callee-saved pushes, and the stack allocation.
        u32 unwind_action_capacity = (target.cpu_arch == CPU_ARCH_X86_64 ? 8u : windows_aarch64 ? 6u : 5u) + stack_action_capacity;
        CodegenFunctionDescriptor* descriptor = result.functions + result.function_count;
        result.function_count += 1;
        *descriptor = (CodegenFunctionDescriptor){
            .unwind_actions = arena_allocate(arena, CodegenUnwindAction, unwind_action_capacity),
            .epilog_offsets = target.cpu_arch == CPU_ARCH_AARCH64 ? arena_allocate(arena, u32, function->instruction_count) : 0,
            .symbol = function->symbol,
            .code_offset = result.entries[result.entry_count - 1].offset,
        };
        result.statistics.function_count += 1;
        result.statistics.instruction_count += function->instruction_count;
        result.statistics.value_count += function->value_count;
        result.statistics.stack_value_bytes += value_bytes;
        result.statistics.stack_frame_bytes += frame_size;
        result.statistics.maximum_stack_frame_bytes = BUSTER_MAX(result.statistics.maximum_stack_frame_bytes, frame_size);
        // MIR_STACK routes eligible functions through machine selection,
        // stack placement, and the machine encoder; everything else falls
        // back to the canonical path below and is counted. The machine
        // prologue byte-for-byte matches the canonical plain x64 prologue,
        // so the descriptor's unwind actions keep their exact meaning.
        bool machine_function_emitted = false;
        if ((options.register_allocator == CODEGEN_REGISTER_ALLOCATOR_MIR_STACK || options.register_allocator == CODEGEN_REGISTER_ALLOCATOR_FAST ||
             options.register_allocator == CODEGEN_REGISTER_ALLOCATOR_QUALITY) &&
            target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_SYSTEM_V)
        {
            bool label_address_target = false;
            for (u32 side_index = 0; side_index < label_address_relocation_count; side_index += 1)
            {
                label_address_target |= result.relocations[label_address_relocation_indices[side_index]].symbol.value == function->symbol.value;
            }
            TemporalArena machine_scratch = scratch_begin(&arena, 1);
            MachineSelectResult selected = {0};
            if (!label_address_target)
            {
                selected = machine_select_canonical_function(machine_scratch.arena, program, function, target);
            }
            if (!selected.supported)
            {
                u32 reason = selected.failed_opcode <= IR_OPCODE_COUNT ? (u32)selected.failed_opcode : (u32)IR_OPCODE_COUNT;
                result.statistics.fallback_opcode_counts[reason] += 1;
            }
            if (selected.supported && machine_verify_function(&selected.function).error != MACHINE_VERIFY_NONE)
            {
                result.statistics.fallback_verify_count += 1;
            }
            if (selected.supported && machine_verify_function(&selected.function).error == MACHINE_VERIFY_NONE)
            {
                MachineStackPlacement placement =
                    options.register_allocator == CODEGEN_REGISTER_ALLOCATOR_FAST    ? machine_fast_placement_build(machine_scratch.arena, &selected.function)
                    : options.register_allocator == CODEGEN_REGISTER_ALLOCATOR_QUALITY
                        ? machine_quality_placement_build(machine_scratch.arena, &selected.function)
                        : machine_stack_placement_build(machine_scratch.arena, &selected.function);
                if (!placement.valid)
                {
                    result.statistics.fallback_placement_count += 1;
                }
                if (placement.valid)
                {
                    MachineEncodeResult encoded = machine_encode_x86_64(machine_scratch.arena, &selected.function, &placement);
                    if (!encoded.valid || buffer.count + encoded.byte_count > buffer.capacity)
                    {
                        result.statistics.fallback_encode_count += 1;
                    }
                    if (encoded.valid && buffer.count + encoded.byte_count <= buffer.capacity)
                    {
                        bool machine_unwind_valid =
                            codegen_unwind_action_append(descriptor, unwind_action_capacity, 1, CODEGEN_UNWIND_ACTION_PUSH_REGISTER, X64_REGISTER_RBP, 0);
                        machine_unwind_valid =
                            codegen_unwind_action_append(descriptor, unwind_action_capacity, 4, CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, X64_REGISTER_RBP, 0) &&
                            machine_unwind_valid;
                        // Callee-saved pushes follow the frame-pointer setup
                        // in the encoder's fixed RBX, R14, R15 order; RBX
                        // pushes in one byte, the extended pair in two.
                        u32 machine_prologue_cursor = 4;
                        if (placement.callee_saved_mask & (1u << X64_REGISTER_RBX))
                        {
                            machine_prologue_cursor += 1;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_PUSH_REGISTER, X64_REGISTER_RBX, 0) &&
                                                   machine_unwind_valid;
                        }
                        if (placement.callee_saved_mask & (1u << X64_REGISTER_R12))
                        {
                            machine_prologue_cursor += 2;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_PUSH_REGISTER, X64_REGISTER_R12, 0) &&
                                                   machine_unwind_valid;
                        }
                        if (placement.callee_saved_mask & (1u << X64_REGISTER_R13))
                        {
                            machine_prologue_cursor += 2;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_PUSH_REGISTER, X64_REGISTER_R13, 0) &&
                                                   machine_unwind_valid;
                        }
                        if (placement.callee_saved_mask & (1u << X64_REGISTER_R14))
                        {
                            machine_prologue_cursor += 2;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_PUSH_REGISTER, X64_REGISTER_R14, 0) &&
                                                   machine_unwind_valid;
                        }
                        if (placement.callee_saved_mask & (1u << X64_REGISTER_R15))
                        {
                            machine_prologue_cursor += 2;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_PUSH_REGISTER, X64_REGISTER_R15, 0) &&
                                                   machine_unwind_valid;
                        }
                        // One allocation action per emitted chunk, at the
                        // exact end offset of its subtract; the probe bytes
                        // follow each action.
                        u32 machine_frame_remaining = placement.frame_size;
                        while (machine_frame_remaining)
                        {
                            u32 machine_frame_chunk = BUSTER_MIN(machine_frame_remaining, 4096u);
                            machine_prologue_cursor += machine_frame_chunk <= INT8_MAX ? 4u : 7u;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, machine_frame_chunk) &&
                                                   machine_unwind_valid;
                            machine_prologue_cursor += 4;
                            machine_frame_remaining -= machine_frame_chunk;
                        }
                        if (machine_unwind_valid)
                        {
                            memcpy(buffer.bytes + buffer.count, encoded.bytes, encoded.byte_count);
                            for (u32 mark_index = 0; mark_index < selected.function.line_mark_count; mark_index += 1)
                            {
                                MachineLineMark* mark = selected.function.line_marks + mark_index;
                                if (mark->row < selected.function.instruction_count)
                                {
                                    codegen_record_line(result.line_entries, &result.line_entry_count, line_entry_capacity,
                                                        (u32)buffer.count + encoded.row_offsets[mark->row], mark->source, mark->line, mark->column);
                                }
                            }
                            for (u32 site_index = 0; site_index < encoded.call_site_count; site_index += 1)
                            {
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .entity = ANALYSIS_ENTITY_ID_INVALID,
                                    .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
                                    .symbol = selected.function.call_targets[encoded.call_sites[site_index].target],
                                    .offset = (u32)buffer.count + encoded.call_sites[site_index].code_offset,
                                };
                            }
                            buffer.count += encoded.byte_count;
                            descriptor->prolog_size = machine_prologue_cursor;
                            descriptor->code_size = (u32)buffer.count - descriptor->code_offset;
                            machine_function_emitted = true;
                            result.statistics.allocator_reload_count += placement.reload_count;
                            result.statistics.allocator_spill_count += placement.spill_count;
                            result.statistics.allocator_copy_count += placement.copy_count;
                            result.statistics.allocator_boundary_spill_count += placement.boundary_spill_count;
                            result.statistics.allocator_rematerialize_count += placement.rematerialize_count;
                            result.statistics.allocator_pinned_register_count += placement.pinned_register_count;
                        }
                    }
                }
            }
            scratch_end(machine_scratch);
        }
        if (machine_function_emitted)
        {
            continue;
        }
        result.statistics.fallback_function_count += options.register_allocator != CODEGEN_REGISTER_ALLOCATOR_NONE;
        if (target.cpu_arch == CPU_ARCH_X86_64)
        {
            codegen_emit_u8(&buffer, 0x55);
            bool unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity,
                                                             (u32)buffer.count - descriptor->code_offset,
                                                             CODEGEN_UNWIND_ACTION_PUSH_REGISTER, X64_REGISTER_RBP, 0);
            if (windows_dynamic_stack)
            {
                codegen_canonical_x64_adjust_stack_described(&buffer, frame_size, true, descriptor, unwind_action_capacity,
                                                             target.os == OPERATING_SYSTEM_WINDOWS);
                codegen_emit_u8(&buffer, 0x48);
                codegen_emit_u8(&buffer, 0x89);
                codegen_emit_u8(&buffer, 0xe5);
                unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity,
                                                            (u32)buffer.count - descriptor->code_offset,
                                                            CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, X64_REGISTER_RBP, 0) &&
                               unwind_valid;
            }
            else
            {
                codegen_emit_u8(&buffer, 0x48);
                codegen_emit_u8(&buffer, 0x89);
                codegen_emit_u8(&buffer, 0xe5);
                unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity,
                                                            (u32)buffer.count - descriptor->code_offset,
                                                            CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, X64_REGISTER_RBP, 0) &&
                               unwind_valid;
                codegen_canonical_x64_adjust_stack_described(&buffer, frame_size, true, descriptor, unwind_action_capacity,
                                                             target.os == OPERATING_SYSTEM_WINDOWS);
            }
            if (x64_save_rbx)
            {
                codegen_emit_u8(&buffer, 0x48);
                codegen_emit_u8(&buffer, 0x89);
                codegen_emit_u8(&buffer, 0x9d);
                codegen_emit_u32(&buffer, (u32)codegen_canonical_x64_rebase_frame_displacement(&buffer, -(s64)x64_rbx_save_offset,
                                                                                                  canonical_x64_frame_base_offset));
                unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, (u32)buffer.count - descriptor->code_offset,
                                                            CODEGEN_UNWIND_ACTION_SAVE_REGISTER, X64_REGISTER_RBX,
                                                            frame_size - x64_rbx_save_offset) &&
                               unwind_valid;
            }
            descriptor->prolog_size = (u32)buffer.count - descriptor->code_offset;
            if (!unwind_valid || buffer.error != CODEGEN_ERROR_NONE)
            {
                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_CAPACITY;
                return result;
            }
            if (canonical_va_save_displacement)
            {
                static u8 const gp_registers[] = {
                    7, 6, 2, 1, 8, 9,
                };
                for (u32 register_index = 0; register_index < BUSTER_ARRAY_LENGTH(gp_registers); register_index += 1)
                {
                    u8 reg = gp_registers[register_index];
                    codegen_emit_u8(&buffer, reg >= 8 ? 0x4c : 0x48);
                    codegen_emit_u8(&buffer, 0x89);
                    codegen_emit_u8(&buffer, (u8)(0x85 | ((reg & 7) << 3)));
                    codegen_emit_u32(&buffer, (u32)codegen_canonical_x64_rebase_frame_displacement(
                                                     &buffer, (s64)canonical_va_save_displacement + (s64)(register_index * 8), canonical_x64_frame_base_offset));
                }
                for (u32 register_index = 0; register_index < 8; register_index += 1)
                {
                    codegen_emit_u8(&buffer, 0xf3);
                    codegen_emit_u8(&buffer, 0x0f);
                    codegen_emit_u8(&buffer, 0x7f);
                    codegen_emit_u8(&buffer, (u8)(0x85 | (register_index << 3)));
                    codegen_emit_u32(&buffer, (u32)codegen_canonical_x64_rebase_frame_displacement(
                                                     &buffer, (s64)canonical_va_save_displacement + 48 + (s64)(register_index * 16), canonical_x64_frame_base_offset));
                }
            }
            else if (canonical_variadic && result.abi == CODEGEN_ABI_X86_64_WINDOWS)
            {
                static u8 const gp_registers[] = {
                    1,
                    2,
                    8,
                    9,
                };
                for (u32 register_index = 0; register_index < BUSTER_ARRAY_LENGTH(gp_registers); register_index += 1)
                {
                    u8 reg = gp_registers[register_index];
                    codegen_emit_u8(&buffer, reg >= 8 ? 0x4c : 0x48);
                    codegen_emit_u8(&buffer, 0x89);
                    codegen_emit_u8(&buffer, (u8)(0x85 | ((reg & 7) << 3)));
                    codegen_emit_u32(&buffer, (u32)codegen_canonical_x64_rebase_frame_displacement(
                                                     &buffer, 16 + (s64)register_index * 8, canonical_x64_frame_base_offset));
                }
            }
            if (x64_indirect_return)
            {
                codegen_emit_u8(&buffer, 0x48);
                codegen_emit_u8(&buffer, 0x89);
                codegen_emit_u8(&buffer, windows_indirect_return ? 0x8d : 0xbd);
                codegen_emit_u32(&buffer, (u32)codegen_canonical_x64_rebase_frame_displacement(&buffer, hidden_result_displacement,
                                                                                                  canonical_x64_frame_base_offset));
            }
        }
        else
        {
            codegen_emit_u32(&buffer, 0xa9bf7bfd);
            bool unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity,
                                                             (u32)buffer.count - descriptor->code_offset,
                                                             CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, 16);
            unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, (u32)buffer.count - descriptor->code_offset,
                                                        CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 29, 0) &&
                           unwind_valid;
            unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, (u32)buffer.count - descriptor->code_offset,
                                                        CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 30, 8) &&
                           unwind_valid;
            codegen_emit_u32(&buffer, 0x910003fd);
            unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, (u32)buffer.count - descriptor->code_offset,
                                                        CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, 29, 0) &&
                           unwind_valid;
            if (frame_size)
            {
                codegen_canonical_a64_adjust_stack_described(&buffer, frame_size, true, descriptor, unwind_action_capacity, windows_aarch64);
            }
            if (!codegen_canonical_a64_memory_operation(&buffer, 28, aarch64_frame_base_save_offset, 8, true, false))
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, (u32)buffer.count - descriptor->code_offset,
                                                        CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 28, aarch64_frame_base_save_offset) &&
                           unwind_valid;
            codegen_emit_u32(&buffer, 0x910003fc);
            if (windows_aarch64)
            {
                unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, (u32)buffer.count - descriptor->code_offset,
                                                            CODEGEN_UNWIND_ACTION_NOP, 0, 0) &&
                               unwind_valid;
            }
            descriptor->prolog_size = (u32)buffer.count - descriptor->code_offset;
            if (!unwind_valid || buffer.error != CODEGEN_ERROR_NONE)
            {
                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_CAPACITY;
                return result;
            }
            if (aarch64_indirect_return)
            {
                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 8, aarch64_hidden_result_offset, 8, true, false))
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
            }
            if (canonical_variadic && !aarch64_darwin_variadic)
            {
                for (u32 register_index = 0; register_index < 8; register_index += 1)
                {
                    if (!codegen_canonical_a64_frame_memory_operation(&buffer, register_index, aarch64_va_save_offset + register_index * 8, 8, true, false))
                    {
                        result.error = CODEGEN_ERROR_CAPACITY;
                        return result;
                    }
                }
            }
        }
        if (function->instruction_count > UINT32_MAX / 2)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        u32 branch_patch_capacity = function->instruction_count * 2;
        u32* block_offsets = arena_allocate(arena, u32, function->block_count);
        if (fragment_info)
        {
            fragment_info->block_offsets = block_offsets;
            fragment_info->block_count = function->block_count;
        }
        CCanonicalBranchPatch* branch_patches = arena_allocate(arena, CCanonicalBranchPatch, branch_patch_capacity);
        u32 branch_patch_count = 0;
#define C_BRANCH_PATCH_PUSH(...)                                                                                                                               \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        if (branch_patch_count >= branch_patch_capacity)                                                                                                       \
        {                                                                                                                                                      \
            result.error = CODEGEN_ERROR_CAPACITY;                                                                                                             \
            return result;                                                                                                                                     \
        }                                                                                                                                                      \
        branch_patches[branch_patch_count++] = (__VA_ARGS__);                                                                                                  \
    } while (0)
        bool x64_upper_vector_dirty = false;
        IrValueId x64_last_wide_vector_result = IR_VALUE_ID_INVALID;
        u32 x64_last_wide_vector_size = 0;
        // Every canonical value owns a frame slot, so an instruction that
        // consumes its predecessor's result reloads the slot that was just
        // written. Remember the frame displacement of the last full-width rax
        // store together with the buffer position immediately after it: while
        // nothing else has been emitted, rax still holds that slot's contents
        // and the reload is a no-op. Recording the buffer position is what
        // makes this safe without tracking clobbers, because any intervening
        // emission moves the position and invalidates the record. The only
        // other way to reach that position is a branch, and every branch in
        // this emitter targets either a block start (reset below) or an offset
        // inside its own instruction expansion.
        u64 x64_forwarded_store_end = UINT64_MAX;
        s32 x64_forwarded_store_displacement = 0;
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            IrBlock* emitted_block = function->blocks + block_index;
            block_offsets[block_index] = (u32)buffer.count;
            x64_forwarded_store_end = UINT64_MAX;
            // Validation's ownership proof covers this walk: every chain is a
            // simple path of in-range ids ending at last_instruction. The
            // counter that used to stand in for that proof re-walked the whole
            // function before it could notice a cycle, and the emitter already
            // trusts validation for everything it indexes below.
            IrInstructionId instruction_id = emitted_block->first_instruction;
            while (instruction_id.value != IR_ID_UNDERLYING_INVALID)
            {
                IrInstruction* instruction = function->instructions + instruction_id.value;
                result.failed_instruction = instruction_id;
                result.failed_opcode = instruction->opcode;
                if (instruction->opcode == IR_OPCODE_LOCAL && function->values[instruction->result.value].alignment <= 16)
                {
                    instruction_id = instruction->next;
                    continue;
                }
                IrSourceRange canonical_source = ir_instruction_canonical_source(function, instruction_id);
                // The line table is one of the four consumers that pay for a
                // line and a column, and the only one that asks per
                // instruction. Consecutive instructions overwhelmingly carry
                // the same range — every instruction of one expression comes
                // from one token — so the repeat is rejected on the offset the
                // range already holds, and a position is recovered only for an
                // offset that can still produce a row.
                if (result.line_entries && canonical_source.source.value != IR_ID_UNDERLYING_INVALID &&
                    (canonical_source.offset != recorded_source.offset || canonical_source.source.value != recorded_source.source.value))
                {
                    recorded_source = canonical_source;
                    IrSourcePosition position = ir_source_position(program, canonical_source);
                    codegen_record_line(result.line_entries, &result.line_entry_count, line_entry_capacity, (u32)buffer.count,
                                        canonical_source.source.value, position.line, position.column);
                }
                if (x64_upper_vector_dirty && !codegen_canonical_x64_instruction_preserves_wide_vector(program, instruction) &&
                    !codegen_canonical_x64_instruction_uses_wide_vector(program, function, instruction, target))
                {
                    codegen_emit_u8(&buffer, 0xc5);
                    codegen_emit_u8(&buffer, 0xf8);
                    codegen_emit_u8(&buffer, 0x77);
                    x64_upper_vector_dirty = false;
                    x64_last_wide_vector_result = IR_VALUE_ID_INVALID;
                    x64_last_wide_vector_size = 0;
                    result.statistics.vzeroupper_count += 1;
                }
                if (target.cpu_arch == CPU_ARCH_X86_64)
                {
                        s32 result_displacement = instruction->result.value == IR_ID_UNDERLYING_INVALID
                                                  ? 0
                                                  : codegen_canonical_x64_rebase_frame_displacement(&buffer,
                                                                                                     -(s64)value_offsets[instruction->result.value],
                                                                                                     canonical_x64_frame_base_offset);
#define C_X64_FRAME_DISPLACEMENT(offset)                                                                                                                        \
    codegen_canonical_x64_rebase_frame_displacement(&buffer, -(s64)(offset), canonical_x64_frame_base_offset)
#define C_X64_LOAD(register_opcode, value_id)                                                                                                                  \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        u8 c_x64_load_modrm = (u8)(register_opcode);                                                                                                            \
        s32 c_x64_load_displacement = C_X64_FRAME_DISPLACEMENT(value_offsets[(value_id).value]);                                                               \
        if (c_x64_load_modrm == 0x85 && buffer.count == x64_forwarded_store_end &&                                                                              \
            c_x64_load_displacement == x64_forwarded_store_displacement)                                                                                        \
        {                                                                                                                                                      \
            break;                                                                                                                                             \
        }                                                                                                                                                      \
        codegen_emit_u8(&buffer, 0x48);                                                                                                                        \
        codegen_emit_u8(&buffer, 0x8b);                                                                                                                        \
        codegen_emit_u8(&buffer, c_x64_load_modrm);                                                                                                            \
        codegen_emit_u32(&buffer, (u32)c_x64_load_displacement);                                                                                               \
    } while (0)
#define C_X64_STORE_RESULT()                                                                                                                                   \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        codegen_emit_u8(&buffer, 0x48);                                                                                                                        \
        codegen_emit_u8(&buffer, 0x89);                                                                                                                        \
        codegen_emit_u8(&buffer, 0x85);                                                                                                                        \
        codegen_emit_u32(&buffer, (u32)result_displacement);                                                                                                   \
        if (!buffer.error)                                                                                                                                     \
        {                                                                                                                                                      \
            x64_forwarded_store_end = buffer.count;                                                                                                            \
            x64_forwarded_store_displacement = result_displacement;                                                                                             \
        }                                                                                                                                                      \
    } while (0)
#define C_X64_LOAD_FLOAT(register_index, value_id, width)                                                                                                      \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        codegen_emit_u8(&buffer, (width) == 32 ? 0xf3 : 0xf2);                                                                                                 \
        codegen_emit_u8(&buffer, 0x0f);                                                                                                                        \
        codegen_emit_u8(&buffer, 0x10);                                                                                                                        \
        codegen_emit_u8(&buffer, (u8)(0x85 | ((register_index) << 3)));                                                                                        \
        codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[(value_id).value]));                                                            \
    } while (0)
#define C_X64_RECORD_FLOAT_STORE(width)                                                                                                                        \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        (void)(width);                                                                                                                                         \
    } while (0)
#define C_X64_RESTORE_RBX()                                                                                                                                    \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        if (x64_save_rbx)                                                                                                                                      \
        {                                                                                                                                                      \
            codegen_emit_u8(&buffer, 0x48);                                                                                                                    \
            codegen_emit_u8(&buffer, 0x8b);                                                                                                                    \
            codegen_emit_u8(&buffer, 0x9d);                                                                                                                    \
            codegen_emit_u32(&buffer, (u32)codegen_canonical_x64_rebase_frame_displacement(&buffer, -(s64)x64_rbx_save_offset,                  \
                                                                                              canonical_x64_frame_base_offset));                          \
        }                                                                                                                                                      \
    } while (0)
                    if (instruction->opcode == IR_OPCODE_LOCAL)
                    {
                        IrValue* local = function->values + instruction->result.value;
                        u32 local_alignment = local->alignment;
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x8d);
                        codegen_emit_u8(&buffer, 0x85);
                        codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(aligned_local_offsets[instruction->result.value]));
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x05);
                        codegen_emit_u32(&buffer, local_alignment - 1);
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x25);
                        codegen_emit_u32(&buffer, 0 - local_alignment);
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_STACK_ALLOCATE)
                    {
                        u32 stack_alignment = (u32)instruction->immediates[0];
                        stack_alignment = BUSTER_MAX(stack_alignment, 16);
                        C_X64_LOAD(0x85, instruction->operands[0]);
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x05);
                        codegen_emit_u32(&buffer, stack_alignment - 1);
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x25);
                        codegen_emit_u32(&buffer, 0 - stack_alignment);
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x3d);
                        codegen_emit_u32(&buffer, 4096);
                        codegen_emit_u8(&buffer, 0x72);
                        codegen_emit_u8(&buffer, 19);
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x81);
                        codegen_emit_u8(&buffer, 0xec);
                        codegen_emit_u32(&buffer, 4096);
                        codegen_emit_u8(&buffer, 0xf6);
                        codegen_emit_u8(&buffer, 0x04);
                        codegen_emit_u8(&buffer, 0x24);
                        codegen_emit_u8(&buffer, 0);
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x2d);
                        codegen_emit_u32(&buffer, 4096);
                        codegen_emit_u8(&buffer, 0xeb);
                        codegen_emit_u8(&buffer, 0xe5);
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x29);
                        codegen_emit_u8(&buffer, 0xc4);
                        codegen_emit_u8(&buffer, 0xf6);
                        codegen_emit_u8(&buffer, 0x04);
                        codegen_emit_u8(&buffer, 0x24);
                        codegen_emit_u8(&buffer, 0);
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x89);
                        codegen_emit_u8(&buffer, 0xe0);
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_STACK_SAVE)
                    {
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x89);
                        codegen_emit_u8(&buffer, 0xe0);
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_STACK_RESTORE)
                    {
                        C_X64_LOAD(0x85, instruction->operands[0]);
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x89);
                        codegen_emit_u8(&buffer, 0xc4);
                    }
                    else if (instruction->opcode == IR_OPCODE_ARGUMENT)
                    {
                        static u8 const system_v[] = {
                            7, 6, 2, 1, 8, 9,
                        };
                        static u8 const windows[] = {
                            1,
                            2,
                            8,
                            9,
                        };
                        u32 argument_index = (u32)instruction->immediates[0];
                        IrType* function_type = ir_type_from_id(&program->types, function->canonical_type);
                        if (!function_type || function_type->kind != IR_TYPE_FUNCTION || argument_index >= function_type->parameter_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u8 const* registers = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? windows : system_v;
                        u32 register_count = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? BUSTER_ARRAY_LENGTH(windows) : BUSTER_ARRAY_LENGTH(system_v);
                        u32 register_index = x64_indirect_return ? 1 : 0;
                        u32 float_register_index = 0;
                        // Bytes, not eightbytes: a parameter the caller had to
                        // align sits past a gap, and reading it back means
                        // walking the incoming area by the same rule the
                        // outgoing one was filled by.
                        u64 prior_stack_bytes = 0;
                        for (u32 prior_index = 0; prior_index < argument_index; prior_index += 1)
                        {
                            u32 prior_parts = 1;
                            bool prior_aggregate =
                                codegen_canonical_integer_aggregate_parts(program, function_type->parameter_types[prior_index], &prior_parts);
                            IrType* prior_type = ir_type_from_id(&program->types, function_type->parameter_types[prior_index]);
                            CodegenCanonicalAbiValue prior_aggregate_abi =
                                codegen_canonical_aggregate_abi(program, function_type->parameter_types[prior_index], result.abi, false, false);
                            bool prior_in_registers = codegen_canonical_x64_abi_value_in_registers(&prior_aggregate_abi, &target);
                            if (prior_aggregate_abi.part_count && !prior_aggregate_abi.memory && !prior_aggregate_abi.indirect)
                            {
                                prior_aggregate = true;
                                if (prior_in_registers)
                                {
                                    prior_parts = prior_aggregate_abi.part_count;
                                }
                            }
                            if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && prior_aggregate_abi.part_count && !prior_aggregate_abi.memory && prior_in_registers)
                            {
                                u32 integer_count = 0;
                                u32 float_count = 0;
                                for (u32 part = 0; part < prior_aggregate_abi.part_count; part += 1)
                                {
                                    if (codegen_canonical_abi_part_is_float(prior_aggregate_abi.parts[part].abi_class))
                                    {
                                        float_count += 1;
                                    }
                                    else
                                    {
                                        integer_count += 1;
                                    }
                                }
                                if (register_index + integer_count <= register_count && float_register_index + float_count <= 8)
                                {
                                    register_index += integer_count;
                                    float_register_index += float_count;
                                }
                                else
                                {
                                    prior_stack_bytes =
                                        codegen_canonical_x64_stack_argument_offset(prior_stack_bytes,
                                                                                    codegen_canonical_x64_stack_argument_alignment(prior_type)) +
                                        ((prior_type->layout.size + 7) & ~(u64)7);
                                }
                                continue;
                            }
                            if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && prior_type && prior_type->kind == IR_TYPE_FLOAT)
                            {
                                if (float_register_index < 8)
                                {
                                    float_register_index += 1;
                                }
                                else
                                {
                                    prior_stack_bytes =
                                        codegen_canonical_x64_stack_argument_offset(prior_stack_bytes,
                                                                                    codegen_canonical_x64_stack_argument_alignment(prior_type)) +
                                        8;
                                }
                                continue;
                            }
                            bool prior_memory = prior_aggregate && prior_type && prior_type->layout.size > 16 &&
                                                result.abi == CODEGEN_ABI_X86_64_SYSTEM_V;
                            if (prior_aggregate && result.abi == CODEGEN_ABI_X86_64_WINDOWS)
                            {
                                prior_parts = 1;
                            }
                            if (!prior_memory && register_index + prior_parts <= register_count)
                            {
                                register_index += prior_parts;
                            }
                            else
                            {
                                u32 prior_alignment =
                                    result.abi == CODEGEN_ABI_X86_64_SYSTEM_V ? codegen_canonical_x64_stack_argument_alignment(prior_type) : 8;
                                prior_stack_bytes = codegen_canonical_x64_stack_argument_offset(prior_stack_bytes, prior_alignment) + (u64)prior_parts * 8;
                            }
                        }
                        u32 part_count = 1;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &part_count);
                        CodegenCanonicalAbiValue argument_aggregate_abi =
                            codegen_canonical_aggregate_abi(program, instruction->canonical_type, result.abi, false, false);
                        // How many eightbytes the argument occupies if it came
                        // in on the stack, which is its size and not the number
                        // of registers it would have taken: one zmm holds a
                        // 64-byte vector, but its stack image is still eight.
                        IrType* argument_stack_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        u32 stack_part_count =
                            argument_stack_type && argument_stack_type->layout.resolved ? (u32)((argument_stack_type->layout.size + 7) / 8) : part_count;
                        bool argument_in_registers = codegen_canonical_x64_abi_value_in_registers(&argument_aggregate_abi, &target);
                        if (argument_aggregate_abi.part_count && !argument_aggregate_abi.memory && !argument_aggregate_abi.indirect)
                        {
                            aggregate = true;
                            if (argument_in_registers)
                            {
                                part_count = argument_aggregate_abi.part_count;
                            }
                        }
                        bool windows_indirect = result.abi == CODEGEN_ABI_X86_64_WINDOWS && argument_aggregate_abi.indirect;
                        IrType* argument_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && argument_type && argument_type->kind == IR_TYPE_FLOAT)
                        {
                            if (argument_type->bit_width != 32 && argument_type->bit_width != 64)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            if (float_register_index < 8)
                            {
                                codegen_emit_u8(&buffer, argument_type->bit_width == 32 ? 0xf3 : 0xf2);
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x11);
                                codegen_emit_u8(&buffer, (u8)(0x85 | (float_register_index << 3)));
                                codegen_emit_u32(&buffer, (u32)result_displacement);
                            }
                            else
                            {
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)(16 + codegen_canonical_x64_stack_argument_offset(prior_stack_bytes, 8)));
                                C_X64_STORE_RESULT();
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (result.abi == CODEGEN_ABI_X86_64_WINDOWS && argument_type && argument_type->kind == IR_TYPE_FLOAT &&
                            register_index < register_count)
                        {
                            if (argument_type->bit_width != 32 && argument_type->bit_width != 64)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            codegen_emit_u8(&buffer, argument_type->bit_width == 32 ? 0xf3 : 0xf2);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0x11);
                            codegen_emit_u8(&buffer, (u8)(0x85 | (register_index << 3)));
                            codegen_emit_u32(&buffer, (u32)result_displacement);
                            instruction_id = instruction->next;
                            continue;
                        }
                        u32 system_v_integer_parts = 0;
                        u32 system_v_float_parts = 0;
                        bool system_v_register_aggregate =
                            result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && argument_aggregate_abi.part_count && !argument_aggregate_abi.memory && argument_in_registers;
                        // "Aggregates over two eightbytes are MEMORY" is a rule
                        // about aggregates; a 32- or 64-byte vector arrives in
                        // a vector register on a target that has one that wide.
                        // The IR ABI and the target between them have already
                        // said which of the two this is, so the size heuristic
                        // only speaks when they did not.
                        bool system_v_memory = aggregate && argument_type && argument_type->layout.size > 16 &&
                                               result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && !system_v_register_aggregate;
                        if (system_v_register_aggregate)
                        {
                            for (u32 part = 0; part < argument_aggregate_abi.part_count; part += 1)
                            {
                                if (codegen_canonical_abi_part_is_float(argument_aggregate_abi.parts[part].abi_class))
                                {
                                    system_v_float_parts += 1;
                                }
                                else
                                {
                                    system_v_integer_parts += 1;
                                }
                            }
                        }
                        u32 register_parts = windows_indirect ? 1 : part_count;
                        if (!aggregate)
                        {
                            IrType* parameter_type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (parameter_type && (parameter_type->kind == IR_TYPE_STRUCT || parameter_type->kind == IR_TYPE_UNION))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                        }
                        if (system_v_memory || (system_v_register_aggregate ? register_index + system_v_integer_parts > register_count ||
                                                                                  float_register_index + system_v_float_parts > 8
                                                                            : register_index + register_parts > register_count))
                        {
                            u32 first_stack_offset = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? 48 : 16;
                            // The caller placed this one at its own alignment,
                            // so skip the padding it left behind.
                            u64 argument_stack_offset = codegen_canonical_x64_stack_argument_offset(
                                prior_stack_bytes,
                                result.abi == CODEGEN_ABI_X86_64_SYSTEM_V ? codegen_canonical_x64_stack_argument_alignment(argument_stack_type) : 8);
                            if (windows_indirect)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)codegen_canonical_x64_rebase_frame_displacement(
                                                                 &buffer, first_stack_offset + (s64)argument_stack_offset, canonical_x64_frame_base_offset));
                                for (u32 part_index = 0; part_index < part_count; part_index += 1)
                                {
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8b);
                                u32 part_offset = part_index * 8;
                                if (!part_offset)
                                {
                                    codegen_emit_u8(&buffer, 0x10);
                                }
                                else if (part_offset <= INT8_MAX)
                                {
                                    codegen_emit_u8(&buffer, 0x50);
                                    codegen_emit_u8(&buffer, (u8)part_offset);
                                }
                                else
                                {
                                    codegen_emit_u8(&buffer, 0x90);
                                    codegen_emit_u32(&buffer, part_offset);
                                }
                                    codegen_emit_u8(&buffer, 0x48);
                                    codegen_emit_u8(&buffer, 0x89);
                                    codegen_emit_u8(&buffer, 0x95);
                                    codegen_emit_u32(&buffer, (u32)(result_displacement + (s32)(part_index * 8)));
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            for (u32 part_index = 0; part_index < stack_part_count; part_index += 1)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)codegen_canonical_x64_rebase_frame_displacement(
                                                                 &buffer, first_stack_offset + (s64)argument_stack_offset + (s64)part_index * 8,
                                                                 canonical_x64_frame_base_offset));
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)(result_displacement + (s32)(part_index * 8)));
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (windows_indirect)
                        {
                            u8 source_reg = registers[register_index];
                            for (u32 part_index = 0; part_index < part_count; part_index += 1)
                            {
                                u32 part_offset = part_index * 8;
                                codegen_emit_u8(&buffer, source_reg >= 8 ? 0x49 : 0x48);
                                codegen_emit_u8(&buffer, 0x8b);
                                if (!part_offset)
                                {
                                    codegen_emit_u8(&buffer, (u8)(source_reg & 7));
                                }
                                else if (part_offset <= INT8_MAX)
                                {
                                    codegen_emit_u8(&buffer, (u8)(0x40 | (source_reg & 7)));
                                    codegen_emit_u8(&buffer, (u8)part_offset);
                                }
                                else
                                {
                                    codegen_emit_u8(&buffer, (u8)(0x80 | (source_reg & 7)));
                                    codegen_emit_u32(&buffer, part_offset);
                                }
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)(result_displacement + (s32)(part_index * 8)));
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        for (u32 part_index = 0; part_index < part_count; part_index += 1)
                        {
                            if (system_v_register_aggregate && codegen_canonical_abi_part_is_float(argument_aggregate_abi.parts[part_index].abi_class))
                            {
                                u32 part_offset = argument_aggregate_abi.parts[part_index].value_offset;
                                u32 part_size = argument_aggregate_abi.parts[part_index].size;
                                if (!codegen_canonical_x64_float_memory(&buffer, target, float_register_index, result_displacement + (s32)part_offset, part_size, true))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                float_register_index += 1;
                                continue;
                            }
                            u8 reg = registers[register_index++];
                            if (reg >= 8)
                            {
                                codegen_emit_u8(&buffer, 0x4c);
                            }
                            else
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0x89);
                            codegen_emit_u8(&buffer, (u8)(0x85 | ((reg & 7) << 3)));
                            codegen_emit_u32(
                                &buffer, (u32)(result_displacement +
                                               (s32)(system_v_register_aggregate ? argument_aggregate_abi.parts[part_index].value_offset : part_index * 8)));
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_GLOBAL || instruction->opcode == IR_OPCODE_FUNCTION)
                    {
                        if (instruction->opcode == IR_OPCODE_FUNCTION && direct_call_uses[instruction->result.value] == 1)
                        {
                            codegen_emit_u8(&buffer, 0x31);
                            codegen_emit_u8(&buffer, 0xc0);
                            C_X64_STORE_RESULT();
                            instruction_id = instruction->next;
                            continue;
                        }
                        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
                        bool is_thread_local = instruction->opcode == IR_OPCODE_GLOBAL && symbol && symbol->is_thread_local;
                        if (is_thread_local)
                        {
                            if (target.os == OPERATING_SYSTEM_WINDOWS)
                            {
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, 0x05);
                                u32 index_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0);
                                codegen_emit_u8(&buffer, 0x65);
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, 0x14);
                                codegen_emit_u8(&buffer, 0x25);
                                codegen_emit_u32(&buffer, 0x58);
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, 0x04);
                                codegen_emit_u8(&buffer, 0xc2);
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8d);
                                codegen_emit_u8(&buffer, 0x80);
                                u32 value_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0);
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = index_offset,
                                    .is_thread_local = true,
                                    .thread_local_index = true,
                                };
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = value_offset,
                                    .is_thread_local = true,
                                };
                            }
                            else if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, 0x3d);
                                u32 descriptor_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0);
                                codegen_emit_u8(&buffer, 0xff);
                                codegen_emit_u8(&buffer, 0x17);
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = descriptor_offset,
                                    .is_thread_local = true,
                                };
                            }
                            else if (target.os != OPERATING_SYSTEM_LINUX && target.os != OPERATING_SYSTEM_ANDROID)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            else
                            {
                                codegen_emit_u8(&buffer, 0x64);
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, 0x04);
                                codegen_emit_u8(&buffer, 0x25);
                                codegen_emit_u32(&buffer, 0);
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8d);
                                codegen_emit_u8(&buffer, 0x80);
                                u32 offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0);
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .entity = ANALYSIS_ENTITY_ID_INVALID,
                                    .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
                                    .symbol = instruction->symbol,
                                    .offset = offset,
                                    .is_thread_local = true,
                                };
                            }
                        }
                        else
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8d);
                            codegen_emit_u8(&buffer, 0x05);
                            u32 offset = (u32)buffer.count;
                            codegen_emit_u32(&buffer, 0);
                            result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                .entity = ANALYSIS_ENTITY_ID_INVALID,
                                .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
                                .symbol = instruction->symbol,
                                .offset = offset,
                            };
                        }
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x89);
                        codegen_emit_u8(&buffer, 0x85);
                        codegen_emit_u32(&buffer, (u32)result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_LOAD || instruction->opcode == IR_OPCODE_ATOMIC_LOAD)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        u32 aggregate_parts = 0;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &aggregate_parts);
                        IrType* aggregate_type = aggregate ? ir_type_from_id(&program->types, instruction->canonical_type) : 0;
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        if (instruction->opcode == IR_OPCODE_ATOMIC_LOAD && aggregate)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        if (aggregate)
                        {
                            if (!aggregate_type || !aggregate_type->layout.resolved)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            if (indirect)
                            {
                                C_X64_LOAD(0x95, instruction->operands[0]);
                            }
                            for (u32 part_index = 0; part_index < aggregate_parts; part_index += 1)
                            {
                                u64 part_offset = (u64)part_index * 8;
                                u64 part_size = BUSTER_MIN((u64)8, aggregate_type->layout.size - part_offset);
                                u64 part_copied = 0;
                                while (part_copied < part_size)
                                {
                                    u64 remaining = part_size - part_copied;
                                    u32 chunk = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
                                    u64 copy_offset = part_offset + part_copied;
                                    if (chunk == 8)
                                    {
                                        codegen_emit_u8(&buffer, 0x48);
                                    }
                                    if (chunk == 1 || chunk == 2)
                                    {
                                        codegen_emit_u8(&buffer, 0x0f);
                                        codegen_emit_u8(&buffer, chunk == 1 ? 0xb6 : 0xb7);
                                    }
                                    else
                                    {
                                        codegen_emit_u8(&buffer, 0x8b);
                                    }
                                    if (indirect)
                                    {
                                        if (copy_offset > INT32_MAX)
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                            return result;
                                        }
                                        if (!copy_offset)
                                        {
                                            codegen_emit_u8(&buffer, 0x02);
                                        }
                                        else if (copy_offset <= INT8_MAX)
                                        {
                                            codegen_emit_u8(&buffer, 0x42);
                                            codegen_emit_u8(&buffer, (u8)copy_offset);
                                        }
                                        else
                                        {
                                            codegen_emit_u8(&buffer, 0x82);
                                            codegen_emit_u32(&buffer, (u32)copy_offset);
                                        }
                                    }
                                    else
                                    {
                                        codegen_emit_u8(&buffer, 0x85);
                                        codegen_emit_u32(&buffer, (u32)(C_X64_FRAME_DISPLACEMENT(value_offsets[instruction->operands[0].value]) + (s32)copy_offset));
                                    }
                                    if (chunk == 8)
                                    {
                                        codegen_emit_u8(&buffer, 0x48);
                                    }
                                    else if (chunk == 2)
                                    {
                                        codegen_emit_u8(&buffer, 0x66);
                                    }
                                    codegen_emit_u8(&buffer, chunk == 1 ? 0x88 : 0x89);
                                    codegen_emit_u8(&buffer, 0x85);
                                    codegen_emit_u32(&buffer, (u32)(result_displacement + (s32)copy_offset));
                                    part_copied += chunk;
                                }
                            }
                        }
                        else if (indirect)
                        {
                            IrType* loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (!loaded_type || !loaded_type->layout.resolved ||
                                (loaded_type->layout.size != 1 && loaded_type->layout.size != 2 && loaded_type->layout.size != 4 &&
                                 loaded_type->layout.size != 8))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            C_X64_LOAD(0x85, instruction->operands[0]);
                            if (loaded_type->layout.size == 1 || loaded_type->layout.size == 2)
                            {
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, loaded_type->layout.size == 1 ? 0xb6 : 0xb7);
                            }
                            else
                            {
                                if (loaded_type->layout.size == 8)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                }
                                codegen_emit_u8(&buffer, 0x8b);
                            }
                            codegen_emit_u8(&buffer, 0x00);
                        }
                        else
                        {
                            C_X64_LOAD(0x85, instruction->operands[0]);
                        }
                        if (!aggregate && instruction->result.value != IR_ID_UNDERLYING_INVALID)
                        {
                            C_X64_STORE_RESULT();
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_INDEX)
                    {
                        IrValueId base = instruction->operands[0];
                        IrInstruction* base_definition = function->instructions + function->values[base.value].definition.value;
                        IrType* base_type = ir_type_from_id(&program->types, function->values[base.value].canonical_type);
                        if (base_definition->opcode == IR_OPCODE_LOCAL || (function->values[base.value].category == IR_VALUE_VALUE && base_type &&
                                                                           (base_type->kind == IR_TYPE_ARRAY || base_type->kind == IR_TYPE_VECTOR)))
                        {
                            if (base_definition->opcode == IR_OPCODE_LOCAL && function->values[base.value].alignment > 16)
                            {
                                C_X64_LOAD(0x85, base);
                            }
                            else
                            {
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8d);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[base.value]));
                            }
                        }
                        else
                        {
                            C_X64_LOAD(0x85, base);
                        }
                        C_X64_LOAD(0x8d, instruction->operands[1]);
                        IrType* index_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
                        IrType* element = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (!index_type || index_type->kind != IR_TYPE_INTEGER || !element || element->layout.size > UINT32_MAX)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        if (index_type->is_signed && index_type->bit_width < 64)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            if (index_type->bit_width == 32)
                            {
                                codegen_emit_u8(&buffer, 0x63);
                                codegen_emit_u8(&buffer, 0xc9);
                            }
                            else if (index_type->bit_width == 8 || index_type->bit_width == 16)
                            {
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, index_type->bit_width == 8 ? 0xbe : 0xbf);
                                codegen_emit_u8(&buffer, 0xc9);
                            }
                            else
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                        }
                        // Byte-element indexing scales by one, so the multiply
                        // is the identity. The following add overwrites flags
                        // without reading them.
                        if (element->layout.size != 1)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x69);
                            codegen_emit_u8(&buffer, 0xc9);
                            codegen_emit_u32(&buffer, (u32)element->layout.size);
                        }
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x01);
                        codegen_emit_u8(&buffer, 0xc8);
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_ADDRESS_OF)
                    {
                        IrValueId object = instruction->operands[0];
                        IrInstruction* definition = function->instructions + function->values[object.value].definition.value;
                        if (definition->opcode == IR_OPCODE_LOCAL)
                        {
                            if (function->values[object.value].alignment > 16)
                            {
                                C_X64_LOAD(0x85, object);
                            }
                            else
                            {
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8d);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[object.value]));
                            }
                        }
                        else
                        {
                            C_X64_LOAD(0x85, object);
                        }
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_DEREFERENCE)
                    {
                        C_X64_LOAD(0x85, instruction->operands[0]);
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_FIELD)
                    {
                        IrValueId base = instruction->operands[0];
                        IrInstruction* definition = function->instructions + function->values[base.value].definition.value;
                        if (definition->opcode == IR_OPCODE_LOCAL)
                        {
                            if (function->values[base.value].alignment > 16)
                            {
                                C_X64_LOAD(0x85, base);
                            }
                            else
                            {
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8d);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[base.value]));
                            }
                        }
                        else
                        {
                            C_X64_LOAD(0x85, base);
                        }
                        IrType* aggregate = ir_type_from_id(&program->types, function->values[base.value].canonical_type);
                        u64 field_index = instruction->immediates[0];
                        if (!aggregate || field_index >= aggregate->field_count || aggregate->fields[field_index].offset > INT32_MAX)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        // The first field of an aggregate sits at offset zero,
                        // so the address arithmetic is the identity. Only the
                        // result store follows, and it does not read flags.
                        if (aggregate->fields[field_index].offset)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x05);
                            codegen_emit_u32(&buffer, (u32)aggregate->fields[field_index].offset);
                        }
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_CAST)
                    {
                        IrType* source_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
                        IrType* target_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        C_X64_LOAD(0x85, instruction->operands[0]);
                        IrConversionOperation conversion = instruction->conversion_operation;
                        if (!target_type || !source_type)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        if (conversion == IR_CONVERSION_INTEGER_SIGN_EXTEND && source_type->kind == IR_TYPE_INTEGER)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            if (source_type->bit_width == 8 || source_type->bit_width == 16)
                            {
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, source_type->bit_width == 8 ? 0xbe : 0xbf);
                                codegen_emit_u8(&buffer, 0xc0);
                            }
                            else if (source_type->bit_width == 32)
                            {
                                codegen_emit_u8(&buffer, 0x63);
                                codegen_emit_u8(&buffer, 0xc0);
                            }
                        }
                        else if (conversion == IR_CONVERSION_INTEGER_ZERO_EXTEND && source_type->kind == IR_TYPE_INTEGER &&
                                 target_type->kind == IR_TYPE_INTEGER)
                        {
                            if (source_type->bit_width == 8 || source_type->bit_width == 16)
                            {
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, source_type->bit_width == 8 ? 0xb6 : 0xb7);
                                codegen_emit_u8(&buffer, 0xc0);
                            }
                            else if (source_type->bit_width == 32)
                            {
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0xc0);
                            }
                        }
                        else if ((conversion == IR_CONVERSION_INTEGER_TRUNCATE || conversion == IR_CONVERSION_INTEGER_REINTERPRET) &&
                                 source_type->kind == IR_TYPE_INTEGER && target_type->kind == IR_TYPE_INTEGER)
                        {
                            u32 effective_bit_width = conversion == IR_CONVERSION_INTEGER_REINTERPRET && source_type->bit_width < target_type->bit_width
                                                          ? source_type->bit_width
                                                          : target_type->bit_width;
                            if (effective_bit_width == 8 || effective_bit_width == 16)
                            {
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, effective_bit_width == 8 ? 0xb6 : 0xb7);
                                codegen_emit_u8(&buffer, 0xc0);
                            }
                            else if (effective_bit_width == 32)
                            {
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0xc0);
                            }
                        }
                        else if ((conversion == IR_CONVERSION_FLOAT_EXTEND || conversion == IR_CONVERSION_FLOAT_TRUNCATE) &&
                                 source_type->kind == IR_TYPE_FLOAT && target_type->kind == IR_TYPE_FLOAT)
                        {
                            codegen_emit_u8(&buffer, source_type->bit_width == 32 ? 0xf3 : 0xf2);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0x10);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[instruction->operands[0].value]));
                            codegen_emit_u8(&buffer, conversion == IR_CONVERSION_FLOAT_EXTEND ? 0xf3 : 0xf2);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0x5a);
                            codegen_emit_u8(&buffer, 0xc0);
                            codegen_emit_u8(&buffer, target_type->bit_width == 32 ? 0xf3 : 0xf2);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0x11);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u32(&buffer, (u32)result_displacement);
                            instruction_id = instruction->next;
                            continue;
                        }
                        else if (target_type->kind == IR_TYPE_FLOAT && source_type->kind == IR_TYPE_INTEGER &&
                                 (conversion == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT || conversion == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT))
                        {
                            if (target_type->bit_width != 32 && target_type->bit_width != 64)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            if (conversion == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT)
                            {
                                if (source_type->bit_width == 8 || source_type->bit_width == 16)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                    codegen_emit_u8(&buffer, 0x0f);
                                    codegen_emit_u8(&buffer, source_type->bit_width == 8 ? 0xbe : 0xbf);
                                    codegen_emit_u8(&buffer, 0xc0);
                                }
                                else if (source_type->bit_width == 32)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                    codegen_emit_u8(&buffer, 0x63);
                                    codegen_emit_u8(&buffer, 0xc0);
                                }
                            }
                            else if (source_type->bit_width == 8 || source_type->bit_width == 16)
                            {
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, source_type->bit_width == 8 ? 0xb6 : 0xb7);
                                codegen_emit_u8(&buffer, 0xc0);
                            }
                            else if (source_type->bit_width == 32)
                            {
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0xc0);
                            }
                            if (conversion == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT && source_type->bit_width == 64)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u8(&buffer, 0xc0);
                                codegen_emit_u8(&buffer, 0x79);
                                codegen_emit_u8(&buffer, 23);
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0xc1);
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0xd1);
                                codegen_emit_u8(&buffer, 0xe8);
                                codegen_emit_u8(&buffer, 0x83);
                                codegen_emit_u8(&buffer, 0xe1);
                                codegen_emit_u8(&buffer, 1);
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x09);
                                codegen_emit_u8(&buffer, 0xc8);
                                codegen_emit_u8(&buffer, target_type->bit_width == 32 ? 0xf3 : 0xf2);
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x2a);
                                codegen_emit_u8(&buffer, 0xc0);
                                codegen_emit_u8(&buffer, target_type->bit_width == 32 ? 0xf3 : 0xf2);
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x58);
                                codegen_emit_u8(&buffer, 0xc0);
                                codegen_emit_u8(&buffer, 0xeb);
                                codegen_emit_u8(&buffer, 5);
                            }
                            codegen_emit_u8(&buffer, target_type->bit_width == 32 ? 0xf3 : 0xf2);
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0x2a);
                            codegen_emit_u8(&buffer, 0xc0);
                            codegen_emit_u8(&buffer, target_type->bit_width == 32 ? 0xf3 : 0xf2);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0x11);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u32(&buffer, (u32)result_displacement);
                            instruction_id = instruction->next;
                            continue;
                        }
                        else if (source_type->kind == IR_TYPE_FLOAT && target_type->kind == IR_TYPE_INTEGER &&
                                 (conversion == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER || conversion == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER))
                        {
                            codegen_emit_u8(&buffer, source_type->bit_width == 32 ? 0xf3 : 0xf2);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0x10);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[instruction->operands[0].value]));
                            if (conversion == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER && target_type->bit_width == 64)
                            {
                                if (source_type->bit_width == 32)
                                {
                                    codegen_emit_u8(&buffer, 0xb8);
                                    codegen_emit_u32(&buffer, 0x5f000000);
                                }
                                else
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                    codegen_emit_u8(&buffer, 0xb8);
                                    codegen_emit_u64(&buffer, UINT64_C(0x43e0000000000000));
                                }
                                codegen_emit_u8(&buffer, 0x66);
                                if (source_type->bit_width == 64)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                }
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x6e);
                                codegen_emit_u8(&buffer, 0xc8);
                                if (source_type->bit_width == 64)
                                {
                                    codegen_emit_u8(&buffer, 0x66);
                                }
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x2e);
                                codegen_emit_u8(&buffer, 0xc1);
                                codegen_emit_u8(&buffer, 0x72);
                                codegen_emit_u8(&buffer, 24);
                                codegen_emit_u8(&buffer, source_type->bit_width == 32 ? 0xf3 : 0xf2);
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x5c);
                                codegen_emit_u8(&buffer, 0xc1);
                                codegen_emit_u8(&buffer, source_type->bit_width == 32 ? 0xf3 : 0xf2);
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x2c);
                                codegen_emit_u8(&buffer, 0xc0);
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0xb9);
                                codegen_emit_u64(&buffer, UINT64_C(0x8000000000000000));
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x09);
                                codegen_emit_u8(&buffer, 0xc8);
                                codegen_emit_u8(&buffer, 0xeb);
                                codegen_emit_u8(&buffer, 5);
                            }
                            codegen_emit_u8(&buffer, source_type->bit_width == 32 ? 0xf3 : 0xf2);
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0x2c);
                            codegen_emit_u8(&buffer, 0xc0);
                        }
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_STORE || instruction->opcode == IR_OPCODE_ATOMIC_STORE)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        u32 aggregate_parts = 0;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, function->values[instruction->operands[1].value].canonical_type,
                                                                                   &aggregate_parts);
                        IrType* stored_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
                        if (!aggregate && stored_type && stored_type->layout.resolved && stored_type->layout.size > 8 &&
                            stored_type->layout.size <= (u64)UINT32_MAX * 8)
                        {
                            aggregate = true;
                            aggregate_parts = (u32)((stored_type->layout.size + 7) / 8);
                        }
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        if (instruction->opcode == IR_OPCODE_ATOMIC_STORE && aggregate)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        if (aggregate)
                        {
                            if (!stored_type || !stored_type->layout.resolved)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            if (indirect)
                            {
                                C_X64_LOAD(0x95, instruction->operands[0]);
                            }
                            for (u32 part_index = 0; part_index < aggregate_parts; part_index += 1)
                            {
                                u64 part_offset = (u64)part_index * 8;
                                u64 part_size = BUSTER_MIN((u64)8, stored_type->layout.size - part_offset);
                                u64 part_copied = 0;
                                while (part_copied < part_size)
                                {
                                    u64 remaining = part_size - part_copied;
                                    u32 chunk = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
                                    u64 copy_offset = part_offset + part_copied;
                                    if (chunk == 8)
                                    {
                                        codegen_emit_u8(&buffer, 0x48);
                                    }
                                    if (chunk == 1 || chunk == 2)
                                    {
                                        codegen_emit_u8(&buffer, 0x0f);
                                        codegen_emit_u8(&buffer, chunk == 1 ? 0xb6 : 0xb7);
                                    }
                                    else
                                    {
                                        codegen_emit_u8(&buffer, 0x8b);
                                    }
                                    codegen_emit_u8(&buffer, 0x85);
                                    codegen_emit_u32(&buffer, (u32)(C_X64_FRAME_DISPLACEMENT(value_offsets[instruction->operands[1].value]) + (s32)copy_offset));
                                    if (chunk == 8)
                                    {
                                        codegen_emit_u8(&buffer, 0x48);
                                    }
                                    else if (chunk == 2)
                                    {
                                        codegen_emit_u8(&buffer, 0x66);
                                    }
                                    codegen_emit_u8(&buffer, chunk == 1 ? 0x88 : 0x89);
                                    if (indirect)
                                    {
                                        if (copy_offset > INT32_MAX)
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                            return result;
                                        }
                                        if (!copy_offset)
                                        {
                                            codegen_emit_u8(&buffer, 0x02);
                                        }
                                        else if (copy_offset <= INT8_MAX)
                                        {
                                            codegen_emit_u8(&buffer, 0x42);
                                            codegen_emit_u8(&buffer, (u8)copy_offset);
                                        }
                                        else
                                        {
                                            codegen_emit_u8(&buffer, 0x82);
                                            codegen_emit_u32(&buffer, (u32)copy_offset);
                                        }
                                    }
                                    else
                                    {
                                        codegen_emit_u8(&buffer, 0x85);
                                        codegen_emit_u32(&buffer, (u32)(C_X64_FRAME_DISPLACEMENT(value_offsets[instruction->operands[0].value]) + (s32)copy_offset));
                                    }
                                    part_copied += chunk;
                                }
                            }
                        }
                        else
                        {
                            C_X64_LOAD(0x85, instruction->operands[1]);
                            if (instruction->opcode == IR_OPCODE_ATOMIC_STORE && instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL)
                            {
                                if (!stored_type || !stored_type->layout.resolved ||
                                    (stored_type->layout.size != 1 && stored_type->layout.size != 2 && stored_type->layout.size != 4 &&
                                     stored_type->layout.size != 8))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                if (indirect)
                                {
                                    C_X64_LOAD(0x95, instruction->operands[0]);
                                }
                                if (stored_type->layout.size == 2)
                                {
                                    codegen_emit_u8(&buffer, 0x66);
                                }
                                else if (stored_type->layout.size == 8)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                }
                                codegen_emit_u8(&buffer, stored_type->layout.size == 1 ? 0x86 : 0x87);
                                codegen_emit_u8(&buffer, indirect ? 0x02 : 0x85);
                                if (!indirect)
                                {
                                    codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[instruction->operands[0].value]));
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            if (indirect)
                            {
                                C_X64_LOAD(0x95, instruction->operands[0]);
                                if (!stored_type || !stored_type->layout.resolved ||
                                    (stored_type->layout.size != 1 && stored_type->layout.size != 2 && stored_type->layout.size != 4 &&
                                     stored_type->layout.size != 8))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                if (stored_type->layout.size == 2)
                                {
                                    codegen_emit_u8(&buffer, 0x66);
                                }
                                if (stored_type->layout.size == 8)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                }
                                codegen_emit_u8(&buffer, stored_type->layout.size == 1 ? 0x88 : 0x89);
                                codegen_emit_u8(&buffer, 0x02);
                                instruction_id = instruction->next;
                                continue;
                            }
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x89);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[instruction->operands[0].value]));
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        bool pointer_arithmetic = value_type && value_type->kind == IR_TYPE_POINTER &&
                                                  (instruction->atomic_operation == IR_ATOMIC_ADD || instruction->atomic_operation == IR_ATOMIC_SUBTRACT);
                        if (!value_type ||
                            (!pointer_arithmetic && value_type->kind != IR_TYPE_INTEGER &&
                             (instruction->atomic_operation != IR_ATOMIC_EXCHANGE ||
                              (value_type->kind != IR_TYPE_BOOLEAN && value_type->kind != IR_TYPE_POINTER))) ||
                            !value_type->layout.resolved ||
                            (value_type->layout.size != 1 && value_type->layout.size != 2 && value_type->layout.size != 4 && value_type->layout.size != 8) ||
                            instruction->atomic_operation >= IR_ATOMIC_OPERATION_COUNT)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        if (indirect)
                        {
                            C_X64_LOAD(0x95, instruction->operands[0]);
                        }
                        else
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8d);
                            codegen_emit_u8(&buffer, 0x95);
                            codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[instruction->operands[0].value]));
                        }
                        C_X64_LOAD(0x8d, instruction->operands[1]);
                        if (value_type->layout.size == 1 || value_type->layout.size == 2)
                        {
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, value_type->layout.size == 1 ? 0xb6 : 0xb7);
                        }
                        else
                        {
                            if (value_type->layout.size == 8)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0x8b);
                        }
                        codegen_emit_u8(&buffer, 0x02);
                        u32 retry_offset = (u32)buffer.count;
                        if (value_type->layout.size == 2)
                        {
                            codegen_emit_u8(&buffer, 0x66);
                        }
                        codegen_emit_u8(&buffer, value_type->layout.size == 8 ? 0x49 : 0x41);
                        codegen_emit_u8(&buffer, value_type->layout.size == 1 ? 0x88 : 0x89);
                        codegen_emit_u8(&buffer, 0xc0);
                        if (value_type->layout.size == 2)
                        {
                            codegen_emit_u8(&buffer, 0x66);
                        }
                        codegen_emit_u8(&buffer, value_type->layout.size == 8 ? 0x49 : 0x41);
                        if (instruction->atomic_operation == IR_ATOMIC_EXCHANGE)
                        {
                            codegen_emit_u8(&buffer, value_type->layout.size == 1 ? 0x88 : 0x89);
                            codegen_emit_u8(&buffer, 0xc8);
                        }
                        else
                        {
                            u8 operation_opcode = 0;
                            switch (instruction->atomic_operation)
                            {
                            case IR_ATOMIC_ADD:
                                operation_opcode = value_type->layout.size == 1 ? 0x00 : 0x01;
                                break;
                            case IR_ATOMIC_SUBTRACT:
                                operation_opcode = value_type->layout.size == 1 ? 0x28 : 0x29;
                                break;
                            case IR_ATOMIC_BITWISE_AND:
                                operation_opcode = value_type->layout.size == 1 ? 0x20 : 0x21;
                                break;
                            case IR_ATOMIC_BITWISE_OR:
                                operation_opcode = value_type->layout.size == 1 ? 0x08 : 0x09;
                                break;
                            case IR_ATOMIC_BITWISE_XOR:
                                operation_opcode = value_type->layout.size == 1 ? 0x30 : 0x31;
                                break;
                            case IR_ATOMIC_EXCHANGE:
                            case IR_ATOMIC_OPERATION_COUNT:
                                break;
                            }
                            codegen_emit_u8(&buffer, operation_opcode);
                            codegen_emit_u8(&buffer, 0xc8);
                        }
                        codegen_emit_u8(&buffer, 0xf0);
                        if (value_type->layout.size == 2)
                        {
                            codegen_emit_u8(&buffer, 0x66);
                        }
                        codegen_emit_u8(&buffer, value_type->layout.size == 8 ? 0x4c : 0x44);
                        codegen_emit_u8(&buffer, 0x0f);
                        codegen_emit_u8(&buffer, value_type->layout.size == 1 ? 0xb0 : 0xb1);
                        codegen_emit_u8(&buffer, 0x02);
                        codegen_emit_u8(&buffer, 0x0f);
                        codegen_emit_u8(&buffer, 0x85);
                        s64 retry_displacement = (s64)retry_offset - ((s64)buffer.count + 4);
                        if (retry_displacement < INT32_MIN || retry_displacement > INT32_MAX)
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_emit_u32(&buffer, (u32)(s32)retry_displacement);
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        if (!value_type || (value_type->kind != IR_TYPE_INTEGER && value_type->kind != IR_TYPE_POINTER) || !value_type->layout.resolved ||
                            (value_type->layout.size != 1 && value_type->layout.size != 2 && value_type->layout.size != 4 && value_type->layout.size != 8))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        if (indirect)
                        {
                            C_X64_LOAD(0x95, instruction->operands[0]);
                        }
                        else
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8d);
                            codegen_emit_u8(&buffer, 0x95);
                            codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[instruction->operands[0].value]));
                        }
                        C_X64_LOAD(0x85, instruction->operands[1]);
                        C_X64_LOAD(0x8d, instruction->operands[2]);
                        if (value_type->layout.size == 2)
                        {
                            codegen_emit_u8(&buffer, 0x66);
                        }
                        codegen_emit_u8(&buffer, 0xf0);
                        if (value_type->layout.size == 8)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                        }
                        codegen_emit_u8(&buffer, 0x0f);
                        codegen_emit_u8(&buffer, value_type->layout.size == 1 ? 0xb0 : 0xb1);
                        codegen_emit_u8(&buffer, 0x0a);
                        if (value_type->layout.size == 1 || value_type->layout.size == 2)
                        {
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, value_type->layout.size == 1 ? 0xb6 : 0xb7);
                            codegen_emit_u8(&buffer, 0xc0);
                        }
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_ATOMIC_FENCE)
                    {
                        if (!instruction->atomic_signal_fence && instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL)
                        {
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0xae);
                            codegen_emit_u8(&buffer, 0xf0);
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_CLEAR_INSTRUCTION_CACHE)
                    {
                    }
                    else if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER || instruction->opcode == IR_OPCODE_CONSTANT_FLOAT)
                    {
                        u64 immediate = instruction->immediates[0];
                        if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER && instruction->immediate_is_negative)
                        {
                            immediate = 0 - immediate;
                        }
                        if (codegen_canonical_register_is_64_bit(program, instruction->canonical_type))
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0xb8);
                            codegen_emit_u64(&buffer, immediate);
                        }
                        else
                        {
                            codegen_emit_u8(&buffer, 0xb8);
                            codegen_emit_u32(&buffer, (u32)immediate);
                        }
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_START)
                    {
                        if (!canonical_variadic)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u32 va_list_component_count = codegen_canonical_va_list_component_count(program, instruction->canonical_type);
                        if (!va_list_component_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u32 gp_count = 0;
                        u32 fp_count = 0;
                        u32 stack_parts = 0;
                        for (u32 parameter_index = 0; parameter_index < canonical_function_type->parameter_count; parameter_index += 1)
                        {
                            IrTypeId parameter_type_id = canonical_function_type->parameter_types[parameter_index];
                            IrType* parameter_type = ir_type_from_id(&program->types, parameter_type_id);
                            u32 parts = 1;
                            bool aggregate = codegen_canonical_integer_aggregate_parts(program, parameter_type_id, &parts);
                            if (parameter_type && parameter_type->kind == IR_TYPE_FLOAT)
                            {
                                if (fp_count < 8)
                                {
                                    fp_count += 1;
                                }
                                else
                                {
                                    stack_parts += 1;
                                }
                            }
                            else if (aggregate ? gp_count + parts <= 6 : gp_count < 6)
                            {
                                gp_count += parts;
                            }
                            else
                            {
                                stack_parts += parts;
                            }
                        }
                        if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V)
                        {
                            u64 offsets = (u64)(gp_count * 8) | ((u64)(48 + fp_count * 16) << 32);
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0xb8);
                            codegen_emit_u64(&buffer, offsets);
                            C_X64_STORE_RESULT();
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8d);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u32(&buffer, 16 + stack_parts * 8);
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x89);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u32(&buffer, (u32)(result_displacement + 8));
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8d);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u32(&buffer, (u32)codegen_canonical_x64_rebase_frame_displacement(&buffer, canonical_va_save_displacement,
                                                                                                              canonical_x64_frame_base_offset));
                        }
                        else
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8d);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u32(&buffer, (u32)codegen_canonical_x64_rebase_frame_displacement(
                                                             &buffer, 16 + (s64)(canonical_function_type->parameter_count + (windows_indirect_return ? 1 : 0)) * 8,
                                                             canonical_x64_frame_base_offset));
                        }
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x89);
                        codegen_emit_u8(&buffer, 0x85);
                        codegen_emit_u32(&buffer, (u32)(result_displacement + (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V ? 16 : 0)));
                        codegen_emit_u8(&buffer, 0x31);
                        codegen_emit_u8(&buffer, 0xc0);
                        u32 zero_start = result.abi == CODEGEN_ABI_X86_64_SYSTEM_V ? 24 : 8;
                        for (u32 offset = zero_start; offset < va_list_component_count * 8; offset += 8)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x89);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u32(&buffer, (u32)(result_displacement + (s32)offset));
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_COPY)
                    {
                        u32 va_list_component_count = codegen_canonical_va_list_component_count(program, instruction->canonical_type);
                        if (!va_list_component_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        C_X64_LOAD(0x85, instruction->operands[0]);
                        for (u32 component = 0; component < va_list_component_count; component += 1)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8b);
                            codegen_emit_u8(&buffer, component ? 0x50 : 0x10);
                            if (component)
                            {
                                codegen_emit_u8(&buffer, (u8)(component * 8));
                            }
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x89);
                            codegen_emit_u8(&buffer, 0x95);
                            codegen_emit_u32(&buffer, (u32)(result_displacement + (s32)(component * 8)));
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_END)
                    {
                        // va_end is a semantic lifetime marker.  The native
                        // representations used here do not require a
                        // destructive operation, and writing a fixed fourth
                        // word would exceed pointer-sized Windows va_list
                        // objects.
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_ARG)
                    {
                        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        u32 integer_parts = 0;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &integer_parts);
                        bool floating = value_type && value_type->kind == IR_TYPE_FLOAT;
                        CodegenCanonicalAbiValue aggregate_abi = codegen_canonical_aggregate_abi(program, instruction->canonical_type, result.abi, false, true);
                        if (!value_type || !value_type->layout.size || value_type->layout.size > 16 ||
                            (!aggregate && !floating && value_type->kind != IR_TYPE_INTEGER && value_type->kind != IR_TYPE_BOOLEAN &&
                             value_type->kind != IR_TYPE_POINTER))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        C_X64_LOAD(0x85, instruction->operands[0]);
                        bool split_system_v_aggregate =
                            result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && aggregate_abi.part_count && !aggregate_abi.memory && !aggregate_abi.indirect;
                        u32 split_integer_count = 0;
                        u32 split_float_count = 0;
                        if (split_system_v_aggregate)
                        {
                            for (u32 part = 0; part < aggregate_abi.part_count; part += 1)
                            {
                                if (codegen_canonical_abi_part_is_float(aggregate_abi.parts[part].abi_class))
                                {
                                    split_float_count += 1;
                                }
                                else
                                {
                                    split_integer_count += 1;
                                }
                            }
                        }
                        if (split_system_v_aggregate && split_float_count)
                        {
                            u32 overflow_patches[2] = {0};
                            u32 overflow_patch_count = 0;
                            if (split_integer_count)
                            {
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, 0x08);
                                codegen_emit_u8(&buffer, 0x81);
                                codegen_emit_u8(&buffer, 0xf9);
                                codegen_emit_u32(&buffer, 48 - split_integer_count * 8);
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x87);
                                overflow_patches[overflow_patch_count++] = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0);
                            }
                            codegen_emit_u8(&buffer, 0x44);
                            codegen_emit_u8(&buffer, 0x8b);
                            codegen_emit_u8(&buffer, 0x40);
                            codegen_emit_u8(&buffer, 4);
                            codegen_emit_u8(&buffer, 0x41);
                            codegen_emit_u8(&buffer, 0x81);
                            codegen_emit_u8(&buffer, 0xf8);
                            codegen_emit_u32(&buffer, 176 - split_float_count * 16);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0x87);
                            overflow_patches[overflow_patch_count++] = (u32)buffer.count;
                            codegen_emit_u32(&buffer, 0);
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8b);
                            codegen_emit_u8(&buffer, 0x50);
                            codegen_emit_u8(&buffer, 16);
                            u32 integer_part = 0;
                            u32 float_part = 0;
                            for (u32 part = 0; part < aggregate_abi.part_count; part += 1)
                            {
                                bool part_float = codegen_canonical_abi_part_is_float(aggregate_abi.parts[part].abi_class);
                                u32 part_offset = part_float ? float_part++ * 16 : integer_part++ * 8;
                                codegen_emit_u8(&buffer, part_float ? 0x4e : 0x4c);
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, part_offset ? 0x4c : 0x0c);
                                codegen_emit_u8(&buffer, part_float ? 0x02 : 0x0a);
                                if (part_offset)
                                {
                                    codegen_emit_u8(&buffer, (u8)part_offset);
                                }
                                codegen_emit_u8(&buffer, 0x4c);
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0x8d);
                                codegen_emit_u32(&buffer, (u32)(result_displacement + (s32)aggregate_abi.parts[part].value_offset));
                            }
                            if (split_integer_count)
                            {
                                codegen_emit_u8(&buffer, 0x83);
                                codegen_emit_u8(&buffer, 0x00);
                                codegen_emit_u8(&buffer, (u8)(split_integer_count * 8));
                            }
                            codegen_emit_u8(&buffer, 0x83);
                            codegen_emit_u8(&buffer, 0x40);
                            codegen_emit_u8(&buffer, 4);
                            codegen_emit_u8(&buffer, (u8)(split_float_count * 16));
                            codegen_emit_u8(&buffer, 0xe9);
                            u32 end_patch = (u32)buffer.count;
                            codegen_emit_u32(&buffer, 0);
                            u32 overflow_offset = (u32)buffer.count;
                            for (u32 patch = 0; patch < overflow_patch_count; patch += 1)
                            {
                                s32 delta = (s32)(overflow_offset - (overflow_patches[patch] + 4));
                                memcpy(buffer.bytes + overflow_patches[patch], &delta, sizeof(delta));
                            }
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8b);
                            codegen_emit_u8(&buffer, 0x50);
                            codegen_emit_u8(&buffer, 8);
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x81);
                            codegen_emit_u8(&buffer, 0x40);
                            codegen_emit_u8(&buffer, 8);
                            codegen_emit_u32(&buffer, (u32)((value_type->layout.size + 7) & ~(u64)7));
                            for (u32 part = 0; part < aggregate_abi.part_count; part += 1)
                            {
                                codegen_emit_u8(&buffer, 0x4c);
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, aggregate_abi.parts[part].value_offset ? 0x4a : 0x0a);
                                if (aggregate_abi.parts[part].value_offset)
                                {
                                    codegen_emit_u8(&buffer, (u8)aggregate_abi.parts[part].value_offset);
                                }
                                codegen_emit_u8(&buffer, 0x4c);
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0x8d);
                                codegen_emit_u32(&buffer, (u32)(result_displacement + (s32)aggregate_abi.parts[part].value_offset));
                            }
                            u32 end_offset = (u32)buffer.count;
                            s32 end_delta = (s32)(end_offset - (end_patch + 4));
                            memcpy(buffer.bytes + end_patch, &end_delta, sizeof(end_delta));
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (result.abi == CODEGEN_ABI_X86_64_WINDOWS)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8b);
                            codegen_emit_u8(&buffer, 0x10);
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x83);
                            codegen_emit_u8(&buffer, 0x00);
                            codegen_emit_u8(&buffer, 8);
                            if (aggregate_abi.indirect)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, 0x12);
                            }
                        }
                        else
                        {
                            u32 descriptor_offset = floating ? 4 : 0;
                            u32 part_count = aggregate ? integer_parts : 1;
                            u32 increment = floating ? 16 : part_count * 8;
                            u32 limit = floating ? 176 - increment : 48 - increment;
                            codegen_emit_u8(&buffer, 0x8b);
                            codegen_emit_u8(&buffer, descriptor_offset ? 0x48 : 0x08);
                            if (descriptor_offset)
                            {
                                codegen_emit_u8(&buffer, (u8)descriptor_offset);
                            }
                            codegen_emit_u8(&buffer, 0x81);
                            codegen_emit_u8(&buffer, 0xf9);
                            codegen_emit_u32(&buffer, limit);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0x87);
                            u32 overflow_patch = (u32)buffer.count;
                            codegen_emit_u32(&buffer, 0);
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8b);
                            codegen_emit_u8(&buffer, 0x50);
                            codegen_emit_u8(&buffer, 16);
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x01);
                            codegen_emit_u8(&buffer, 0xca);
                            codegen_emit_u8(&buffer, 0x83);
                            codegen_emit_u8(&buffer, descriptor_offset ? 0x40 : 0x00);
                            if (descriptor_offset)
                            {
                                codegen_emit_u8(&buffer, (u8)descriptor_offset);
                            }
                            codegen_emit_u8(&buffer, (u8)increment);
                            codegen_emit_u8(&buffer, 0xe9);
                            u32 end_patch = (u32)buffer.count;
                            codegen_emit_u32(&buffer, 0);
                            u32 overflow_offset = (u32)buffer.count;
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8b);
                            codegen_emit_u8(&buffer, 0x50);
                            codegen_emit_u8(&buffer, 8);
                            // The caller placed an over-aligned argument at its
                            // own alignment, so the overflow cursor has to skip
                            // the same padding before reading one back. Only
                            // sixteen is reachable: a wider type is refused
                            // above for being larger than two eightbytes.
                            if (codegen_canonical_x64_stack_argument_alignment(value_type) > 8)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x83);
                                codegen_emit_u8(&buffer, 0xc2);
                                codegen_emit_u8(&buffer, 15);
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x83);
                                codegen_emit_u8(&buffer, 0xe2);
                                codegen_emit_u8(&buffer, 0xf0);
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0x50);
                                codegen_emit_u8(&buffer, 8);
                            }
                            u32 stack_size = (u32)((value_type->layout.size + 7) & ~(u64)7);
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x81);
                            codegen_emit_u8(&buffer, 0x40);
                            codegen_emit_u8(&buffer, 8);
                            codegen_emit_u32(&buffer, stack_size);
                            u32 copy_offset = (u32)buffer.count;
                            s32 overflow_delta = (s32)(overflow_offset - (overflow_patch + 4));
                            memcpy(buffer.bytes + overflow_patch, &overflow_delta, sizeof(overflow_delta));
                            for (u32 part = 0; part < (aggregate ? integer_parts : 1); part += 1)
                            {
                                codegen_emit_u8(&buffer, 0x4c);
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, part ? 0x42 : 0x02);
                                if (part)
                                {
                                    codegen_emit_u8(&buffer, (u8)(part * 8));
                                }
                                codegen_emit_u8(&buffer, 0x4c);
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)(result_displacement + (s32)(part * 8)));
                            }
                            s32 end_delta = (s32)(copy_offset - (end_patch + 4));
                            memcpy(buffer.bytes + end_patch, &end_delta, sizeof(end_delta));
                            instruction_id = instruction->next;
                            continue;
                        }
                        for (u32 part = 0; part < (aggregate ? integer_parts : 1); part += 1)
                        {
                            codegen_emit_u8(&buffer, 0x4c);
                            codegen_emit_u8(&buffer, 0x8b);
                            codegen_emit_u8(&buffer, part ? 0x42 : 0x02);
                            if (part)
                            {
                                codegen_emit_u8(&buffer, (u8)(part * 8));
                            }
                            codegen_emit_u8(&buffer, 0x4c);
                            codegen_emit_u8(&buffer, 0x89);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u32(&buffer, (u32)(result_displacement + (s32)(part * 8)));
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_CALL)
                    {
                        CodegenCanonicalCallLayout call_layout = {0};
                        u32 call_instruction_index = (u32)(instruction - function->instructions);
                        if (call_layout_cache && call_layout_cache[call_instruction_index])
                        {
                            call_layout = *call_layout_cache[call_instruction_index];
                        }
                        else
                        {
                            CodegenError call_error =
                                codegen_canonical_x64_call_layout(arena, program, function, instruction, result.abi, target, &call_layout);
                            if (call_error != CODEGEN_ERROR_NONE)
                            {
                                result.error = call_error;
                                return result;
                            }
                        }
                        static u8 const system_v[] = {
                            7, 6, 2, 1, 8, 9,
                        };
                        static u8 const windows[] = {
                            1,
                            2,
                            8,
                            9,
                        };
                        u8 const* registers = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? windows : system_v;
                        u32 register_count = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? BUSTER_ARRAY_LENGTH(windows) : BUSTER_ARRAY_LENGTH(system_v);
                        u32 argument_count = call_layout.argument_count;
                        CodegenCanonicalCallArgument* arguments = call_layout.arguments;
                        CodegenCanonicalAbiValue call_return_abi = call_layout.return_abi;
                        bool call_windows_indirect_return = call_layout.windows_indirect_return;
                        bool call_x64_indirect_return = call_layout.indirect_return;
                        u32 simulated_float_registers = call_layout.simulated_float_registers;
                        // An argument wanting more than the sixteen bytes the
                        // stack pointer is already worth cannot be reached by
                        // pushing: where the pushes leave it depends on where
                        // the stack happened to be. Such a call moves the stack
                        // pointer down to the alignment the area needs instead,
                        // writes each argument at its own offset within it, and
                        // puts the stack pointer back from a frame slot after,
                        // which is the one restore an `and` cannot undo and a
                        // dynamically grown stack does not invalidate.
                        bool system_v_aligned_area = result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && call_layout.stack_part_count &&
                                                     call_layout.stack_alignment > CODEGEN_X64_STACK_ALIGNMENT;
                        if (system_v_aligned_area && !x64_stack_save_offset)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                            return result;
                        }
                        bool stack_padding = call_layout.stack_padding && !system_v_aligned_area;
                        if (stack_padding)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x83);
                            codegen_emit_u8(&buffer, 0xec);
                            codegen_emit_u8(&buffer, 8);
                        }
                        if (system_v_aligned_area)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x89);
                            codegen_emit_u8(&buffer, 0xa5);
                            codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(x64_stack_save_offset));
                            codegen_canonical_x64_adjust_stack(&buffer, call_layout.stack_part_count * 8, true);
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x81);
                            codegen_emit_u8(&buffer, 0xe4);
                            codegen_emit_u32(&buffer, 0 - call_layout.stack_alignment);
                            for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
                            {
                                CodegenCanonicalCallArgument* call_argument = arguments + argument_index;
                                if (!call_argument->on_stack)
                                {
                                    continue;
                                }
                                u32 argument_offset = value_offsets[instruction->operands[argument_index + 1].value];
                                // No rounding here: a System V stack argument
                                // is already at an offset respecting its own
                                // alignment inside an area the call aligned to
                                // the widest of them, so the slot address is
                                // whatever the stack pointer already is.
                                if (!codegen_canonical_x64_copy_frame_to_rsp(&buffer, argument_offset, canonical_x64_frame_base_offset,
                                                                             call_argument->stack_offset, call_argument->stack_part_count * 8,
                                                                             CODEGEN_X64_STACK_ALIGNMENT))
                                {
                                    result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                        }
                        bool windows_dynamic_call = windows_dynamic_stack && result.abi == CODEGEN_ABI_X86_64_WINDOWS;
                        if (windows_dynamic_call)
                        {
                            codegen_canonical_x64_adjust_stack(&buffer, call_layout.windows_stack_size, true);
                            if (buffer.error != CODEGEN_ERROR_NONE)
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        if (result.abi == CODEGEN_ABI_X86_64_WINDOWS)
                        {
                            for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
                            {
                                CodegenCanonicalCallArgument* call_argument = arguments + argument_index;
                                if (call_argument->windows_indirect &&
                                    (value_offsets[instruction->operands[argument_index + 1].value] > UINT32_MAX - call_argument->copy_size ||
                                     !codegen_canonical_x64_copy_frame_to_rsp(&buffer, value_offsets[instruction->operands[argument_index + 1].value],
                                                                               canonical_x64_frame_base_offset, call_argument->copy_offset,
                                                                               call_argument->copy_size, call_argument->copy_alignment)))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                        }
                        // The pushes build the area downward, so an argument is
                        // preceded by whatever padding sits above it: emit that
                        // first and the one below it lands on its own offset.
                        u32 stack_cursor = system_v_aligned_area ? 0 : call_layout.stack_part_count * 8;
                        for (u32 argument_reverse_index = argument_count; argument_reverse_index > 0 && !system_v_aligned_area; argument_reverse_index -= 1)
                        {
                            u32 array_index = argument_reverse_index - 1;
                            if (!arguments[array_index].on_stack || result.abi != CODEGEN_ABI_X86_64_SYSTEM_V)
                            {
                                continue;
                            }
                            IrValueId argument = instruction->operands[argument_reverse_index];
                            for (u32 padding = arguments[array_index].stack_offset + arguments[array_index].stack_part_count * 8; padding < stack_cursor;
                                 padding += 8)
                            {
                                codegen_emit_u8(&buffer, 0x50);
                            }
                            for (u32 part_index = arguments[array_index].stack_part_count; part_index > 0; part_index -= 1)
                            {
                                codegen_emit_u8(&buffer, 0xff);
                                codegen_emit_u8(&buffer, 0xb5);
                                codegen_emit_u32(&buffer, (u32)(C_X64_FRAME_DISPLACEMENT(value_offsets[argument.value]) + (s32)((part_index - 1) * 8)));
                            }
                            stack_cursor = arguments[array_index].stack_offset;
                        }
                        if (result.abi == CODEGEN_ABI_X86_64_WINDOWS)
                        {
                            u32 stack_index = 0;
                            for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
                            {
                                if (!arguments[argument_index].on_stack)
                                {
                                    continue;
                                }
                                IrValueId argument = instruction->operands[argument_index + 1];
                                if (arguments[argument_index].windows_indirect)
                                {
                                    if (!codegen_canonical_x64_rsp_address(&buffer, X64_REGISTER_RAX, arguments[argument_index].copy_offset,
                                                                            arguments[argument_index].copy_alignment))
                                    {
                                        result.error = CODEGEN_ERROR_CAPACITY;
                                        return result;
                                    }
                                }
                                else
                                {
                                    C_X64_LOAD(0x85, argument);
                                }
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0x84);
                                codegen_emit_u8(&buffer, 0x24);
                                codegen_emit_u32(&buffer, 32 + stack_index * 8);
                                stack_index += arguments[argument_index].part_count;
                            }
                        }
                        if (call_x64_indirect_return)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8d);
                            codegen_emit_u8(&buffer, call_windows_indirect_return ? 0x8d : 0xbd);
                            codegen_emit_u32(&buffer, (u32)result_displacement);
                        }
                        IrType* call_callee_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
                        if (call_callee_type && call_callee_type->kind == IR_TYPE_POINTER)
                        {
                            call_callee_type = ir_type_from_id(&program->types, call_callee_type->element_type);
                        }
                        bool windows_variadic_call = result.abi == CODEGEN_ABI_X86_64_WINDOWS && call_callee_type &&
                                                     call_callee_type->kind == IR_TYPE_FUNCTION && call_callee_type->is_variadic;
                        u32 register_index = call_x64_indirect_return ? 1 : 0;
                        for (u32 argument_index = 1; argument_index < instruction->operand_count; argument_index += 1)
                        {
                            IrValueId argument = instruction->operands[argument_index];
                            CodegenCanonicalCallArgument* call_argument = arguments + argument_index - 1;
                            IrType* argument_type = call_argument->type;
                            CodegenCanonicalAbiValue argument_abi = call_argument->abi;
                            u32 register_parts = call_argument->windows_indirect ? 1 : call_argument->part_count;
                            if (call_argument->on_stack)
                            {
                                continue;
                            }
                            if (!argument_type || (!call_argument->system_v_aggregate &&
                                                   (register_index > register_count || register_parts > register_count - register_index)))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && argument_type->kind == IR_TYPE_FLOAT)
                            {
                                u8 float_register = call_argument->float_register;
                                if (float_register >= 8 || (argument_type->bit_width != 32 && argument_type->bit_width != 64))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                codegen_emit_u8(&buffer, argument_type->bit_width == 32 ? 0xf3 : 0xf2);
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x10);
                                codegen_emit_u8(&buffer, (u8)(0x85 | (float_register << 3)));
                                codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[argument.value]));
                                continue;
                            }
                            if (call_argument->system_v_aggregate)
                            {
                                u32 float_register = call_argument->float_register;
                                for (u32 part_index = 0; part_index < argument_abi.part_count; part_index += 1)
                                {
                                    CodegenCanonicalAbiPart* part = argument_abi.parts + part_index;
                                    s32 displacement = C_X64_FRAME_DISPLACEMENT(value_offsets[argument.value]) + (s32)part->value_offset;
                                    if (codegen_canonical_abi_part_is_float(part->abi_class))
                                    {
                                        if (!codegen_canonical_x64_float_memory(&buffer, target, float_register, displacement, part->size, false))
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                            return result;
                                        }
                                        float_register += 1;
                                    }
                                    else
                                    {
                                        if (register_index >= register_count)
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                            return result;
                                        }
                                        u8 reg = registers[register_index++];
                                        codegen_emit_u8(&buffer, reg >= 8 ? 0x4c : 0x48);
                                        codegen_emit_u8(&buffer, 0x8b);
                                        codegen_emit_u8(&buffer, (u8)(0x85 | ((reg & 7) << 3)));
                                        codegen_emit_u32(&buffer, (u32)displacement);
                                    }
                                }
                                continue;
                            }
                            if (result.abi == CODEGEN_ABI_X86_64_WINDOWS && argument_type->kind == IR_TYPE_FLOAT)
                            {
                                if (register_index >= register_count || (argument_type->bit_width != 32 && argument_type->bit_width != 64))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                codegen_emit_u8(&buffer, argument_type->bit_width == 32 ? 0xf3 : 0xf2);
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x10);
                                codegen_emit_u8(&buffer, (u8)(0x85 | (register_index << 3)));
                                codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[argument.value]));
                                if (windows_variadic_call)
                                {
                                    u8 reg = registers[register_index];
                                    codegen_emit_u8(&buffer, reg >= 8 ? 0x4c : 0x48);
                                    codegen_emit_u8(&buffer, 0x8b);
                                    codegen_emit_u8(&buffer, (u8)(0x85 | ((reg & 7) << 3)));
                                    codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[argument.value]));
                                }
                                register_index += 1;
                                continue;
                            }
                            if (call_argument->windows_indirect)
                            {
                                u8 reg = registers[register_index++];
                                if (!codegen_canonical_x64_rsp_address(&buffer, reg, call_argument->copy_offset, call_argument->copy_alignment))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                continue;
                            }
                            for (u32 part_index = 0; part_index < call_argument->part_count; part_index += 1)
                            {
                                u8 reg = registers[register_index++];
                                if (reg >= 8)
                                {
                                    codegen_emit_u8(&buffer, 0x4c);
                                }
                                else
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                }
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, (u8)(0x85 | ((reg & 7) << 3)));
                                codegen_emit_u32(&buffer, (u32)(C_X64_FRAME_DISPLACEMENT(value_offsets[argument.value]) + (s32)(part_index * 8)));
                            }
                        }
                        IrType* callee_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
                        bool indirect_call = callee_type && callee_type->kind == IR_TYPE_POINTER;
                        if (indirect_call)
                        {
                            callee_type = ir_type_from_id(&program->types, callee_type->element_type);
                        }
                        if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && callee_type && callee_type->kind == IR_TYPE_FUNCTION && callee_type->is_variadic)
                        {
                            codegen_emit_u8(&buffer, 0xb8);
                            codegen_emit_u32(&buffer, simulated_float_registers);
                        }
                        if (indirect_call)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8b);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[instruction->operands[0].value]));
                            codegen_emit_u8(&buffer, 0xff);
                            codegen_emit_u8(&buffer, 0xd0);
                        }
                        else
                        {
                            codegen_emit_u8(&buffer, 0xe8);
                            u32 offset = (u32)buffer.count;
                            codegen_emit_u32(&buffer, 0);
                            result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                .entity = ANALYSIS_ENTITY_ID_INVALID,
                                .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
                                .symbol = instruction->symbol,
                                .offset = offset,
                            };
                        }
                        if (windows_dynamic_call)
                        {
                            codegen_canonical_x64_adjust_stack(&buffer, call_layout.windows_stack_size, false);
                            if (buffer.error != CODEGEN_ERROR_NONE)
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        if (system_v_aligned_area)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x8b);
                            codegen_emit_u8(&buffer, 0xa5);
                            codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(x64_stack_save_offset));
                        }
                        else if (result.abi != CODEGEN_ABI_X86_64_WINDOWS && (call_layout.stack_part_count || stack_padding))
                        {
                            u32 cleanup = call_layout.stack_part_count * 8 + (stack_padding ? 8 : 0);
                            codegen_canonical_x64_adjust_stack(&buffer, cleanup, false);
                        }
                        if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
                        {
                            IrType* return_type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (return_type && return_type->kind == IR_TYPE_FLOAT)
                            {
                                if (return_type->bit_width != 32 && return_type->bit_width != 64)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                codegen_emit_u8(&buffer, return_type->bit_width == 32 ? 0xf3 : 0xf2);
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x11);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)result_displacement);
                                instruction_id = instruction->next;
                                continue;
                            }
                            if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && call_return_abi.part_count && !call_return_abi.indirect && !call_return_abi.memory)
                            {
                                u32 integer_index = 0;
                                u32 float_index = 0;
                                for (u32 part_index = 0; part_index < call_return_abi.part_count; part_index += 1)
                                {
                                    CodegenCanonicalAbiPart* part = call_return_abi.parts + part_index;
                                    if (codegen_canonical_abi_part_is_float(part->abi_class))
                                    {
                                        u32 register_size = 0;
                                        u32 register_count_used = codegen_canonical_x64_vector_part_registers(&target, part->size, &register_size);
                                        if (!register_count_used || float_index + register_count_used > BUSTER_MAX((u32)2, register_count_used))
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                            return result;
                                        }
                                        for (u32 chunk = 0; chunk < register_count_used; chunk += 1)
                                        {
                                            if (!codegen_canonical_x64_float_memory(&buffer, target, float_index,
                                                                                    result_displacement + (s32)part->value_offset + (s32)(chunk * register_size),
                                                                                    register_size, true))
                                            {
                                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                                return result;
                                            }
                                            float_index += 1;
                                        }
                                    }
                                    else
                                    {
                                        if (integer_index >= 2)
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                            return result;
                                        }
                                        codegen_emit_u8(&buffer, 0x48);
                                        codegen_emit_u8(&buffer, 0x89);
                                        codegen_emit_u8(&buffer, integer_index ? 0x95 : 0x85);
                                        codegen_emit_u32(&buffer, (u32)(result_displacement + (s32)part->value_offset));
                                        integer_index += 1;
                                    }
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            u32 return_parts = 0;
                            bool aggregate_return = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &return_parts);
                            CodegenCanonicalAbiValue aggregate_return_abi =
                                codegen_canonical_aggregate_abi(program, instruction->canonical_type, result.abi, true, false);
                            if (aggregate_return_abi.part_count && !aggregate_return_abi.indirect)
                            {
                                aggregate_return = true;
                                return_parts = aggregate_return_abi.part_count;
                            }
                            if (aggregate_return_abi.indirect)
                            {
                                if (!call_x64_indirect_return)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            C_X64_STORE_RESULT();
                            if (aggregate_return && return_parts == 2)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0x95);
                                codegen_emit_u32(&buffer, (u32)(result_displacement + 8));
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_ARRAY)
                    {
                        IrType* array = ir_type_from_id(&program->types, instruction->canonical_type);
                        IrType* element = array ? ir_type_from_id(&program->types, array->element_type) : 0;
                        if (!array || !element || (array->kind != IR_TYPE_ARRAY && array->kind != IR_TYPE_VECTOR) ||
                            instruction->operand_count != array->element_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        for (u32 element_index = 0; element_index < instruction->operand_count; element_index += 1)
                        {
                            u64 copied = 0;
                            while (copied < element->layout.size)
                            {
                                u64 remaining = element->layout.size - copied;
                                u32 chunk = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
                                if (chunk == 8)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                }
                                else if (chunk == 2)
                                {
                                    codegen_emit_u8(&buffer, 0x66);
                                }
                                codegen_emit_u8(&buffer, chunk == 1 ? 0x8a : 0x8b);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)(C_X64_FRAME_DISPLACEMENT(value_offsets[instruction->operands[element_index].value]) + (s32)copied));
                                if (chunk == 8)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                }
                                else if (chunk == 2)
                                {
                                    codegen_emit_u8(&buffer, 0x66);
                                }
                                codegen_emit_u8(&buffer, chunk == 1 ? 0x88 : 0x89);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)(result_displacement + (s32)(element_index * element->layout.size + copied)));
                                copied += chunk;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_AGGREGATE)
                    {
                        IrType* aggregate = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (!aggregate || instruction->operand_count != instruction->immediate_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u64 aggregate_copied = 0;
                        while (aggregate_copied < aggregate->layout.size)
                        {
                            u64 remaining = aggregate->layout.size - aggregate_copied;
                            u32 chunk = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
                            codegen_emit_u8(&buffer, 0x31);
                            codegen_emit_u8(&buffer, 0xc0);
                            if (chunk == 8)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            else if (chunk == 2)
                            {
                                codegen_emit_u8(&buffer, 0x66);
                            }
                            codegen_emit_u8(&buffer, chunk == 1 ? 0x88 : 0x89);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u32(&buffer, (u32)(result_displacement + (s32)aggregate_copied));
                            aggregate_copied += chunk;
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 field_index = instruction->immediates[operand_index];
                            if (field_index >= aggregate->field_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrField* field = aggregate->fields + field_index;
                            IrType* field_type = ir_type_from_id(&program->types, field->type);
                            if (!field_type || !field_type->layout.resolved)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            s32 field_displacement = result_displacement + (s32)field->offset;
                            if (field->is_bit_field)
                            {
                                if (!field->bit_width)
                                {
                                    continue;
                                }
                                if (field_type->layout.size != 1 && field_type->layout.size != 2 && field_type->layout.size != 4 &&
                                    field_type->layout.size != 8)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                C_X64_LOAD(0x85, instruction->operands[operand_index]);
                                if (field->bit_width < 64)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                    codegen_emit_u8(&buffer, 0xba);
                                    codegen_emit_u64(&buffer, ((u64)1 << field->bit_width) - 1);
                                    codegen_emit_u8(&buffer, 0x48);
                                    codegen_emit_u8(&buffer, 0x21);
                                    codegen_emit_u8(&buffer, 0xd0);
                                }
                                if (field->bit_offset)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                    codegen_emit_u8(&buffer, 0xc1);
                                    codegen_emit_u8(&buffer, 0xe0);
                                    codegen_emit_u8(&buffer, (u8)field->bit_offset);
                                }
                                if (field_type->layout.size == 8)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                }
                                else if (field_type->layout.size == 2)
                                {
                                    codegen_emit_u8(&buffer, 0x66);
                                }
                                codegen_emit_u8(&buffer, field_type->layout.size == 1 ? 0x08 : 0x09);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)field_displacement);
                                continue;
                            }
                            u64 field_copied = 0;
                            while (field_copied < field_type->layout.size)
                            {
                                u64 remaining = field_type->layout.size - field_copied;
                                u32 chunk = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
                                if (chunk == 8)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                }
                                else if (chunk == 2)
                                {
                                    codegen_emit_u8(&buffer, 0x66);
                                }
                                codegen_emit_u8(&buffer, chunk == 1 ? 0x8a : 0x8b);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)(C_X64_FRAME_DISPLACEMENT(value_offsets[instruction->operands[operand_index].value]) + (s32)field_copied));
                                if (chunk == 8)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                }
                                else if (chunk == 2)
                                {
                                    codegen_emit_u8(&buffer, 0x66);
                                }
                                codegen_emit_u8(&buffer, chunk == 1 ? 0x88 : 0x89);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)(field_displacement + (s32)field_copied));
                                field_copied += chunk;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_UNARY)
                    {
                        IrType* canonical_unary_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (canonical_unary_type && canonical_unary_type->kind == IR_TYPE_VECTOR)
                        {
                            if (!codegen_canonical_x64_vector_operation(
                                    &buffer, program, function, instruction, value_offsets, canonical_x64_frame_base_offset, target,
                                    &result.statistics.native_vector_operation_count, &result.statistics.split_vector_operation_count,
                                    &x64_upper_vector_dirty, &x64_last_wide_vector_result, &x64_last_wide_vector_size,
                                    &result.statistics.forwarded_wide_vector_load_count))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        C_X64_LOAD(0x85, instruction->operands[0]);
                        if (instruction->unary_operation == IR_UNARY_BOOLEAN_NOT)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x85);
                            codegen_emit_u8(&buffer, 0xc0);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0x94);
                            codegen_emit_u8(&buffer, 0xc0);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0xb6);
                            codegen_emit_u8(&buffer, 0xc0);
                        }
                        else if (instruction->unary_operation == IR_UNARY_FLOAT_NEGATE)
                        {
                            IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (!type || type->kind != IR_TYPE_FLOAT || (type->bit_width != 32 && type->bit_width != 64))
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            u64 sign = type->bit_width == 32 ? (u64)1 << 31 : (u64)1 << 63;
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0xb9);
                            codegen_emit_u64(&buffer, sign);
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x31);
                            codegen_emit_u8(&buffer, 0xc8);
                        }
                        else if (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE || instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT)
                        {
                            IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (!type || type->kind != IR_TYPE_INTEGER)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            if (type->bit_width > 32)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0xf7);
                            codegen_emit_u8(&buffer, instruction->unary_operation == IR_UNARY_INTEGER_NEGATE ? 0xd8 : 0xd0);
                        }
                        else if (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS ||
                                 instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS)
                        {
                            IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (!type || type->kind != IR_TYPE_INTEGER)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            if (type->bit_width > 32)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS ? 0xbc : 0xbd);
                            codegen_emit_u8(&buffer, 0xc0);
                            if (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS)
                            {
                                if (type->bit_width > 32)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                }
                                codegen_emit_u8(&buffer, 0x83);
                                codegen_emit_u8(&buffer, 0xf0);
                                codegen_emit_u8(&buffer, (u8)(type->bit_width - 1));
                            }
                        }
                        else if (instruction->unary_operation == IR_UNARY_INTEGER_POPULATION_COUNT)
                        {
                            IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (!type || type->kind != IR_TYPE_INTEGER)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            x64_emit_population_count(&buffer, type->bit_width);
                        }
                        else
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_BINARY)
                    {
                        IrTypeId operand_type = function->values[instruction->operands[0].value].canonical_type;
                        IrType* operand_type_value = ir_type_from_id(&program->types, operand_type);
                        if (operand_type_value && operand_type_value->kind == IR_TYPE_VECTOR)
                        {
                            if (!codegen_canonical_x64_vector_operation(
                                    &buffer, program, function, instruction, value_offsets, canonical_x64_frame_base_offset, target,
                                    &result.statistics.native_vector_operation_count, &result.statistics.split_vector_operation_count,
                                    &x64_upper_vector_dirty, &x64_last_wide_vector_result, &x64_last_wide_vector_size,
                                    &result.statistics.forwarded_wide_vector_load_count))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (operand_type_value && operand_type_value->kind == IR_TYPE_FLOAT)
                        {
                            u32 width = operand_type_value->bit_width;
                            if (width != 32 && width != 64)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            C_X64_LOAD_FLOAT(0, instruction->operands[0], width);
                            C_X64_LOAD_FLOAT(1, instruction->operands[1], width);
                            IrBinaryOperation operation = instruction->binary_operation;
                            if (operation >= IR_BINARY_FLOAT_ADD && operation <= IR_BINARY_FLOAT_DIVIDE)
                            {
                                codegen_emit_u8(&buffer, width == 32 ? 0xf3 : 0xf2);
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, operation == IR_BINARY_FLOAT_ADD        ? 0x58
                                                         : operation == IR_BINARY_FLOAT_SUBTRACT ? 0x5c
                                                         : operation == IR_BINARY_FLOAT_MULTIPLY ? 0x59
                                                                                                 : 0x5e);
                                codegen_emit_u8(&buffer, 0xc1);
                                codegen_emit_u8(&buffer, width == 32 ? 0xf3 : 0xf2);
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x11);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)result_displacement);
                                C_X64_RECORD_FLOAT_STORE(width);
                            }
                            else
                            {
                                if (width == 64)
                                {
                                    codegen_emit_u8(&buffer, 0x66);
                                }
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x2e);
                                codegen_emit_u8(&buffer, 0xc1);
                                u8 condition = operation == IR_BINARY_FLOAT_EQUAL           ? 0x94
                                               : operation == IR_BINARY_FLOAT_NOT_EQUAL     ? 0x95
                                               : operation == IR_BINARY_FLOAT_LESS          ? 0x92
                                               : operation == IR_BINARY_FLOAT_LESS_EQUAL    ? 0x96
                                               : operation == IR_BINARY_FLOAT_GREATER       ? 0x97
                                               : operation == IR_BINARY_FLOAT_GREATER_EQUAL ? 0x93
                                                                                            : 0;
                                if (!condition)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, condition);
                                codegen_emit_u8(&buffer, 0xc0);
                                if (operation == IR_BINARY_FLOAT_EQUAL || operation == IR_BINARY_FLOAT_LESS || operation == IR_BINARY_FLOAT_LESS_EQUAL)
                                {
                                    codegen_emit_u8(&buffer, 0x0f);
                                    codegen_emit_u8(&buffer, 0x9b);
                                    codegen_emit_u8(&buffer, 0xc2);
                                    codegen_emit_u8(&buffer, 0x20);
                                    codegen_emit_u8(&buffer, 0xd0);
                                }
                                else if (operation == IR_BINARY_FLOAT_NOT_EQUAL)
                                {
                                    codegen_emit_u8(&buffer, 0x0f);
                                    codegen_emit_u8(&buffer, 0x9a);
                                    codegen_emit_u8(&buffer, 0xc2);
                                    codegen_emit_u8(&buffer, 0x08);
                                    codegen_emit_u8(&buffer, 0xd0);
                                }
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0xb6);
                                codegen_emit_u8(&buffer, 0xc0);
                                C_X64_STORE_RESULT();
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        bool wide = codegen_canonical_register_is_64_bit(program, operand_type);
                        C_X64_LOAD(0x85, instruction->operands[0]);
                        C_X64_LOAD(0x8d, instruction->operands[1]);
                        switch (instruction->binary_operation)
                        {
                        case IR_BINARY_INTEGER_ADD:
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0x01);
                            codegen_emit_u8(&buffer, 0xc8);
                            break;
                        case IR_BINARY_INTEGER_SUBTRACT:
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0x29);
                            codegen_emit_u8(&buffer, 0xc8);
                            break;
                        case IR_BINARY_INTEGER_MULTIPLY:
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0xaf);
                            codegen_emit_u8(&buffer, 0xc1);
                            break;
                        case IR_BINARY_SIGNED_DIVIDE:
                        case IR_BINARY_SIGNED_REMAINDER:
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0x99);
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0xf7);
                            codegen_emit_u8(&buffer, 0xf9);
                            if (instruction->binary_operation == IR_BINARY_SIGNED_REMAINDER)
                            {
                                if (wide)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                }
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0xd0);
                            }
                            break;
                        case IR_BINARY_UNSIGNED_DIVIDE:
                        case IR_BINARY_UNSIGNED_REMAINDER:
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0x31);
                            codegen_emit_u8(&buffer, 0xd2);
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0xf7);
                            codegen_emit_u8(&buffer, 0xf1);
                            if (instruction->binary_operation == IR_BINARY_UNSIGNED_REMAINDER)
                            {
                                if (wide)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                }
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0xd0);
                            }
                            break;
                        case IR_BINARY_SHIFT_LEFT:
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0xd3);
                            codegen_emit_u8(&buffer, 0xe0);
                            break;
                        case IR_BINARY_SIGNED_SHIFT_RIGHT:
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0xd3);
                            codegen_emit_u8(&buffer, 0xf8);
                            break;
                        case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0xd3);
                            codegen_emit_u8(&buffer, 0xe8);
                            break;
                        case IR_BINARY_INTEGER_BITWISE_AND:
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0x21);
                            codegen_emit_u8(&buffer, 0xc8);
                            break;
                        case IR_BINARY_INTEGER_BITWISE_OR:
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0x09);
                            codegen_emit_u8(&buffer, 0xc8);
                            break;
                        case IR_BINARY_INTEGER_BITWISE_XOR:
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0x31);
                            codegen_emit_u8(&buffer, 0xc8);
                            break;
                        case IR_BINARY_INTEGER_EQUAL:
                        case IR_BINARY_POINTER_EQUAL:
                        case IR_BINARY_INTEGER_NOT_EQUAL:
                        case IR_BINARY_POINTER_NOT_EQUAL:
                        case IR_BINARY_SIGNED_LESS:
                        case IR_BINARY_SIGNED_LESS_EQUAL:
                        case IR_BINARY_SIGNED_GREATER:
                        case IR_BINARY_SIGNED_GREATER_EQUAL:
                        case IR_BINARY_UNSIGNED_LESS:
                        case IR_BINARY_UNSIGNED_LESS_EQUAL:
                        case IR_BINARY_UNSIGNED_GREATER:
                        case IR_BINARY_UNSIGNED_GREATER_EQUAL:
                        {
                            u8 condition = 0;
                            switch (instruction->binary_operation)
                            {
                            case IR_BINARY_INTEGER_EQUAL:
                            case IR_BINARY_POINTER_EQUAL:
                                condition = 0x94;
                                break;
                            case IR_BINARY_INTEGER_NOT_EQUAL:
                            case IR_BINARY_POINTER_NOT_EQUAL:
                                condition = 0x95;
                                break;
                            case IR_BINARY_SIGNED_LESS:
                                condition = 0x9c;
                                break;
                            case IR_BINARY_SIGNED_LESS_EQUAL:
                                condition = 0x9e;
                                break;
                            case IR_BINARY_SIGNED_GREATER:
                                condition = 0x9f;
                                break;
                            case IR_BINARY_SIGNED_GREATER_EQUAL:
                                condition = 0x9d;
                                break;
                            case IR_BINARY_UNSIGNED_LESS:
                                condition = 0x92;
                                break;
                            case IR_BINARY_UNSIGNED_LESS_EQUAL:
                                condition = 0x96;
                                break;
                            case IR_BINARY_UNSIGNED_GREATER:
                                condition = 0x97;
                                break;
                            case IR_BINARY_UNSIGNED_GREATER_EQUAL:
                                condition = 0x93;
                                break;
                            default:
                                BUSTER_UNREACHABLE();
                            }
                            if (wide)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                            }
                            codegen_emit_u8(&buffer, 0x39);
                            codegen_emit_u8(&buffer, 0xc8);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, condition);
                            codegen_emit_u8(&buffer, 0xc0);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0xb6);
                            codegen_emit_u8(&buffer, 0xc0);
                            break;
                        }
                        default:
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_LABEL_ADDRESS)
                    {
                        if (instruction->target_count != 1)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x8d);
                        codegen_emit_u8(&buffer, 0x05);
                        C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                            .target = instruction->targets[0],
                            .offset = (u32)buffer.count,
                            .label_address = true,
                        });
                        codegen_emit_u32(&buffer, 0);
                        C_X64_STORE_RESULT();
                    }
                    else if (instruction->opcode == IR_OPCODE_BRANCH)
                    {
                        codegen_emit_u8(&buffer, 0xe9);
                        C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                            .target = instruction->targets[0],
                            .offset = (u32)buffer.count,
                        });
                        codegen_emit_u32(&buffer, 0);
                    }
                    else if (instruction->opcode == IR_OPCODE_BRANCH_IF)
                    {
                        C_X64_LOAD(0x85, instruction->operands[0]);
                        codegen_emit_u8(&buffer, 0x48);
                        codegen_emit_u8(&buffer, 0x85);
                        codegen_emit_u8(&buffer, 0xc0);
                        codegen_emit_u8(&buffer, 0x0f);
                        codegen_emit_u8(&buffer, 0x85);
                        C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                            .target = instruction->targets[0],
                            .offset = (u32)buffer.count,
                        });
                        codegen_emit_u32(&buffer, 0);
                        codegen_emit_u8(&buffer, 0xe9);
                        C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                            .target = instruction->targets[1],
                            .offset = (u32)buffer.count,
                        });
                        codegen_emit_u32(&buffer, 0);
                    }
                    else if (instruction->opcode == IR_OPCODE_INDIRECT_BRANCH)
                    {
                        if (instruction->operand_count != 1)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        C_X64_LOAD(0x85, instruction->operands[0]);
                        codegen_emit_u8(&buffer, 0xff);
                        codegen_emit_u8(&buffer, 0xe0);
                    }
                    else if (instruction->opcode == IR_OPCODE_SWITCH)
                    {
                        C_X64_LOAD(0x85, instruction->operands[0]);
                        for (u32 case_index = 0; case_index < instruction->immediate_count; case_index += 1)
                        {
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0xb9);
                            codegen_emit_u64(&buffer, instruction->immediates[case_index]);
                            codegen_emit_u8(&buffer, 0x48);
                            codegen_emit_u8(&buffer, 0x39);
                            codegen_emit_u8(&buffer, 0xc8);
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, 0x84);
                            C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                                .target = instruction->targets[case_index],
                                .offset = (u32)buffer.count,
                                .conditional = true,
                            });
                            codegen_emit_u32(&buffer, 0);
                        }
                        codegen_emit_u8(&buffer, 0xe9);
                        C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                            .target = instruction->targets[instruction->target_count - 1],
                            .offset = (u32)buffer.count,
                        });
                        codegen_emit_u32(&buffer, 0);
                    }
                    else if (instruction->opcode == IR_OPCODE_SIMD)
                    {
                        if (!codegen_canonical_x64_simd_operation(&buffer, instruction, value_offsets, canonical_x64_frame_base_offset, target))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        x64_upper_vector_dirty = true;
                        x64_last_wide_vector_result = IR_VALUE_ID_INVALID;
                        x64_last_wide_vector_size = 0;
                        result.statistics.simd_operation_count += 1;
                    }
                    else if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY)
                    {
                        IrInstructionExtra asm_extra = ir_instruction_extra(function, instruction->id);
                        bool cpuid = string_equal(asm_extra.literal, S8("cpuid"));
                        bool xgetbv = string_equal(asm_extra.literal, S8("xgetbv"));
                        bool undefined = string_equal(asm_extra.literal, S8("ud2"));
                        bool no_instruction = !asm_extra.literal.length;
                        bool nop = string_equal(asm_extra.literal, S8("nop"));
                        bool pause = string_equal(asm_extra.literal, S8("pause"));
                        bool interrupt = string_equal(asm_extra.literal, S8("int3"));
                        u32 jump_target_index = 0;
                        bool jump_label = codegen_inline_assembly_jump_target(function, instruction, asm_extra.literal, S8("jmp %l"), &jump_target_index);
                        bool operandless = undefined || nop || pause || interrupt;
                        if (!cpuid && !xgetbv && !operandless && !no_instruction && !jump_label)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        if ((operandless && instruction->operand_count) || (jump_label && instruction->target_count < 2) ||
                            instruction->operand_count != instruction->immediate_count || asm_extra.operand_name_count != instruction->operand_count ||
                            (instruction->operand_count && (!instruction->operands || !instruction->immediates || !asm_extra.operand_names)) ||
                            (asm_extra.clobber_count && !asm_extra.clobbers))
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        for (u32 clobber_index = 0; clobber_index < asm_extra.clobber_count; clobber_index += 1)
                        {
                            String8 clobber = asm_extra.clobbers[clobber_index];
                            bool accepted = string_equal(clobber, S8("memory")) || string_equal(clobber, S8("cc")) ||
                                            string_equal(clobber, S8("rax")) || string_equal(clobber, S8("eax")) || string_equal(clobber, S8("ax")) ||
                                            string_equal(clobber, S8("al")) || string_equal(clobber, S8("rbx")) || string_equal(clobber, S8("ebx")) ||
                                            string_equal(clobber, S8("bx")) || string_equal(clobber, S8("bl")) || string_equal(clobber, S8("rcx")) ||
                                            string_equal(clobber, S8("ecx")) || string_equal(clobber, S8("cx")) || string_equal(clobber, S8("cl")) ||
                                            string_equal(clobber, S8("rdx")) || string_equal(clobber, S8("edx")) || string_equal(clobber, S8("dx")) ||
                                            string_equal(clobber, S8("dl")) || string_equal(clobber, S8("rsi")) || string_equal(clobber, S8("esi")) ||
                                            string_equal(clobber, S8("si")) || string_equal(clobber, S8("sil")) || string_equal(clobber, S8("rdi")) ||
                                            string_equal(clobber, S8("edi")) || string_equal(clobber, S8("di")) || string_equal(clobber, S8("dil"));
                            if (!accepted && clobber.length >= 2 && clobber.pointer[0] == 'r')
                            {
                                u64 number = 0;
                                String8 suffix = {
                                    .pointer = clobber.pointer + 1,
                                    .length = clobber.length - 1,
                                };
                                accepted = codegen_decimal_number(suffix, &number) && number >= 8 && number <= 11;
                            }
                            if (!accepted)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                        }
                        bool indirect_operands = false;
                        bool used_registers[12] = {0};
                        bool clobbered_registers[12] = {0};
                        bool* indirect = arena_allocate(arena, bool, instruction->operand_count ? instruction->operand_count : 1);
                        X64Register* asm_registers = arena_allocate(arena, X64Register, instruction->operand_count ? instruction->operand_count : 1);
                        for (u32 clobber_index = 0; clobber_index < asm_extra.clobber_count; clobber_index += 1)
                        {
                            X64Register clobber_register = X64_REGISTER_RAX;
                            if (codegen_inline_assembly_clobber_register(asm_extra.clobbers[clobber_index], &clobber_register))
                            {
                                clobbered_registers[clobber_register] = true;
                                used_registers[clobber_register] = true;
                            }
                        }
                        // Reserve every fixed-register operand before assigning
                        // any generic r operand.  The allocation order is not
                        // part of GNU asm's constraint semantics: a generic
                        // operand appearing before an a/b/c/d operand must not
                        // steal that fixed register and make a valid operand
                        // list appear to alias two outputs.
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 constraint = instruction->immediates[operand_index];
                            if (!codegen_inline_assembly_constraint_shape_valid(constraint, operand_index, instruction->operand_count, 0) ||
                                (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) > IR_INLINE_ASSEMBLY_CONSTRAINT_R)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0)
                            {
                                u32 match_index = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
                                u64 output_constraint = instruction->immediates[match_index];
                                if ((output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) == 0 ||
                                    (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0 ||
                                    (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) !=
                                        (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK))
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                for (u32 previous_index = 0; previous_index < operand_index; previous_index += 1)
                                {
                                    u64 previous_constraint = instruction->immediates[previous_index];
                                    if ((previous_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0 &&
                                        IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(previous_constraint) == match_index)
                                    {
                                        result.error = CODEGEN_ERROR_INVALID_IR;
                                        return result;
                                    }
                                }
                            }
                            X64Register fixed_register = X64_REGISTER_RAX;
                            if (codegen_inline_assembly_constraint_register(constraint, &fixed_register))
                            {
                                if (clobbered_registers[fixed_register])
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                used_registers[fixed_register] = true;
                            }
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            IrValueId operand = instruction->operands[operand_index];
                            if (operand.value >= function->value_count || function->values[operand.value].definition.value >= function->instruction_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrInstruction* definition = function->instructions + function->values[operand.value].definition.value;
                            indirect[operand_index] = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                                       definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE;
                            indirect_operands |= indirect[operand_index];
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 constraint = instruction->immediates[operand_index];
                            u64 constraint_index = constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK;
                            if (constraint_index > IR_INLINE_ASSEMBLY_CONSTRAINT_R)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrValueId operand = instruction->operands[operand_index];
                            if (operand.value >= function->value_count || function->values[operand.value].definition.value >= function->instruction_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrInstruction* definition = function->instructions + function->values[operand.value].definition.value;
                            indirect[operand_index] = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                                       definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE;
                            indirect_operands |= indirect[operand_index];
                            IrType* operand_type = ir_type_from_id(&program->types, function->values[operand.value].canonical_type);
                            if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0)
                            {
                                if (codegen_inline_assembly_type_class(operand_type) == IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID)
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                u32 match_index = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
                                IrValueId output = instruction->operands[match_index];
                                u64 output_constraint = instruction->immediates[match_index];
                                IrType* output_type = ir_type_from_id(&program->types, function->values[output.value].canonical_type);
                                if ((output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) == 0 ||
                                    (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0 ||
                                    (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) !=
                                        (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) ||
                                    !codegen_inline_assembly_types_compatible(output_type, operand_type))
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                for (u32 previous_index = 0; previous_index < operand_index; previous_index += 1)
                                {
                                    u64 previous_constraint = instruction->immediates[previous_index];
                                    if ((previous_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0 &&
                                        IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(previous_constraint) == match_index)
                                    {
                                        result.error = CODEGEN_ERROR_INVALID_IR;
                                        return result;
                                    }
                                }
                                asm_registers[operand_index] = asm_registers[match_index];
                                continue;
                            }
                            X64Register register_index = X64_REGISTER_RAX;
                            bool is_fixed_register = codegen_inline_assembly_constraint_register(constraint, &register_index);
                            if (is_fixed_register)
                            {
                                if (clobbered_registers[register_index])
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                for (u32 previous_index = 0; previous_index < operand_index; previous_index += 1)
                                {
                                    if (asm_registers[previous_index] == register_index)
                                    {
                                        u64 previous_constraint = instruction->immediates[previous_index];
                                        bool current_output = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0;
                                        bool previous_output = (previous_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0;
                                        bool current_read_write = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0;
                                        bool previous_read_write = (previous_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0;
                                        bool output_input_pair = current_output != previous_output &&
                                                                 ((current_output && !current_read_write) || (previous_output && !previous_read_write));
                                        if (!output_input_pair)
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                            return result;
                                        }
                                    }
                                }
                                used_registers[register_index] = true;
                            }
                            else
                            {
                                static X64Register const system_v_registers[] = {
                                    X64_REGISTER_RAX, X64_REGISTER_RCX, X64_REGISTER_RDX, X64_REGISTER_RSI, X64_REGISTER_RDI,
                                    X64_REGISTER_R8, X64_REGISTER_R9, X64_REGISTER_R10, X64_REGISTER_R11,
                                };
                                static X64Register const windows_registers[] = {
                                    X64_REGISTER_RAX, X64_REGISTER_RCX, X64_REGISTER_RDX, X64_REGISTER_R8, X64_REGISTER_R9,
                                    X64_REGISTER_R10, X64_REGISTER_R11,
                                };
                                X64Register const* candidates = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? windows_registers : system_v_registers;
                                u32 candidate_count = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? BUSTER_ARRAY_LENGTH(windows_registers)
                                                                                                 : BUSTER_ARRAY_LENGTH(system_v_registers);
                                bool found = false;
                                for (u32 candidate_index = 0; candidate_index < candidate_count; candidate_index += 1)
                                {
                                    X64Register candidate = candidates[candidate_index];
                                    if (!used_registers[candidate] && !(indirect_operands && candidate == X64_REGISTER_R11))
                                    {
                                        register_index = candidate;
                                        used_registers[candidate] = true;
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                            }
                            asm_registers[operand_index] = register_index;
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 constraint = instruction->immediates[operand_index];
                            if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) && !(constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE))
                            {
                                continue;
                            }
                            IrValueId input = instruction->operands[operand_index];
                            IrType* input_type = ir_type_from_id(&program->types, function->values[input.value].canonical_type);
                            if (!input_type || !input_type->layout.resolved || input_type->layout.size > UINT32_MAX ||
                                !codegen_canonical_x64_asm_memory_width((u32)input_type->layout.size))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            if (indirect[operand_index])
                            {
                                codegen_canonical_x64_asm_load(&buffer, X64_REGISTER_R11, X64_REGISTER_RBP,
                                                              (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[input.value]), 8);
                                codegen_canonical_x64_asm_load(&buffer, asm_registers[operand_index], X64_REGISTER_R11, 0,
                                                              (u32)input_type->layout.size);
                            }
                            else
                            {
                                codegen_canonical_x64_asm_load(&buffer, asm_registers[operand_index], X64_REGISTER_RBP,
                                                              (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[input.value]),
                                                              (u32)input_type->layout.size);
                            }
                        }
                        if (cpuid || xgetbv || undefined)
                        {
                            codegen_emit_u8(&buffer, 0x0f);
                            codegen_emit_u8(&buffer, cpuid ? 0xa2 : xgetbv ? 0x01 : 0x0b);
                            if (xgetbv)
                            {
                                codegen_emit_u8(&buffer, 0xd0);
                            }
                        }
                        else if (nop)
                        {
                            codegen_emit_u8(&buffer, 0x90);
                        }
                        else if (pause)
                        {
                            codegen_emit_u8(&buffer, 0xf3);
                            codegen_emit_u8(&buffer, 0x90);
                        }
                        else if (interrupt)
                        {
                            codegen_emit_u8(&buffer, 0xcc);
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 constraint = instruction->immediates[operand_index];
                            if (!(constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT))
                            {
                                continue;
                            }
                            IrValueId place_id = instruction->operands[operand_index];
                            if (place_id.value >= function->value_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrValue* place = function->values + place_id.value;
                            if (place->definition.value >= function->instruction_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrInstruction* definition = function->instructions + place->definition.value;
                            IrType* output_type = ir_type_from_id(&program->types, place->canonical_type);
                            if (!output_type || !output_type->layout.resolved || output_type->layout.size > UINT32_MAX ||
                                !codegen_canonical_x64_asm_memory_width((u32)output_type->layout.size))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            bool output_indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                                   definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE;
                            if (output_indirect)
                            {
                                codegen_canonical_x64_asm_load(&buffer, X64_REGISTER_R11, X64_REGISTER_RBP,
                                                              (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[place_id.value]), 8);
                                codegen_canonical_x64_asm_store(&buffer, X64_REGISTER_R11, asm_registers[operand_index], 0,
                                                               (u32)output_type->layout.size);
                            }
                            else
                            {
                                codegen_canonical_x64_asm_store(&buffer, X64_REGISTER_RBP, asm_registers[operand_index],
                                                               (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[place_id.value]),
                                                               (u32)output_type->layout.size);
                            }
                        }
                        // Inline assembly may use RBX for a fixed b operand or
                        // declare it clobbered.  Restore the ABI-owned value
                        // before either falling through or taking an asm-goto
                        // edge; otherwise a successor can observe the clobber
                        // or call with an invalid callee-saved register.
                        C_X64_RESTORE_RBX();
                        if (jump_label)
                        {
                            codegen_emit_u8(&buffer, 0xe9);
                            C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                                .target = instruction->targets[jump_target_index],
                                .offset = (u32)buffer.count,
                            });
                            codegen_emit_u32(&buffer, 0);
                        }
                        else if (instruction->target_count)
                        {
                            codegen_emit_u8(&buffer, 0xe9);
                            C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                                .target = instruction->targets[0],
                                .offset = (u32)buffer.count,
                            });
                            codegen_emit_u32(&buffer, 0);
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_DEBUG_TRAP)
                    {
                        codegen_emit_u8(&buffer, 0xcc);
                    }
                    else if (instruction->opcode == IR_OPCODE_UNREACHABLE)
                    {
                        codegen_emit_u8(&buffer, 0x0f);
                        codegen_emit_u8(&buffer, 0x0b);
                    }
                    else if (instruction->opcode == IR_OPCODE_RETURN)
                    {
                        if (instruction->operand_count)
                        {
                            IrValueId return_value = instruction->operands[0];
                            IrType* return_type = ir_type_from_id(&program->types, function->values[return_value.value].canonical_type);
                            u32 return_parts = 0;
                            bool aggregate_return =
                                codegen_canonical_integer_aggregate_parts(program, function->values[return_value.value].canonical_type, &return_parts);
                            CodegenCanonicalAbiValue aggregate_return_abi =
                                codegen_canonical_aggregate_abi(program, function->values[return_value.value].canonical_type, result.abi, true, false);
                            if (aggregate_return_abi.part_count && !aggregate_return_abi.indirect)
                            {
                                aggregate_return = true;
                                return_parts = aggregate_return_abi.part_count;
                            }
                            if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && aggregate_return_abi.part_count && !aggregate_return_abi.indirect &&
                                !aggregate_return_abi.memory)
                            {
                                u32 integer_index = 0;
                                u32 float_index = 0;
                                for (u32 part_index = 0; part_index < aggregate_return_abi.part_count; part_index += 1)
                                {
                                    CodegenCanonicalAbiPart* part = aggregate_return_abi.parts + part_index;
                                    s32 displacement = C_X64_FRAME_DISPLACEMENT(value_offsets[return_value.value]) + (s32)part->value_offset;
                                    if (codegen_canonical_abi_part_is_float(part->abi_class))
                                    {
                                        u32 register_size = 0;
                                        u32 register_count_used = codegen_canonical_x64_vector_part_registers(&target, part->size, &register_size);
                                        // Two vector registers is what a result
                                        // gets, unless the part is one this
                                        // target has to split, which takes as
                                        // many as it takes and starts at zero.
                                        if (!register_count_used || float_index + register_count_used > BUSTER_MAX((u32)2, register_count_used))
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                            return result;
                                        }
                                        for (u32 chunk = 0; chunk < register_count_used; chunk += 1)
                                        {
                                            if (!codegen_canonical_x64_float_memory(&buffer, target, float_index, displacement + (s32)(chunk * register_size),
                                                                                    register_size, false))
                                            {
                                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                                return result;
                                            }
                                            float_index += 1;
                                        }
                                    }
                                    else
                                    {
                                        if (integer_index >= 2)
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                            return result;
                                        }
                                        codegen_emit_u8(&buffer, 0x48);
                                        codegen_emit_u8(&buffer, 0x8b);
                                        codegen_emit_u8(&buffer, integer_index ? 0x95 : 0x85);
                                        codegen_emit_u32(&buffer, (u32)displacement);
                                        integer_index += 1;
                                    }
                                }
                                C_X64_RESTORE_RBX();
                                codegen_canonical_x64_emit_return(&buffer, frame_size, result.abi, windows_dynamic_stack);
                                instruction_id = instruction->next;
                                continue;
                            }
                            if (aggregate_return_abi.indirect)
                            {
                                if (!x64_indirect_return)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, 0x8d);
                                codegen_emit_u32(&buffer, (u32)codegen_canonical_x64_rebase_frame_displacement(&buffer, hidden_result_displacement,
                                                                                                                  canonical_x64_frame_base_offset));
                                for (u32 part_index = 0; part_index < return_parts; part_index += 1)
                                {
                                    codegen_emit_u8(&buffer, 0x48);
                                    codegen_emit_u8(&buffer, 0x8b);
                                    codegen_emit_u8(&buffer, 0x95);
                                    codegen_emit_u32(&buffer, (u32)(C_X64_FRAME_DISPLACEMENT(value_offsets[return_value.value]) + (s32)(part_index * 8)));
                                    codegen_emit_u8(&buffer, 0x48);
                                    codegen_emit_u8(&buffer, 0x89);
                                    codegen_emit_u8(&buffer, !part_index ? 0x11 : part_index * 8 <= INT8_MAX ? 0x51 : 0x91);
                                    if (part_index * 8 <= INT8_MAX && part_index)
                                    {
                                        codegen_emit_u8(&buffer, (u8)(part_index * 8));
                                    }
                                    else if (part_index)
                                    {
                                        codegen_emit_u32(&buffer, part_index * 8);
                                    }
                                }
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x89);
                                codegen_emit_u8(&buffer, 0xc8);
                                C_X64_RESTORE_RBX();
                                codegen_canonical_x64_emit_return(&buffer, frame_size, result.abi, windows_dynamic_stack);
                                instruction_id = instruction->next;
                                continue;
                            }
                            if (return_type && return_type->kind == IR_TYPE_FLOAT)
                            {
                                if (return_type->bit_width != 32 && return_type->bit_width != 64)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                codegen_emit_u8(&buffer, return_type->bit_width == 32 ? 0xf3 : 0xf2);
                                codegen_emit_u8(&buffer, 0x0f);
                                codegen_emit_u8(&buffer, 0x10);
                                codegen_emit_u8(&buffer, 0x85);
                                codegen_emit_u32(&buffer, (u32)C_X64_FRAME_DISPLACEMENT(value_offsets[return_value.value]));
                            }
                            else
                            {
                                C_X64_LOAD(0x85, return_value);
                            }
                            if (aggregate_return && return_parts == 2)
                            {
                                codegen_emit_u8(&buffer, 0x48);
                                codegen_emit_u8(&buffer, 0x8b);
                                codegen_emit_u8(&buffer, 0x95);
                                codegen_emit_u32(&buffer, (u32)(C_X64_FRAME_DISPLACEMENT(value_offsets[return_value.value]) + 8));
                            }
                        }
                        C_X64_RESTORE_RBX();
                        codegen_canonical_x64_emit_return(&buffer, frame_size, result.abi, windows_dynamic_stack);
                    }
                    else
                    {
                        result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                        return result;
                    }
#undef C_X64_STORE_RESULT
#undef C_X64_LOAD
#undef C_X64_RECORD_FLOAT_STORE
#undef C_X64_LOAD_FLOAT
#undef C_X64_RESTORE_RBX
                }
                else
                {
                    u32 result_offset = instruction->result.value == IR_ID_UNDERLYING_INVALID ? 0 : value_offsets[instruction->result.value];
#define C_A64_LOAD(register_number, value_id)                                                                                                                  \
    ((void)codegen_canonical_a64_frame_memory_operation(&buffer, (register_number), value_offsets[(value_id).value], 8, false, false))
#define C_A64_STORE(register_number) ((void)codegen_canonical_a64_frame_memory_operation(&buffer, (register_number), result_offset, 8, true, false))
                    if (instruction->opcode == IR_OPCODE_LOCAL)
                    {
                        IrValue* local = function->values + instruction->result.value;
                        u32 local_alignment = local->alignment;
                        codegen_canonical_a64_base_address(&buffer, 9, 28, aligned_local_offsets[instruction->result.value]);
                        a64_emit_constant(&buffer, 10, local_alignment - 1);
                        codegen_emit_u32(&buffer, 0x8b0a0129);
                        a64_emit_constant(&buffer, 10, ~(u64)(local_alignment - 1));
                        codegen_emit_u32(&buffer, 0x8a0a0129);
                        C_A64_STORE(9);
                    }
                    else if (instruction->opcode == IR_OPCODE_STACK_ALLOCATE)
                    {
                        u32 stack_alignment = (u32)instruction->immediates[0];
                        stack_alignment = BUSTER_MAX(stack_alignment, 16);
                        C_A64_LOAD(9, instruction->operands[0]);
                        a64_emit_constant(&buffer, 10, stack_alignment - 1);
                        codegen_emit_u32(&buffer, 0x8b0a0129);
                        a64_emit_constant(&buffer, 10, ~(u64)(stack_alignment - 1));
                        codegen_emit_u32(&buffer, 0x8a0a0129);
                        codegen_emit_u32(&buffer, 0xf140053f);
                        codegen_emit_u32(&buffer, 0x540000a3);
                        codegen_emit_u32(&buffer, 0xd14007ff);
                        codegen_emit_u32(&buffer, 0xf94003ff);
                        codegen_emit_u32(&buffer, 0xd1400529);
                        codegen_emit_u32(&buffer, 0x17fffffb);
                        codegen_emit_u32(&buffer, 0xcb2963ff);
                        codegen_emit_u32(&buffer, 0xf94003ff);
                        codegen_emit_u32(&buffer, 0x910003ea);
                        C_A64_STORE(10);
                    }
                    else if (instruction->opcode == IR_OPCODE_STACK_SAVE)
                    {
                        codegen_emit_u32(&buffer, 0x910003e9);
                        C_A64_STORE(9);
                    }
                    else if (instruction->opcode == IR_OPCODE_STACK_RESTORE)
                    {
                        C_A64_LOAD(9, instruction->operands[0]);
                        codegen_emit_u32(&buffer, 0x9100013f);
                    }
                    else if (instruction->opcode == IR_OPCODE_ARGUMENT)
                    {
                        u32 argument_index = (u32)instruction->immediates[0];
                        IrType* function_type = ir_type_from_id(&program->types, function->canonical_type);
                        if (!function_type || function_type->kind != IR_TYPE_FUNCTION || argument_index >= function_type->parameter_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u32 register_index = 0;
                        u32 float_register_index = 0;
                        u32 prior_stack_parts = 0;
                        for (u32 prior_index = 0; prior_index < argument_index; prior_index += 1)
                        {
                            u32 prior_parts = 1;
                            bool prior_aggregate =
                                codegen_canonical_integer_aggregate_parts(program, function_type->parameter_types[prior_index], &prior_parts);
                            IrType* prior_type = ir_type_from_id(&program->types, function_type->parameter_types[prior_index]);
                            CodegenCanonicalAbiValue prior_abi =
                                codegen_canonical_aggregate_abi(program, function_type->parameter_types[prior_index], result.abi, false, false);
                            bool prior_hfa = prior_abi.part_count != 0;
                            for (u32 part = 0; part < prior_abi.part_count; part += 1)
                            {
                                prior_hfa &= codegen_canonical_abi_part_is_float(prior_abi.parts[part].abi_class);
                            }
                            if (prior_hfa)
                            {
                                if (float_register_index + prior_abi.part_count <= 8)
                                {
                                    float_register_index += prior_abi.part_count;
                                }
                                else
                                {
                                    float_register_index = 8;
                                    prior_stack_parts += (u32)((prior_type->layout.size + 7) / 8);
                                }
                                continue;
                            }
                            if (prior_type && prior_type->kind == IR_TYPE_FLOAT)
                            {
                                if (float_register_index < 8)
                                {
                                    float_register_index += 1;
                                }
                                else
                                {
                                    prior_stack_parts += 1;
                                }
                                continue;
                            }
                            if (prior_aggregate && prior_type && prior_type->layout.size > 16)
                            {
                                prior_parts = 1;
                            }
                            if (register_index + prior_parts <= 8)
                            {
                                register_index += prior_parts;
                            }
                            else
                            {
                                prior_stack_parts += prior_parts;
                            }
                        }
                        u32 part_count = 1;
                        IrType* argument_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &part_count);
                        CodegenCanonicalAbiValue argument_abi = codegen_canonical_aggregate_abi(program, instruction->canonical_type, result.abi, false, false);
                        bool argument_hfa = argument_abi.part_count != 0;
                        for (u32 part = 0; part < argument_abi.part_count; part += 1)
                        {
                            argument_hfa &= codegen_canonical_abi_part_is_float(argument_abi.parts[part].abi_class);
                        }
                        bool indirect = aggregate && argument_type && argument_type->layout.size > 16;
                        u32 abi_part_count = indirect ? 1 : part_count;
                        if (!argument_type || ((argument_type->kind == IR_TYPE_STRUCT || argument_type->kind == IR_TYPE_UNION) && !aggregate))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                            return result;
                        }
                        if (argument_type->kind == IR_TYPE_FLOAT)
                        {
                            if (argument_type->bit_width != 32 && argument_type->bit_width != 64)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            if (float_register_index < 8)
                            {
                                u32 scale = argument_type->bit_width == 32 ? 4 : 8;
                                if (!codegen_canonical_a64_frame_float_memory_operation(&buffer, float_register_index, result_offset, scale, true))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            else
                            {
                                if (!codegen_canonical_a64_memory_operation_base(&buffer, 9, 16 + prior_stack_parts * 8, 8, false, false, 29) ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset, 8, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (argument_hfa)
                        {
                            if (float_register_index + argument_abi.part_count <= 8)
                            {
                                for (u32 part = 0; part < argument_abi.part_count; part += 1)
                                {
                                    codegen_canonical_a64_frame_float_memory_operation(&buffer, float_register_index + part,
                                                                                       result_offset + argument_abi.parts[part].value_offset,
                                                                                       argument_abi.parts[part].size, true);
                                }
                            }
                            else
                            {
                                for (u32 part = 0; part < argument_abi.part_count; part += 1)
                                {
                                    CodegenCanonicalAbiPart* abi_part = argument_abi.parts + part;
                                    u32 copied = 0;
                                    while (copied < abi_part->size)
                                    {
                                        u32 remaining = abi_part->size - copied;
                                        u32 chunk = remaining >= 8 ? 8 : 4;
                                        u32 source_offset = 16 + prior_stack_parts * 8 + abi_part->value_offset + copied;
                                        u32 destination_offset = result_offset + abi_part->value_offset + copied;
                                        if (!codegen_canonical_a64_memory_operation_base(&buffer, 9, source_offset, chunk, false, false, 29) ||
                                            !codegen_canonical_a64_frame_memory_operation(&buffer, 9, destination_offset, chunk, true, false))
                                        {
                                            result.error = CODEGEN_ERROR_CAPACITY;
                                            return result;
                                        }
                                        copied += chunk;
                                    }
                                }
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (register_index + abi_part_count > 8)
                        {
                            if (indirect)
                            {
                                if (!codegen_canonical_a64_memory_operation_base(&buffer, 10, 16 + prior_stack_parts * 8, 8, false, false, 29))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                for (u32 part_index = 0; part_index < part_count; part_index += 1)
                                {
                                    if (!codegen_canonical_a64_memory_operation_base(&buffer, 9, part_index * 8, 8, false, false, 10) ||
                                        !codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + part_index * 8, 8, true, false))
                                    {
                                        result.error = CODEGEN_ERROR_CAPACITY;
                                        return result;
                                    }
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            for (u32 part_index = 0; part_index < part_count; part_index += 1)
                            {
                                if (!codegen_canonical_a64_memory_operation_base(&buffer, 9, 16 + (prior_stack_parts + part_index) * 8, 8, false, false, 29) ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + part_index * 8, 8, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (indirect)
                        {
                            u32 source_register = register_index;
                            for (u32 part_index = 0; part_index < part_count; part_index += 1)
                            {
                                if (!codegen_canonical_a64_memory_operation_base(&buffer, 9, part_index * 8, 8, false, false, source_register) ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + part_index * 8, 8, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        for (u32 part_index = 0; part_index < abi_part_count; part_index += 1)
                        {
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, register_index + part_index, result_offset + part_index * 8, 8, true,
                                                                             false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_GLOBAL || instruction->opcode == IR_OPCODE_FUNCTION)
                    {
                        if (instruction->opcode == IR_OPCODE_FUNCTION && direct_call_uses[instruction->result.value] == 1)
                        {
                            codegen_emit_u32(&buffer, 0xaa1f03e9);
                            C_A64_STORE(9);
                            instruction_id = instruction->next;
                            continue;
                        }
                        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
                        bool is_thread_local = instruction->opcode == IR_OPCODE_GLOBAL && symbol && symbol->is_thread_local;
                        if (is_thread_local)
                        {
                            if (target.os == OPERATING_SYSTEM_WINDOWS)
                            {
                                u32 index_high_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0x90000009);
                                u32 index_low_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0xb9400129);
                                codegen_emit_u32(&buffer, 0xf9402e4a);
                                codegen_emit_u32(&buffer, 0xf8697949);
                                u32 value_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0x91000129);
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = index_high_offset,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                    .thread_local_index = true,
                                };
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = index_low_offset,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                    .thread_local_low = true,
                                    .thread_local_index = true,
                                };
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = value_offset,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                };
                            }
                            else if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
                            {
                                u32 descriptor_high_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0x90000000);
                                u32 descriptor_low_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0xf9400000);
                                codegen_emit_u32(&buffer, 0xf9400008);
                                codegen_emit_u32(&buffer, 0xd63f0100);
                                codegen_emit_u32(&buffer, 0xaa0003e9);
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = descriptor_high_offset,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                };
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = descriptor_low_offset,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                    .thread_local_low = true,
                                };
                            }
                            else if (target.os != OPERATING_SYSTEM_LINUX && target.os != OPERATING_SYSTEM_ANDROID)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            else
                            {
                                codegen_emit_u32(&buffer, 0xd53bd049);
                                u32 high_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0x91400129);
                                u32 low_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0x91000129);
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .entity = ANALYSIS_ENTITY_ID_INVALID,
                                    .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
                                    .symbol = instruction->symbol,
                                    .offset = high_offset,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                };
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .entity = ANALYSIS_ENTITY_ID_INVALID,
                                    .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
                                    .symbol = instruction->symbol,
                                    .offset = low_offset,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                    .thread_local_low = true,
                                };
                            }
                        }
                        else
                        {
                            codegen_emit_u32(&buffer, 0x58000049);
                            codegen_emit_u32(&buffer, 0x14000003);
                            u32 offset = (u32)buffer.count;
                            codegen_emit_u64(&buffer, 0);
                            result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                .entity = ANALYSIS_ENTITY_ID_INVALID,
                                .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
                                .symbol = instruction->symbol,
                                .offset = offset,
                                .aarch64 = true,
                                .absolute = true,
                            };
                        }
                        if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset, 8, true, false))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_LOAD || instruction->opcode == IR_OPCODE_ATOMIC_LOAD)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        u32 aggregate_parts = 0;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &aggregate_parts);
                        IrType* loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        if (instruction->opcode == IR_OPCODE_ATOMIC_LOAD && aggregate)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        if (instruction->opcode == IR_OPCODE_ATOMIC_LOAD && instruction->memory_order != IR_MEMORY_ORDER_RELAXED)
                        {
                            if (!loaded_type || !loaded_type->layout.resolved ||
                                (loaded_type->layout.size != 1 && loaded_type->layout.size != 2 && loaded_type->layout.size != 4 &&
                                 loaded_type->layout.size != 8))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            if (indirect)
                            {
                                C_A64_LOAD(10, instruction->operands[0]);
                            }
                            else
                            {
                                codegen_canonical_a64_base_address(&buffer, 10, 28, value_offsets[instruction->operands[0].value]);
                            }
                            a64_emit_atomic_pointer(&buffer, 9, 10, (u32)loaded_type->layout.size, false);
                            C_A64_STORE(9);
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (aggregate)
                        {
                            if (!loaded_type || !loaded_type->layout.resolved)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            if (indirect)
                            {
                                C_A64_LOAD(10, instruction->operands[0]);
                            }
                            for (u32 part_index = 0; part_index < aggregate_parts; part_index += 1)
                            {
                                u32 part_offset = part_index * 8;
                                u64 part_size = BUSTER_MIN((u64)8, loaded_type->layout.size - part_offset);
                                u64 part_copied = 0;
                                while (part_copied < part_size)
                                {
                                    u32 copy_offset = part_offset + (u32)part_copied;
                                    u64 remaining = part_size - part_copied;
                                    u32 chunk = codegen_canonical_copy_chunk(
                                        remaining, indirect ? copy_offset : value_offsets[instruction->operands[0].value] + copy_offset,
                                        result_offset + copy_offset);
                                    if (indirect)
                                    {
                                        a64_emit_load_pointer_offset(&buffer, 9, 10, copy_offset, chunk);
                                    }
                                    else
                                    {
                                        a64_emit_load_pointer_offset(&buffer, 9, 28, value_offsets[instruction->operands[0].value] + copy_offset, chunk);
                                    }
                                    a64_emit_store_pointer_offset(&buffer, 9, 28, result_offset + copy_offset, chunk);
                                    part_copied += chunk;
                                }
                            }
                        }
                        else if (indirect)
                        {
                            if (!loaded_type || !loaded_type->layout.resolved ||
                                (loaded_type->layout.size != 1 && loaded_type->layout.size != 2 && loaded_type->layout.size != 4 &&
                                 loaded_type->layout.size != 8))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            a64_emit_load_pointer_offset(&buffer, 9, 28, value_offsets[instruction->operands[0].value], 8);
                            a64_emit_load_pointer(&buffer, 9, 9, (u32)loaded_type->layout.size);
                        }
                        else
                        {
                            C_A64_LOAD(9, instruction->operands[0]);
                        }
                        if (!aggregate)
                        {
                            C_A64_STORE(9);
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_INDEX)
                    {
                        IrValueId base = instruction->operands[0];
                        IrInstruction* base_definition = function->instructions + function->values[base.value].definition.value;
                        IrType* base_type = ir_type_from_id(&program->types, function->values[base.value].canonical_type);
                        u32 base_offset = value_offsets[base.value];
                        if (base_definition->opcode == IR_OPCODE_LOCAL || (function->values[base.value].category == IR_VALUE_VALUE && base_type &&
                                                                           (base_type->kind == IR_TYPE_ARRAY || base_type->kind == IR_TYPE_VECTOR)))
                        {
                            if (base_definition->opcode == IR_OPCODE_LOCAL && function->values[base.value].alignment > 16)
                            {
                                C_A64_LOAD(9, base);
                            }
                            else
                            {
                                codegen_canonical_a64_base_address(&buffer, 9, 28, base_offset);
                            }
                        }
                        else
                        {
                            C_A64_LOAD(9, base);
                        }
                        C_A64_LOAD(10, instruction->operands[1]);
                        IrType* index_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
                        IrType* element = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (!index_type || index_type->kind != IR_TYPE_INTEGER || !element || element->layout.size > UINT16_MAX)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        if (index_type->is_signed && index_type->bit_width < 64)
                        {
                            u32 sign_extend = index_type->bit_width == 8    ? 0x93401d4a
                                              : index_type->bit_width == 16 ? 0x93403d4a
                                              : index_type->bit_width == 32 ? 0x93407d4a
                                                                            : 0;
                            if (!sign_extend)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            codegen_emit_u32(&buffer, sign_extend);
                        }
                        codegen_emit_u32(&buffer, 0xd280000b | ((u32)element->layout.size << 5));
                        codegen_emit_u32(&buffer, 0x9b0b7d4a);
                        codegen_emit_u32(&buffer, 0x8b0a0129);
                        C_A64_STORE(9);
                    }
                    else if (instruction->opcode == IR_OPCODE_ADDRESS_OF)
                    {
                        IrValueId object = instruction->operands[0];
                        IrInstruction* definition = function->instructions + function->values[object.value].definition.value;
                        u32 object_offset = value_offsets[object.value];
                        if (definition->opcode == IR_OPCODE_LOCAL)
                        {
                            if (function->values[object.value].alignment > 16)
                            {
                                C_A64_LOAD(9, object);
                            }
                            else
                            {
                                codegen_canonical_a64_base_address(&buffer, 9, 28, object_offset);
                            }
                        }
                        else
                        {
                            C_A64_LOAD(9, object);
                        }
                        C_A64_STORE(9);
                    }
                    else if (instruction->opcode == IR_OPCODE_DEREFERENCE)
                    {
                        C_A64_LOAD(9, instruction->operands[0]);
                        C_A64_STORE(9);
                    }
                    else if (instruction->opcode == IR_OPCODE_FIELD)
                    {
                        IrValueId base = instruction->operands[0];
                        IrInstruction* definition = function->instructions + function->values[base.value].definition.value;
                        u32 base_offset = value_offsets[base.value];
                        if (definition->opcode == IR_OPCODE_LOCAL)
                        {
                            if (function->values[base.value].alignment > 16)
                            {
                                C_A64_LOAD(9, base);
                            }
                            else
                            {
                                codegen_canonical_a64_base_address(&buffer, 9, 28, base_offset);
                            }
                        }
                        else
                        {
                            C_A64_LOAD(9, base);
                        }
                        IrType* aggregate = ir_type_from_id(&program->types, function->values[base.value].canonical_type);
                        u64 field_index = instruction->immediates[0];
                        if (!aggregate || field_index >= aggregate->field_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u64 field_offset = aggregate->fields[field_index].offset;
                        if (field_offset <= 4095)
                        {
                            codegen_emit_u32(&buffer, 0x91000129 | ((u32)field_offset << 10));
                        }
                        else
                        {
                            a64_emit_constant(&buffer, 10, field_offset);
                            codegen_emit_u32(&buffer, 0x8b0a0129);
                        }
                        C_A64_STORE(9);
                    }
                    else if (instruction->opcode == IR_OPCODE_CAST)
                    {
                        IrType* source_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
                        IrType* target_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        IrConversionOperation conversion = instruction->conversion_operation;
                        C_A64_LOAD(9, instruction->operands[0]);
                        if (!target_type || !source_type)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        if (conversion == IR_CONVERSION_INTEGER_SIGN_EXTEND && source_type->kind == IR_TYPE_INTEGER)
                        {
                            u32 sign_extend = source_type->bit_width == 8    ? 0x93401d29
                                              : source_type->bit_width == 16 ? 0x93403d29
                                              : source_type->bit_width == 32 ? 0x93407d29
                                                                             : 0;
                            if (sign_extend)
                            {
                                codegen_emit_u32(&buffer, sign_extend);
                            }
                        }
                        else if ((conversion == IR_CONVERSION_INTEGER_ZERO_EXTEND || conversion == IR_CONVERSION_INTEGER_TRUNCATE ||
                                  conversion == IR_CONVERSION_INTEGER_REINTERPRET) &&
                                 source_type->kind == IR_TYPE_INTEGER && target_type->kind == IR_TYPE_INTEGER)
                        {
                            u32 effective_bit_width = conversion == IR_CONVERSION_INTEGER_ZERO_EXTEND ? source_type->bit_width
                                                      : conversion == IR_CONVERSION_INTEGER_REINTERPRET && source_type->bit_width < target_type->bit_width
                                                          ? source_type->bit_width
                                                          : target_type->bit_width;
                            u32 zero_extend = effective_bit_width == 8    ? 0x53001d29
                                              : effective_bit_width == 16 ? 0x53003d29
                                              : effective_bit_width == 32 ? 0x2a0903e9
                                                                          : 0;
                            if (zero_extend)
                            {
                                codegen_emit_u32(&buffer, zero_extend);
                            }
                        }
                        if (target_type && source_type && target_type->kind == IR_TYPE_FLOAT && source_type->kind == IR_TYPE_INTEGER &&
                            (instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT ||
                             instruction->conversion_operation == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT))
                        {
                            if (instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT && source_type->bit_width < 64)
                            {
                                u32 sign_extend = source_type->bit_width == 8 ? 0x93401d29 : source_type->bit_width == 16 ? 0x93403d29 : 0x93407d29;
                                codegen_emit_u32(&buffer, sign_extend);
                            }
                            u32 encoded = instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT ? 0x9e220000 : 0x9e230000;
                            if (target_type->bit_width == 64)
                            {
                                encoded |= 0x00400000;
                            }
                            else if (target_type->bit_width != 32)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            codegen_emit_u32(&buffer, encoded | (9 << 5));
                            codegen_canonical_a64_frame_float_memory_operation(&buffer, 0, result_offset, (u32)target_type->layout.size, true);
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (target_type && source_type && target_type->kind == IR_TYPE_FLOAT && source_type->kind == IR_TYPE_FLOAT &&
                            (instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND ||
                             instruction->conversion_operation == IR_CONVERSION_FLOAT_TRUNCATE))
                        {
                            codegen_canonical_a64_frame_float_memory_operation(&buffer, 0, value_offsets[instruction->operands[0].value],
                                                                               (u32)source_type->layout.size, false);
                            codegen_emit_u32(&buffer, instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND ? 0x1e22c000 : 0x1e624000);
                            codegen_canonical_a64_frame_float_memory_operation(&buffer, 0, result_offset, (u32)target_type->layout.size, true);
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (target_type && source_type && target_type->kind == IR_TYPE_INTEGER && source_type->kind == IR_TYPE_FLOAT &&
                            (instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER ||
                             instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER))
                        {
                            codegen_canonical_a64_frame_float_memory_operation(&buffer, 0, value_offsets[instruction->operands[0].value],
                                                                               (u32)source_type->layout.size, false);
                            u32 encoded = instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER ? 0x9e380000 : 0x9e390000;
                            if (source_type->bit_width == 64)
                            {
                                encoded |= 0x00400000;
                            }
                            codegen_emit_u32(&buffer, encoded | 9);
                            C_A64_STORE(9);
                            instruction_id = instruction->next;
                            continue;
                        }
                        C_A64_STORE(9);
                    }
                    else if (instruction->opcode == IR_OPCODE_STORE || instruction->opcode == IR_OPCODE_ATOMIC_STORE)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        u32 aggregate_parts = 0;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, function->values[instruction->operands[1].value].canonical_type,
                                                                                   &aggregate_parts);
                        IrType* stored_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
                        if (!aggregate && stored_type && stored_type->layout.resolved && stored_type->layout.size > 8 &&
                            stored_type->layout.size <= (u64)UINT32_MAX * 8)
                        {
                            aggregate = true;
                            aggregate_parts = (u32)((stored_type->layout.size + 7) / 8);
                        }
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        if (instruction->opcode == IR_OPCODE_ATOMIC_STORE && aggregate)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        if (instruction->opcode == IR_OPCODE_ATOMIC_STORE && instruction->memory_order != IR_MEMORY_ORDER_RELAXED)
                        {
                            if (!stored_type || !stored_type->layout.resolved ||
                                (stored_type->layout.size != 1 && stored_type->layout.size != 2 && stored_type->layout.size != 4 &&
                                 stored_type->layout.size != 8))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            C_A64_LOAD(9, instruction->operands[1]);
                            if (indirect)
                            {
                                C_A64_LOAD(10, instruction->operands[0]);
                            }
                            else
                            {
                                codegen_canonical_a64_base_address(&buffer, 10, 28, value_offsets[instruction->operands[0].value]);
                            }
                            a64_emit_atomic_pointer(&buffer, 9, 10, (u32)stored_type->layout.size, true);
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (aggregate)
                        {
                            if (!stored_type || !stored_type->layout.resolved)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            if (indirect)
                            {
                                C_A64_LOAD(10, instruction->operands[0]);
                            }
                            for (u32 part_index = 0; part_index < aggregate_parts; part_index += 1)
                            {
                                u32 part_offset = part_index * 8;
                                u64 part_size = BUSTER_MIN((u64)8, stored_type->layout.size - part_offset);
                                u64 part_copied = 0;
                                while (part_copied < part_size)
                                {
                                    u64 remaining = part_size - part_copied;
                                    u32 copy_offset = part_offset + (u32)part_copied;
                                    u32 chunk =
                                        codegen_canonical_copy_chunk(remaining, value_offsets[instruction->operands[1].value] + copy_offset, copy_offset);
                                    a64_emit_load_pointer_offset(&buffer, 9, 28, value_offsets[instruction->operands[1].value] + copy_offset, chunk);
                                    if (indirect)
                                    {
                                        a64_emit_store_pointer_offset(&buffer, 9, 10, copy_offset, chunk);
                                    }
                                    else
                                    {
                                        a64_emit_store_pointer_offset(&buffer, 9, 28, value_offsets[instruction->operands[0].value] + copy_offset, chunk);
                                    }
                                    part_copied += chunk;
                                }
                            }
                        }
                        else if (indirect)
                        {
                            if (!stored_type || !stored_type->layout.resolved ||
                                (stored_type->layout.size != 1 && stored_type->layout.size != 2 && stored_type->layout.size != 4 &&
                                 stored_type->layout.size != 8))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            C_A64_LOAD(10, instruction->operands[1]);
                            C_A64_LOAD(9, instruction->operands[0]);
                            a64_emit_store_pointer(&buffer, 10, 9, (u32)stored_type->layout.size);
                        }
                        else
                        {
                            C_A64_LOAD(9, instruction->operands[1]);
                            u32 place_offset = value_offsets[instruction->operands[0].value];
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, place_offset, 8, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        bool pointer_arithmetic = value_type && value_type->kind == IR_TYPE_POINTER &&
                                                  (instruction->atomic_operation == IR_ATOMIC_ADD || instruction->atomic_operation == IR_ATOMIC_SUBTRACT);
                        if (!value_type ||
                            (!pointer_arithmetic && value_type->kind != IR_TYPE_INTEGER &&
                             (instruction->atomic_operation != IR_ATOMIC_EXCHANGE ||
                              (value_type->kind != IR_TYPE_BOOLEAN && value_type->kind != IR_TYPE_POINTER))) ||
                            !value_type->layout.resolved ||
                            (value_type->layout.size != 1 && value_type->layout.size != 2 && value_type->layout.size != 4 && value_type->layout.size != 8) ||
                            instruction->atomic_operation >= IR_ATOMIC_OPERATION_COUNT)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        C_A64_LOAD(11, instruction->operands[1]);
                        if (indirect)
                        {
                            C_A64_LOAD(10, instruction->operands[0]);
                        }
                        else
                        {
                            codegen_canonical_a64_base_address(&buffer, 10, 28, value_offsets[instruction->operands[0].value]);
                        }
                        bool acquire = instruction->memory_order == IR_MEMORY_ORDER_CONSUME || instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE ||
                                       instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE_RELEASE || instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
                        bool release = instruction->memory_order == IR_MEMORY_ORDER_RELEASE || instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE_RELEASE ||
                                       instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
                        u32 retry_offset = (u32)buffer.count;
                        a64_emit_atomic_exclusive_load(&buffer, 9, 10, (u32)value_type->layout.size, acquire);
                        u32 operation = 0;
                        bool wide = value_type->layout.size == 8;
                        switch (instruction->atomic_operation)
                        {
                        case IR_ATOMIC_ADD:
                            operation = wide ? UINT32_C(0x8b0b012c) : UINT32_C(0x0b0b012c);
                            break;
                        case IR_ATOMIC_SUBTRACT:
                            operation = wide ? UINT32_C(0xcb0b012c) : UINT32_C(0x4b0b012c);
                            break;
                        case IR_ATOMIC_BITWISE_AND:
                            operation = wide ? UINT32_C(0x8a0b012c) : UINT32_C(0x0a0b012c);
                            break;
                        case IR_ATOMIC_BITWISE_OR:
                            operation = wide ? UINT32_C(0xaa0b012c) : UINT32_C(0x2a0b012c);
                            break;
                        case IR_ATOMIC_BITWISE_XOR:
                            operation = wide ? UINT32_C(0xca0b012c) : UINT32_C(0x4a0b012c);
                            break;
                        case IR_ATOMIC_EXCHANGE:
                            operation = wide ? UINT32_C(0xaa0b03ec) : UINT32_C(0x2a0b03ec);
                            break;
                        case IR_ATOMIC_OPERATION_COUNT:
                            break;
                        }
                        codegen_emit_u32(&buffer, operation);
                        a64_emit_atomic_exclusive_store(&buffer, 13, 12, 10, (u32)value_type->layout.size, release);
                        s64 retry_displacement = (s64)retry_offset - (s64)buffer.count;
                        if (retry_displacement % 4 || retry_displacement / 4 < -INT64_C(0x40000) || retry_displacement / 4 > INT64_C(0x3ffff))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_emit_u32(&buffer, UINT32_C(0x35000000) | (((u32)(retry_displacement / 4) & UINT32_C(0x7ffff)) << 5) | 13);
                        C_A64_STORE(9);
                    }
                    else if (instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        if (!value_type || (value_type->kind != IR_TYPE_INTEGER && value_type->kind != IR_TYPE_POINTER) || !value_type->layout.resolved ||
                            (value_type->layout.size != 1 && value_type->layout.size != 2 && value_type->layout.size != 4 && value_type->layout.size != 8))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        C_A64_LOAD(12, instruction->operands[1]);
                        C_A64_LOAD(11, instruction->operands[2]);
                        if (indirect)
                        {
                            C_A64_LOAD(10, instruction->operands[0]);
                        }
                        else
                        {
                            codegen_canonical_a64_base_address(&buffer, 10, 28, value_offsets[instruction->operands[0].value]);
                        }
                        bool acquire =
                            instruction->memory_order == IR_MEMORY_ORDER_CONSUME || instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE ||
                            instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE_RELEASE || instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL ||
                            instruction->failure_memory_order == IR_MEMORY_ORDER_CONSUME || instruction->failure_memory_order == IR_MEMORY_ORDER_ACQUIRE ||
                            instruction->failure_memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
                        bool release = instruction->memory_order == IR_MEMORY_ORDER_RELEASE || instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE_RELEASE ||
                                       instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
                        u32 retry_offset = (u32)buffer.count;
                        a64_emit_atomic_exclusive_load(&buffer, 9, 10, (u32)value_type->layout.size, acquire);
                        bool wide = value_type->layout.size == 8;
                        codegen_emit_u32(&buffer, wide ? UINT32_C(0xeb0c013f) : UINT32_C(0x6b0c013f));
                        codegen_emit_u32(&buffer, UINT32_C(0x54000061));
                        a64_emit_atomic_exclusive_store(&buffer, 13, 11, 10, (u32)value_type->layout.size, release);
                        s64 retry_displacement = (s64)retry_offset - (s64)buffer.count;
                        if (retry_displacement % 4 || retry_displacement / 4 < -INT64_C(0x40000) || retry_displacement / 4 > INT64_C(0x3ffff))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_emit_u32(&buffer, UINT32_C(0x35000000) | (((u32)(retry_displacement / 4) & UINT32_C(0x7ffff)) << 5) | 13);
                        codegen_emit_u32(&buffer, UINT32_C(0xd5033f5f));
                        C_A64_STORE(9);
                    }
                    else if (instruction->opcode == IR_OPCODE_ATOMIC_FENCE)
                    {
                        if (!instruction->atomic_signal_fence && instruction->memory_order != IR_MEMORY_ORDER_RELAXED)
                        {
                            codegen_emit_u32(&buffer, UINT32_C(0xd5033bbf));
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_CLEAR_INSTRUCTION_CACHE)
                    {
                        if (instruction->operand_count != 2)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        C_A64_LOAD(9, instruction->operands[0]);
                        C_A64_LOAD(10, instruction->operands[1]);
                        codegen_emit_u32(&buffer, UINT32_C(0xaa0903eb));
                        codegen_emit_u32(&buffer, UINT32_C(0xeb0a013f));
                        codegen_emit_u32(&buffer, UINT32_C(0x54000082));
                        codegen_emit_u32(&buffer, UINT32_C(0xd50b7b29));
                        codegen_emit_u32(&buffer, UINT32_C(0x91001129));
                        codegen_emit_u32(&buffer, UINT32_C(0x17fffffc));
                        codegen_emit_u32(&buffer, UINT32_C(0xd5033b9f));
                        codegen_emit_u32(&buffer, UINT32_C(0xaa0b03e9));
                        codegen_emit_u32(&buffer, UINT32_C(0xeb0a013f));
                        codegen_emit_u32(&buffer, UINT32_C(0x54000082));
                        codegen_emit_u32(&buffer, UINT32_C(0xd50b7529));
                        codegen_emit_u32(&buffer, UINT32_C(0x91001129));
                        codegen_emit_u32(&buffer, UINT32_C(0x17fffffc));
                        codegen_emit_u32(&buffer, UINT32_C(0xd5033b9f));
                        codegen_emit_u32(&buffer, UINT32_C(0xd5033fdf));
                    }
                    else if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER || instruction->opcode == IR_OPCODE_CONSTANT_FLOAT)
                    {
                        u64 immediate = instruction->immediates[0];
                        if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER && instruction->immediate_is_negative)
                        {
                            immediate = 0 - immediate;
                        }
                        bool wide = codegen_canonical_register_is_64_bit(program, instruction->canonical_type);
                        codegen_emit_u32(&buffer, (wide ? 0xd2800009 : 0x52800009) | ((u32)(immediate & 0xffff) << 5));
                        codegen_emit_u32(&buffer, (wide ? 0xf2a00009 : 0x72a00009) | ((u32)((immediate >> 16) & 0xffff) << 5));
                        if (wide)
                        {
                            codegen_emit_u32(&buffer, 0xf2c00009 | ((u32)((immediate >> 32) & 0xffff) << 5));
                            codegen_emit_u32(&buffer, 0xf2e00009 | ((u32)((immediate >> 48) & 0xffff) << 5));
                        }
                        C_A64_STORE(9);
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_START)
                    {
                        if (!canonical_variadic)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u32 gp_count = 0;
                        u32 stack_parts = 0;
                        for (u32 parameter_index = 0; parameter_index < canonical_function_type->parameter_count; parameter_index += 1)
                        {
                            IrTypeId parameter_type = canonical_function_type->parameter_types[parameter_index];
                            IrType* parameter = ir_type_from_id(&program->types, parameter_type);
                            u32 part_count = 1;
                            bool aggregate = codegen_canonical_integer_aggregate_parts(program, parameter_type, &part_count);
                            if (aggregate && parameter && parameter->layout.size > 16)
                            {
                                part_count = 1;
                            }
                            if (gp_count + part_count <= 8)
                            {
                                gp_count += part_count;
                            }
                            else
                            {
                                stack_parts += part_count;
                            }
                        }
                        if (aarch64_darwin)
                        {
                            codegen_canonical_a64_base_address(&buffer, 9, 29, 16 + stack_parts * 8);
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset, 8, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        a64_emit_constant(&buffer, 9, gp_count * 8);
                        if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset, 8, true, false))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        u32 overflow_offset = 16 + stack_parts * 8;
                        if (overflow_offset > 4095)
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_emit_u32(&buffer, 0x910003a9 | (overflow_offset << 10));
                        if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + 8, 8, true, false))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_canonical_a64_base_address(&buffer, 9, 28, aarch64_va_save_offset);
                        if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + 16, 8, true, false) ||
                            !codegen_canonical_a64_frame_memory_operation(&buffer, 31, result_offset + 24, 8, true, false))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_COPY)
                    {
                        C_A64_LOAD(10, instruction->operands[0]);
                        if (aarch64_darwin)
                        {
                            codegen_emit_u32(&buffer, 0xf9400149);
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset, 8, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        for (u32 component = 0; component < 4; component += 1)
                        {
                            codegen_emit_u32(&buffer, 0xf9400149 | (component << 10));
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + component * 8, 8, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_END)
                    {
                        if (aarch64_darwin)
                        {
                            instruction_id = instruction->next;
                            continue;
                        }
                        C_A64_LOAD(10, instruction->operands[0]);
                        a64_emit_constant(&buffer, 9, 1);
                        codegen_emit_u32(&buffer, 0xf9000d49);
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_ARG)
                    {
                        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        u32 part_count = 1;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &part_count);
                        if (!value_type || !value_type->layout.size || value_type->layout.size > 16 ||
                            (!aggregate && value_type->kind != IR_TYPE_INTEGER && value_type->kind != IR_TYPE_BOOLEAN && value_type->kind != IR_TYPE_POINTER &&
                             value_type->kind != IR_TYPE_FLOAT))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        C_A64_LOAD(10, instruction->operands[0]);
                        if (aarch64_darwin)
                        {
                            codegen_emit_u32(&buffer, 0xf940014b);
                            for (u32 part_index = 0; part_index < part_count; part_index += 1)
                            {
                                if (!codegen_canonical_a64_memory_operation_base(&buffer, 9, part_index * 8, 8, false, false, 11) ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + part_index * 8, 8, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            a64_emit_constant(&buffer, 9, part_count * 8);
                            codegen_emit_u32(&buffer, 0x8b09016b);
                            codegen_emit_u32(&buffer, 0xf900014b);
                            instruction_id = instruction->next;
                            continue;
                        }
                        codegen_emit_u32(&buffer, 0xf940014b);
                        u32 increment = part_count * 8;
                        u32 limit = 64 - increment;
                        codegen_emit_u32(&buffer, 0xf100017f | (limit << 10));
                        u32 overflow_patch = (u32)buffer.count;
                        codegen_emit_u32(&buffer, 0x54000008);
                        codegen_emit_u32(&buffer, 0xf940094c);
                        codegen_emit_u32(&buffer, 0x8b0b018c);
                        for (u32 part_index = 0; part_index < part_count; part_index += 1)
                        {
                            codegen_emit_u32(&buffer, 0xf9400189 | (part_index << 10));
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + part_index * 8, 8, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                        codegen_emit_u32(&buffer, 0x9100016b | (increment << 10));
                        codegen_emit_u32(&buffer, 0xf900014b);
                        u32 end_patch = (u32)buffer.count;
                        codegen_emit_u32(&buffer, 0x14000000);
                        u32 overflow_offset = (u32)buffer.count;
                        codegen_emit_u32(&buffer, 0xf940054c);
                        for (u32 part_index = 0; part_index < part_count; part_index += 1)
                        {
                            codegen_emit_u32(&buffer, 0xf9400189 | (part_index << 10));
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + part_index * 8, 8, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                        codegen_emit_u32(&buffer, 0x9100018c | (increment << 10));
                        codegen_emit_u32(&buffer, 0xf900054c);
                        u32 end_offset = (u32)buffer.count;
                        u32 conditional = 0x54000008 | (((overflow_offset - overflow_patch) / 4) << 5);
                        memcpy(buffer.bytes + overflow_patch, &conditional, sizeof(conditional));
                        u32 branch = 0x14000000 | ((end_offset - end_patch) / 4);
                        memcpy(buffer.bytes + end_patch, &branch, sizeof(branch));
                    }
                    else if (instruction->opcode == IR_OPCODE_CALL)
                    {
                        u32 argument_count = instruction->operand_count - 1;
                        IrType* callee_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
                        IrType* callee_function_type = callee_type && callee_type->kind == IR_TYPE_POINTER
                                                           ? ir_type_from_id(&program->types, callee_type->element_type)
                                                           : callee_type;
                        bool darwin_variadic_call = result.abi == CODEGEN_ABI_AARCH64_DARWIN && callee_function_type &&
                                                    callee_function_type->kind == IR_TYPE_FUNCTION && callee_function_type->is_variadic;
                        bool* argument_on_stack = arena_allocate(arena, bool, argument_count);
                        u32* argument_stack_offset = arena_allocate(arena, u32, argument_count);
                        bool* argument_indirect = arena_allocate(arena, bool, argument_count);
                        u8* argument_float_register = arena_allocate(arena, u8, argument_count);
                        memset(argument_on_stack, 0, sizeof(*argument_on_stack) * argument_count);
                        memset(argument_indirect, 0, sizeof(*argument_indirect) * argument_count);
                        memset(argument_float_register, UINT8_MAX, sizeof(*argument_float_register) * argument_count);
                        u32 simulated_registers = 0;
                        u32 simulated_float_registers = 0;
                        u32 stack_part_count = 0;
                        for (u32 argument_array_index = 0; argument_array_index < argument_count; argument_array_index += 1)
                        {
                            IrValueId argument = instruction->operands[argument_array_index + 1];
                            IrTypeId type_id = function->values[argument.value].canonical_type;
                            IrType* type = ir_type_from_id(&program->types, type_id);
                            u32 part_count = 1;
                            bool aggregate = codegen_canonical_integer_aggregate_parts(program, type_id, &part_count);
                            CodegenCanonicalAbiValue argument_abi = codegen_canonical_aggregate_abi(program, type_id, result.abi, false, false);
                            bool argument_hfa = argument_abi.part_count != 0;
                            for (u32 part = 0; part < argument_abi.part_count; part += 1)
                            {
                                argument_hfa &= codegen_canonical_abi_part_is_float(argument_abi.parts[part].abi_class);
                            }
                            if (!type || ((type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION) && !aggregate))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            bool unnamed_variadic = darwin_variadic_call && argument_array_index >= callee_function_type->parameter_count;
                            if (type->kind == IR_TYPE_FLOAT)
                            {
                                if (!unnamed_variadic && simulated_float_registers < 8)
                                {
                                    argument_float_register[argument_array_index] = (u8)simulated_float_registers++;
                                }
                                else
                                {
                                    argument_on_stack[argument_array_index] = true;
                                    argument_stack_offset[argument_array_index] = stack_part_count;
                                    stack_part_count += 1;
                                }
                                continue;
                            }
                            if (argument_hfa)
                            {
                                if (!unnamed_variadic && simulated_float_registers + argument_abi.part_count <= 8)
                                {
                                    argument_float_register[argument_array_index] = (u8)simulated_float_registers;
                                    simulated_float_registers += argument_abi.part_count;
                                }
                                else
                                {
                                    if (!unnamed_variadic)
                                    {
                                        simulated_float_registers = 8;
                                    }
                                    argument_on_stack[argument_array_index] = true;
                                    argument_stack_offset[argument_array_index] = stack_part_count;
                                    stack_part_count += (u32)((type->layout.size + 7) / 8);
                                }
                                continue;
                            }
                            bool indirect = aggregate && type->layout.size > 16;
                            if (indirect)
                            {
                                part_count = 1;
                                argument_indirect[argument_array_index] = true;
                            }
                            if (!unnamed_variadic && simulated_registers + part_count <= 8)
                            {
                                simulated_registers += part_count;
                            }
                            else
                            {
                                argument_on_stack[argument_array_index] = true;
                                argument_stack_offset[argument_array_index] = stack_part_count;
                                stack_part_count += part_count;
                            }
                        }
                        u32 stack_size = (stack_part_count * 8 + 15) & ~(u32)15;
                        if (stack_size)
                        {
                            codegen_canonical_a64_adjust_stack(&buffer, stack_size, true);
                        }
                        for (u32 argument_array_index = 0; argument_array_index < argument_count; argument_array_index += 1)
                        {
                            if (!argument_on_stack[argument_array_index])
                            {
                                continue;
                            }
                            IrValueId argument = instruction->operands[argument_array_index + 1];
                            if (argument_indirect[argument_array_index])
                            {
                                u32 source_offset = value_offsets[argument.value];
                                codegen_canonical_a64_base_address(&buffer, 9, 28, source_offset);
                                if (!codegen_canonical_a64_memory_operation(&buffer, 9, argument_stack_offset[argument_array_index] * 8, 8, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                continue;
                            }
                            u32 part_count = 1;
                            codegen_canonical_integer_aggregate_parts(program, function->values[argument.value].canonical_type, &part_count);
                            for (u32 part_index = 0; part_index < part_count; part_index += 1)
                            {
                                u32 source_offset = value_offsets[argument.value] + part_index * 8;
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, source_offset, 8, false, false) ||
                                    !codegen_canonical_a64_memory_operation(&buffer, 9, (argument_stack_offset[argument_array_index] + part_index) * 8, 8, true,
                                                                            false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                        }
                        u32 register_index = 0;
                        for (u32 argument_index = 1; argument_index < instruction->operand_count; argument_index += 1)
                        {
                            IrValueId argument = instruction->operands[argument_index];
                            IrTypeId argument_type_id = function->values[argument.value].canonical_type;
                            IrType* argument_type = ir_type_from_id(&program->types, argument_type_id);
                            u32 part_count = 1;
                            bool aggregate = codegen_canonical_integer_aggregate_parts(program, argument_type_id, &part_count);
                            CodegenCanonicalAbiValue argument_abi = codegen_canonical_aggregate_abi(program, argument_type_id, result.abi, false, false);
                            bool argument_hfa = argument_abi.part_count != 0;
                            for (u32 part = 0; part < argument_abi.part_count; part += 1)
                            {
                                argument_hfa &= codegen_canonical_abi_part_is_float(argument_abi.parts[part].abi_class);
                            }
                            bool indirect = aggregate && argument_type && argument_type->layout.size > 16;
                            if (indirect)
                            {
                                part_count = 1;
                            }
                            if (argument_on_stack[argument_index - 1])
                            {
                                continue;
                            }
                            if (!argument_type || ((argument_type->kind == IR_TYPE_STRUCT || argument_type->kind == IR_TYPE_UNION) && !aggregate) ||
                                register_index + part_count > 8)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            if (argument_type->kind == IR_TYPE_FLOAT)
                            {
                                u8 float_register = argument_float_register[argument_index - 1];
                                if (float_register >= 8 || (argument_type->bit_width != 32 && argument_type->bit_width != 64))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                u32 source_offset = value_offsets[argument.value];
                                if (!codegen_canonical_a64_frame_float_memory_operation(&buffer, float_register, source_offset, argument_type->bit_width / 8,
                                                                                        false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                continue;
                            }
                            if (argument_hfa)
                            {
                                u32 float_register = argument_float_register[argument_index - 1];
                                for (u32 part = 0; part < argument_abi.part_count; part += 1)
                                {
                                    CodegenCanonicalAbiPart* abi_part = argument_abi.parts + part;
                                    u32 source_offset = value_offsets[argument.value] + abi_part->value_offset;
                                    if (float_register + part >= 8)
                                    {
                                        result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                        return result;
                                    }
                                    codegen_canonical_a64_frame_float_memory_operation(&buffer, float_register + part, source_offset, abi_part->size, false);
                                }
                                continue;
                            }
                            for (u32 part_index = 0; part_index < part_count; part_index += 1)
                            {
                                u32 source_offset = value_offsets[argument.value] + part_index * 8;
                                if (indirect)
                                {
                                    codegen_canonical_a64_base_address(&buffer, register_index, 28, source_offset);
                                }
                                else
                                {
                                    if (!codegen_canonical_a64_frame_memory_operation(&buffer, register_index, source_offset, 8, false, false))
                                    {
                                        result.error = CODEGEN_ERROR_CAPACITY;
                                        return result;
                                    }
                                }
                                register_index += 1;
                            }
                        }
                        CodegenCanonicalAbiValue call_return_abi =
                            codegen_canonical_aggregate_abi(program, instruction->canonical_type, result.abi, true, false);
                        bool call_indirect_return = call_return_abi.indirect;
                        if (call_indirect_return)
                        {
                            u32 return_offset = result_offset;
                            codegen_canonical_a64_base_address(&buffer, 8, 28, return_offset);
                        }
                        bool indirect_call = callee_type && callee_type->kind == IR_TYPE_POINTER;
                        if (indirect_call)
                        {
                            u32 callee_offset = value_offsets[instruction->operands[0].value];
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 16, callee_offset, 8, false, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            codegen_emit_u32(&buffer, 0xd63f0200);
                        }
                        else
                        {
                            u32 offset = (u32)buffer.count;
                            codegen_emit_u32(&buffer, 0x94000000);
                            result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                .entity = ANALYSIS_ENTITY_ID_INVALID,
                                .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
                                .symbol = instruction->symbol,
                                .offset = offset,
                                .aarch64 = true,
                            };
                        }
                        if (stack_size)
                        {
                            codegen_canonical_a64_adjust_stack(&buffer, stack_size, false);
                        }
                        if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
                        {
                            IrType* return_type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (return_type && return_type->kind == IR_TYPE_FLOAT)
                            {
                                if (return_type->bit_width != 32 && return_type->bit_width != 64)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                if (!codegen_canonical_a64_frame_float_memory_operation(&buffer, 0, result_offset, return_type->bit_width / 8, true))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            bool return_hfa = call_return_abi.part_count != 0;
                            for (u32 part = 0; part < call_return_abi.part_count; part += 1)
                            {
                                return_hfa &= codegen_canonical_abi_part_is_float(call_return_abi.parts[part].abi_class);
                            }
                            if (return_hfa)
                            {
                                for (u32 part = 0; part < call_return_abi.part_count; part += 1)
                                {
                                    codegen_canonical_a64_frame_float_memory_operation(&buffer, part, result_offset + call_return_abi.parts[part].value_offset,
                                                                                       call_return_abi.parts[part].size, true);
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            u32 return_parts = 0;
                            bool aggregate_return = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &return_parts);
                            if (!call_indirect_return)
                            {
                                C_A64_STORE(0);
                            }
                            if (!call_indirect_return && aggregate_return && return_parts == 2)
                            {
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 1, result_offset + 8, 8, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_ARRAY)
                    {
                        IrType* array = ir_type_from_id(&program->types, instruction->canonical_type);
                        IrType* element = array ? ir_type_from_id(&program->types, array->element_type) : 0;
                        if (!array || !element || (array->kind != IR_TYPE_ARRAY && array->kind != IR_TYPE_VECTOR) ||
                            instruction->operand_count != array->element_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        for (u32 element_index = 0; element_index < instruction->operand_count; element_index += 1)
                        {
                            u64 copied = 0;
                            while (copied < element->layout.size)
                            {
                                u64 remaining = element->layout.size - copied;
                                u64 source_offset = (u64)value_offsets[instruction->operands[element_index].value] + copied;
                                u64 destination_offset = (u64)value_offsets[instruction->result.value] + element_index * element->layout.size + copied;
                                u32 chunk = codegen_canonical_copy_chunk(remaining, source_offset, destination_offset);
                                if (source_offset > UINT32_MAX || destination_offset > UINT32_MAX ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, (u32)source_offset, chunk, false, false) ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, (u32)destination_offset, chunk, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                copied += chunk;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_AGGREGATE)
                    {
                        IrType* aggregate = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (!aggregate || instruction->operand_count != instruction->immediate_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u64 aggregate_copied = 0;
                        while (aggregate_copied < aggregate->layout.size)
                        {
                            u64 remaining = aggregate->layout.size - aggregate_copied;
                            u64 destination_offset = (u64)result_offset + aggregate_copied;
                            u32 chunk = codegen_canonical_copy_chunk(remaining, destination_offset, destination_offset);
                            if (destination_offset > UINT32_MAX ||
                                !codegen_canonical_a64_frame_memory_operation(&buffer, 31, (u32)destination_offset, chunk, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            aggregate_copied += chunk;
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 field_index = instruction->immediates[operand_index];
                            if (field_index >= aggregate->field_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrField* field = aggregate->fields + field_index;
                            IrType* field_type = ir_type_from_id(&program->types, field->type);
                            if (!field_type || !field_type->layout.resolved)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            u32 field_offset = result_offset + (u32)field->offset;
                            if (field->is_bit_field)
                            {
                                if (!field->bit_width)
                                {
                                    continue;
                                }
                                u32 field_size = (u32)field_type->layout.size;
                                if (field_size != 1 && field_size != 2 && field_size != 4 && field_size != 8)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                C_A64_LOAD(9, instruction->operands[operand_index]);
                                if (field->bit_width < 64)
                                {
                                    a64_emit_constant(&buffer, 10, ((u64)1 << field->bit_width) - 1);
                                    codegen_emit_u32(&buffer, 0x8a0a0129);
                                }
                                if (field->bit_offset)
                                {
                                    u32 shift = field->bit_offset;
                                    codegen_emit_u32(&buffer, 0xd3400000 | ((64 - shift) << 16) | ((63 - shift) << 10) | (9 << 5) | 9);
                                }
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 10, field_offset, field_size, false, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                codegen_emit_u32(&buffer, 0xaa09014a);
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 10, field_offset, field_size, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                continue;
                            }
                            u64 field_copied = 0;
                            while (field_copied < field_type->layout.size)
                            {
                                u64 remaining = field_type->layout.size - field_copied;
                                u64 source_offset = (u64)value_offsets[instruction->operands[operand_index].value] + field_copied;
                                u64 destination_offset = (u64)field_offset + field_copied;
                                u32 chunk = codegen_canonical_copy_chunk(remaining, source_offset, destination_offset);
                                if (source_offset > UINT32_MAX || destination_offset > UINT32_MAX ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, (u32)source_offset, chunk, false, false) ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, (u32)destination_offset, chunk, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                field_copied += chunk;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_UNARY)
                    {
                        IrType* canonical_unary_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (canonical_unary_type && canonical_unary_type->kind == IR_TYPE_VECTOR)
                        {
                            if (!codegen_canonical_a64_vector_operation(&buffer, program, function, instruction, value_offsets))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        C_A64_LOAD(9, instruction->operands[0]);
                        IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (instruction->unary_operation == IR_UNARY_BOOLEAN_NOT)
                        {
                            codegen_emit_u32(&buffer, 0x7100013f);
                            codegen_emit_u32(&buffer, 0x1a9f17e9);
                            C_A64_STORE(9);
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (instruction->unary_operation == IR_UNARY_FLOAT_NEGATE)
                        {
                            if (!type || type->kind != IR_TYPE_FLOAT || (type->bit_width != 32 && type->bit_width != 64))
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            codegen_emit_u32(&buffer, type->bit_width == 32 ? 0x52b0000a : 0xd2f0000a);
                            codegen_emit_u32(&buffer, type->bit_width == 32 ? 0x4a0a0129 : 0xca0a0129);
                            C_A64_STORE(9);
                            instruction_id = instruction->next;
                            continue;
                        }
                        u32 operation =
                            instruction->unary_operation == IR_UNARY_INTEGER_NEGATE                 ? (type && type->bit_width > 32 ? 0xcb0903e9 : 0x4b0903e9)
                            : instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT          ? (type && type->bit_width > 32 ? 0xaa2903e9 : 0x2a2903e9)
                            : instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS  ? (type && type->bit_width > 32 ? 0xdac01129 : 0x5ac01129)
                            : instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS ? (type && type->bit_width > 32 ? 0xdac00129 : 0x5ac00129)
                                                                                                    : 0;
                        if (!operation)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        codegen_emit_u32(&buffer, operation);
                        if (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS)
                        {
                            codegen_emit_u32(&buffer, type && type->bit_width > 32 ? 0xdac01129 : 0x5ac01129);
                        }
                        C_A64_STORE(9);
                    }
                    else if (instruction->opcode == IR_OPCODE_BINARY)
                    {
                        IrTypeId operand_type = function->values[instruction->operands[0].value].canonical_type;
                        IrType* operand_type_value = ir_type_from_id(&program->types, operand_type);
                        if (operand_type_value && operand_type_value->kind == IR_TYPE_VECTOR)
                        {
                            if (!codegen_canonical_a64_vector_operation(&buffer, program, function, instruction, value_offsets))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (operand_type_value && operand_type_value->kind == IR_TYPE_FLOAT)
                        {
                            u32 width = operand_type_value->bit_width;
                            if (width != 32 && width != 64)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            u32 left_offset = value_offsets[instruction->operands[0].value];
                            u32 right_offset = value_offsets[instruction->operands[1].value];
                            u32 float_size = width == 32 ? 4 : 8;
                            if (!codegen_canonical_a64_float_memory_operation(&buffer, 0, left_offset, float_size, false) ||
                                !codegen_canonical_a64_float_memory_operation(&buffer, 1, right_offset, float_size, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            IrBinaryOperation operation = instruction->binary_operation;
                            if (operation >= IR_BINARY_FLOAT_ADD && operation <= IR_BINARY_FLOAT_DIVIDE)
                            {
                                u32 encoded = operation == IR_BINARY_FLOAT_ADD        ? 0x1e212800
                                              : operation == IR_BINARY_FLOAT_SUBTRACT ? 0x1e213800
                                              : operation == IR_BINARY_FLOAT_MULTIPLY ? 0x1e210800
                                                                                      : 0x1e211800;
                                if (width == 64)
                                {
                                    encoded |= 0x00400000;
                                }
                                codegen_emit_u32(&buffer, encoded);
                                if (!codegen_canonical_a64_float_memory_operation(&buffer, 0, result_offset, float_size, true))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            u32 condition = operation == IR_BINARY_FLOAT_EQUAL           ? 0
                                            : operation == IR_BINARY_FLOAT_NOT_EQUAL     ? 1
                                            : operation == IR_BINARY_FLOAT_LESS          ? 4
                                            : operation == IR_BINARY_FLOAT_LESS_EQUAL    ? 9
                                            : operation == IR_BINARY_FLOAT_GREATER       ? 12
                                            : operation == IR_BINARY_FLOAT_GREATER_EQUAL ? 10
                                                                                         : UINT32_MAX;
                            if (condition == UINT32_MAX)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            codegen_emit_u32(&buffer, width == 32 ? 0x1e212000 : 0x1e612000);
                            codegen_emit_u32(&buffer, 0x1a9f07e9 | ((condition ^ 1) << 12));
                            C_A64_STORE(9);
                            instruction_id = instruction->next;
                            continue;
                        }
                        u32 wide_mask = codegen_canonical_register_is_64_bit(program, operand_type) ? 0x80000000 : 0;
                        C_A64_LOAD(9, instruction->operands[0]);
                        C_A64_LOAD(10, instruction->operands[1]);
                        u32 operation = 0;
                        switch (instruction->binary_operation)
                        {
                        case IR_BINARY_INTEGER_ADD:
                            operation = 0x0b0a0129;
                            break;
                        case IR_BINARY_INTEGER_SUBTRACT:
                            operation = 0x4b0a0129;
                            break;
                        case IR_BINARY_INTEGER_MULTIPLY:
                            operation = 0x1b0a7d29;
                            break;
                        case IR_BINARY_SIGNED_DIVIDE:
                            operation = 0x1aca0d29;
                            break;
                        case IR_BINARY_SIGNED_REMAINDER:
                            codegen_emit_u32(&buffer, codegen_canonical_a64_remainder_divide_instruction(true, wide_mask != 0));
                            operation = 0x1b0aa569;
                            break;
                        case IR_BINARY_UNSIGNED_DIVIDE:
                            operation = 0x1aca0929;
                            break;
                        case IR_BINARY_UNSIGNED_REMAINDER:
                            codegen_emit_u32(&buffer, codegen_canonical_a64_remainder_divide_instruction(false, wide_mask != 0));
                            operation = 0x1b0aa569;
                            break;
                        case IR_BINARY_SHIFT_LEFT:
                            operation = 0x1aca2129;
                            break;
                        case IR_BINARY_SIGNED_SHIFT_RIGHT:
                            operation = 0x1aca2929;
                            break;
                        case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
                            operation = 0x1aca2529;
                            break;
                        case IR_BINARY_INTEGER_BITWISE_AND:
                            operation = 0x0a0a0129;
                            break;
                        case IR_BINARY_INTEGER_BITWISE_OR:
                            operation = 0x2a0a0129;
                            break;
                        case IR_BINARY_INTEGER_BITWISE_XOR:
                            operation = 0x4a0a0129;
                            break;
                        case IR_BINARY_INTEGER_EQUAL:
                        case IR_BINARY_POINTER_EQUAL:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9f17e9;
                            break;
                        case IR_BINARY_INTEGER_NOT_EQUAL:
                        case IR_BINARY_POINTER_NOT_EQUAL:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9f07e9;
                            break;
                        case IR_BINARY_SIGNED_LESS:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9fa7e9;
                            break;
                        case IR_BINARY_SIGNED_LESS_EQUAL:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9fc7e9;
                            break;
                        case IR_BINARY_SIGNED_GREATER:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9fd7e9;
                            break;
                        case IR_BINARY_SIGNED_GREATER_EQUAL:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9fb7e9;
                            break;
                        case IR_BINARY_UNSIGNED_LESS:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9f27e9;
                            break;
                        case IR_BINARY_UNSIGNED_LESS_EQUAL:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9f87e9;
                            break;
                        case IR_BINARY_UNSIGNED_GREATER:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9f97e9;
                            break;
                        case IR_BINARY_UNSIGNED_GREATER_EQUAL:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9f37e9;
                            break;
                        default:
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        codegen_emit_u32(&buffer, operation | wide_mask);
                        C_A64_STORE(9);
                    }
                    else if (instruction->opcode == IR_OPCODE_LABEL_ADDRESS)
                    {
                        if (instruction->target_count != 1)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                            .target = instruction->targets[0],
                            .offset = (u32)buffer.count,
                            .aarch64 = true,
                            .label_address = true,
                        });
                        branch_patches[branch_patch_count - 1].secondary_offset = (u32)buffer.count + 4;
                        // Materialize the byte delta from this ADR to the
                        // target block.  Unlike ADRP, this remains correct
                        // when the text section is placed at a non-page
                        // aligned address or concatenated after another
                        // object: both addresses move by the same amount.
                        codegen_emit_u32(&buffer, 0x10000009);
                        codegen_emit_u32(&buffer, 0xd280000a);
                        codegen_emit_u32(&buffer, 0xf2a0000a);
                        codegen_emit_u32(&buffer, 0xf2c0000a);
                        codegen_emit_u32(&buffer, 0xf2e0000a);
                        codegen_emit_u32(&buffer, 0x8b0a0129);
                        C_A64_STORE(9);
                    }
                    else if (instruction->opcode == IR_OPCODE_BRANCH)
                    {
                        C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                            .target = instruction->targets[0],
                            .offset = (u32)buffer.count,
                            .aarch64 = true,
                        });
                        codegen_emit_u32(&buffer, 0x14000000);
                    }
                    else if (instruction->opcode == IR_OPCODE_INDIRECT_BRANCH)
                    {
                        if (instruction->operand_count != 1)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        C_A64_LOAD(9, instruction->operands[0]);
                        codegen_emit_u32(&buffer, 0xd61f0120);
                    }
                    else if (instruction->opcode == IR_OPCODE_BRANCH_IF)
                    {
                        C_A64_LOAD(9, instruction->operands[0]);
                        codegen_emit_u32(&buffer, 0xf100013f);
                        codegen_emit_u32(&buffer, 0x54000040);
                        C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                            .target = instruction->targets[0],
                            .offset = (u32)buffer.count,
                            .aarch64 = true,
                        });
                        codegen_emit_u32(&buffer, 0x14000000);
                        C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                            .target = instruction->targets[1],
                            .offset = (u32)buffer.count,
                            .aarch64 = true,
                        });
                        codegen_emit_u32(&buffer, 0x14000000);
                    }
                    else if (instruction->opcode == IR_OPCODE_SWITCH)
                    {
                        C_A64_LOAD(9, instruction->operands[0]);
                        for (u32 case_index = 0; case_index < instruction->immediate_count; case_index += 1)
                        {
                            u64 immediate = instruction->immediates[case_index];
                            codegen_emit_u32(&buffer, 0xd280000a | ((u32)(immediate & 0xffff) << 5));
                            codegen_emit_u32(&buffer, 0xf2a0000a | ((u32)((immediate >> 16) & 0xffff) << 5));
                            codegen_emit_u32(&buffer, 0xf2c0000a | ((u32)((immediate >> 32) & 0xffff) << 5));
                            codegen_emit_u32(&buffer, 0xf2e0000a | ((u32)((immediate >> 48) & 0xffff) << 5));
                            codegen_emit_u32(&buffer, 0xeb0a013f);
                            codegen_emit_u32(&buffer, 0x54000041);
                            C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                                .target = instruction->targets[case_index],
                                .offset = (u32)buffer.count,
                                .aarch64 = true,
                            });
                            codegen_emit_u32(&buffer, 0x14000000);
                        }
                        C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                            .target = instruction->targets[instruction->target_count - 1],
                            .offset = (u32)buffer.count,
                            .aarch64 = true,
                        });
                        codegen_emit_u32(&buffer, 0x14000000);
                    }
                    else if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY)
                    {
                        IrInstructionExtra asm_extra = ir_instruction_extra(function, instruction->id);
                        bool empty = !asm_extra.literal.length;
                        bool brk = string_equal(asm_extra.literal, S8("brk #0"));
                        u32 nop_count = codegen_canonical_a64_nop_count(asm_extra.literal);
                        bool nop = nop_count != 0;
                        bool yield = string_equal(asm_extra.literal, S8("yield"));
                        bool wait_event = string_equal(asm_extra.literal, S8("wfe"));
                        bool wait_interrupt = string_equal(asm_extra.literal, S8("wfi"));
                        bool send_event = string_equal(asm_extra.literal, S8("sev"));
                        bool send_event_local = string_equal(asm_extra.literal, S8("sevl"));
                        u32 jump_target_index = 0;
                        bool jump_label = codegen_inline_assembly_jump_target(function, instruction, asm_extra.literal, S8("b %l"), &jump_target_index);
                        if ((!empty && !brk && !nop && !yield && !wait_event && !wait_interrupt && !send_event && !send_event_local && !jump_label) ||
                            (jump_label && instruction->target_count < 2) || instruction->operand_count != instruction->immediate_count ||
                            asm_extra.operand_name_count != instruction->operand_count ||
                            (instruction->operand_count && (!instruction->operands || !instruction->immediates || !asm_extra.operand_names)) ||
                            (asm_extra.clobber_count && !asm_extra.clobbers))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        u32* asm_registers = arena_allocate(arena, u32, instruction->operand_count ? instruction->operand_count : 1);
                        bool* asm_indirect = arena_allocate(arena, bool, instruction->operand_count ? instruction->operand_count : 1);
                        bool used_asm_registers[8] = {0};
                        // x16 is a valid eighth operand register.  Keep the
                        // address scratch separate so an indirect eighth
                        // output cannot overwrite its own pointer.
                        u32 asm_address_register = 17;
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 constraint = instruction->immediates[operand_index];
                            if (!codegen_inline_assembly_constraint_shape_valid(constraint, operand_index, instruction->operand_count, 0) ||
                                (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) != IR_INLINE_ASSEMBLY_CONSTRAINT_R)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrValueId operand = instruction->operands[operand_index];
                            if (operand.value >= function->value_count || function->values[operand.value].definition.value >= function->instruction_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrInstruction* definition = function->instructions + function->values[operand.value].definition.value;
                            asm_indirect[operand_index] = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                                          definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE;
                            IrType* operand_type = ir_type_from_id(&program->types, function->values[operand.value].canonical_type);
                            if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0)
                            {
                                if (codegen_inline_assembly_type_class(operand_type) == IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID)
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                u32 match_index = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
                                IrValueId output = instruction->operands[match_index];
                                u64 output_constraint = instruction->immediates[match_index];
                                IrType* output_type = ir_type_from_id(&program->types, function->values[output.value].canonical_type);
                                if ((output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) == 0 ||
                                    (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0 ||
                                    (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) !=
                                        (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) ||
                                    !codegen_inline_assembly_types_compatible(output_type, operand_type))
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                for (u32 previous_index = 0; previous_index < operand_index; previous_index += 1)
                                {
                                    u64 previous_constraint = instruction->immediates[previous_index];
                                    if ((previous_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0 &&
                                        IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(previous_constraint) == match_index)
                                    {
                                        result.error = CODEGEN_ERROR_INVALID_IR;
                                        return result;
                                    }
                                }
                                asm_registers[operand_index] = asm_registers[match_index];
                            }
                            else
                            {
                                u32 register_index = 0;
                                bool found = false;
                                for (u32 candidate = 0; candidate < BUSTER_ARRAY_LENGTH(used_asm_registers); candidate += 1)
                                {
                                    if (!used_asm_registers[candidate])
                                    {
                                        used_asm_registers[candidate] = true;
                                        register_index = 9 + candidate;
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                asm_registers[operand_index] = register_index;
                            }
                            if (!operand_type || !operand_type->layout.resolved || operand_type->layout.size > UINT32_MAX ||
                                !codegen_canonical_a64_asm_memory_width((u32)operand_type->layout.size))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) == 0 ||
                                (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0)
                            {
                                if (asm_indirect[operand_index])
                                {
                                    if (!codegen_canonical_a64_frame_memory_operation(&buffer, asm_address_register, value_offsets[operand.value], 8, false, false) ||
                                        !codegen_canonical_a64_memory_operation_base(&buffer, asm_registers[operand_index], 0, (u32)operand_type->layout.size, false, false,
                                                                                      asm_address_register))
                                    {
                                        result.error = CODEGEN_ERROR_CAPACITY;
                                        return result;
                                    }
                                }
                                else if (!codegen_canonical_a64_frame_memory_operation(&buffer, asm_registers[operand_index], value_offsets[operand.value],
                                                                                         (u32)operand_type->layout.size, false, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                        }
                        if (!empty)
                        {
                            if (nop)
                            {
                                for (u32 nop_index = 0; nop_index < nop_count; nop_index += 1)
                                {
                                    codegen_emit_u32(&buffer, 0xd503201f);
                                }
                            }
                            else if (!jump_label)
                            {
                                codegen_emit_u32(&buffer, brk              ? 0xd4200000
                                                          : yield          ? 0xd503203f
                                                          : wait_event     ? 0xd503205f
                                                          : wait_interrupt ? 0xd503207f
                                                          : send_event     ? 0xd503209f
                                                                           : 0xd50320bf);
                            }
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 constraint = instruction->immediates[operand_index];
                            if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) == 0)
                            {
                                continue;
                            }
                            IrValueId place_id = instruction->operands[operand_index];
                            if (place_id.value >= function->value_count || function->values[place_id.value].definition.value >= function->instruction_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrType* output_type = ir_type_from_id(&program->types, function->values[place_id.value].canonical_type);
                            if (!output_type || !output_type->layout.resolved || output_type->layout.size > UINT32_MAX ||
                                !codegen_canonical_a64_asm_memory_width((u32)output_type->layout.size))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            IrInstruction* definition = function->instructions + function->values[place_id.value].definition.value;
                            bool output_indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                                   definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE;
                            if (output_indirect)
                            {
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, asm_address_register, value_offsets[place_id.value], 8, false, false) ||
                                    !codegen_canonical_a64_memory_operation_base(&buffer, asm_registers[operand_index], 0,
                                                                                   (u32)output_type->layout.size, true, false, asm_address_register))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            else if (!codegen_canonical_a64_frame_memory_operation(&buffer, asm_registers[operand_index], value_offsets[place_id.value],
                                                                                   (u32)output_type->layout.size, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                        if (jump_label)
                        {
                            C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                                .target = instruction->targets[jump_target_index],
                                .offset = (u32)buffer.count,
                                .aarch64 = true,
                            });
                            codegen_emit_u32(&buffer, 0x14000000);
                        }
                        else if (instruction->target_count)
                        {
                            C_BRANCH_PATCH_PUSH((CCanonicalBranchPatch){
                                .target = instruction->targets[0],
                                .offset = (u32)buffer.count,
                                .aarch64 = true,
                            });
                            codegen_emit_u32(&buffer, 0x14000000);
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_DEBUG_TRAP)
                    {
                        codegen_emit_u32(&buffer, 0xd4200000);
                    }
                    else if (instruction->opcode == IR_OPCODE_UNREACHABLE)
                    {
                        codegen_emit_u32(&buffer, 0xd4200000);
                    }
                    else if (instruction->opcode == IR_OPCODE_RETURN)
                    {
                        if (instruction->operand_count)
                        {
                            IrValueId return_value = instruction->operands[0];
                            IrType* return_type = ir_type_from_id(&program->types, function->values[return_value.value].canonical_type);
                            u32 return_parts = 0;
                            bool aggregate_return =
                                codegen_canonical_integer_aggregate_parts(program, function->values[return_value.value].canonical_type, &return_parts);
                            CodegenCanonicalAbiValue aggregate_return_abi =
                                codegen_canonical_aggregate_abi(program, function->values[return_value.value].canonical_type, result.abi, true, false);
                            bool return_hfa = aggregate_return_abi.part_count != 0;
                            for (u32 part = 0; part < aggregate_return_abi.part_count; part += 1)
                            {
                                return_hfa &= codegen_canonical_abi_part_is_float(aggregate_return_abi.parts[part].abi_class);
                            }
                            if (return_hfa)
                            {
                                for (u32 part = 0; part < aggregate_return_abi.part_count; part += 1)
                                {
                                    CodegenCanonicalAbiPart* abi_part = aggregate_return_abi.parts + part;
                                    codegen_canonical_a64_frame_float_memory_operation(
                                        &buffer, part, value_offsets[return_value.value] + abi_part->value_offset, abi_part->size, false);
                                }
                                if (!codegen_epilog_offset_append(descriptor, function->instruction_count,
                                                                  (u32)buffer.count - descriptor->code_offset))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                codegen_emit_u32(&buffer, 0x9100039f);
                                if (!codegen_canonical_a64_memory_operation(&buffer, 28, aarch64_frame_base_save_offset, 8, false, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                if (frame_size)
                                {
                                    codegen_canonical_a64_adjust_stack_described(&buffer, frame_size, false, 0, 0, windows_aarch64);
                                }
                                codegen_emit_u32(&buffer, 0xa8c17bfd);
                                codegen_emit_u32(&buffer, 0xd65f03c0);
                                instruction_id = instruction->next;
                                continue;
                            }
                            if (aarch64_indirect_return)
                            {
                                if (!aggregate_return || !return_type)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 10, aarch64_hidden_result_offset, 8, false, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                u64 copied = 0;
                                while (copied < return_type->layout.size)
                                {
                                    u64 remaining = return_type->layout.size - copied;
                                    u64 source_offset = (u64)value_offsets[return_value.value] + copied;
                                    u32 chunk = codegen_canonical_copy_chunk(remaining, source_offset, copied);
                                    if (source_offset > UINT32_MAX || copied > UINT32_MAX)
                                    {
                                        result.error = CODEGEN_ERROR_CAPACITY;
                                        return result;
                                    }
                                    a64_emit_load_pointer_offset(&buffer, 9, 28, (u32)source_offset, chunk);
                                    a64_emit_store_pointer_offset(&buffer, 9, 10, (u32)copied, chunk);
                                    copied += chunk;
                                }
                            }
                            else if (return_type && return_type->kind == IR_TYPE_FLOAT)
                            {
                                if (return_type->bit_width != 32 && return_type->bit_width != 64)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                if (!codegen_canonical_a64_frame_float_memory_operation(&buffer, 0, value_offsets[return_value.value],
                                                                                        return_type->bit_width / 8, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            else
                            {
                                C_A64_LOAD(0, return_value);
                            }
                            if (!aarch64_indirect_return && aggregate_return && return_parts == 2)
                            {
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 1, value_offsets[return_value.value] + 8, 8, false, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                        }
                        if (!codegen_epilog_offset_append(descriptor, function->instruction_count,
                                                          (u32)buffer.count - descriptor->code_offset))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_emit_u32(&buffer, 0x9100039f);
                        if (!codegen_canonical_a64_memory_operation(&buffer, 28, aarch64_frame_base_save_offset, 8, false, false))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        if (frame_size)
                        {
                            codegen_canonical_a64_adjust_stack_described(&buffer, frame_size, false, 0, 0, windows_aarch64);
                        }
                        codegen_emit_u32(&buffer, 0xa8c17bfd);
                        codegen_emit_u32(&buffer, 0xd65f03c0);
                    }
                    else
                    {
                        result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                        return result;
                    }
#undef C_A64_STORE
#undef C_A64_LOAD
#undef C_BRANCH_PATCH_PUSH
                }
                if (buffer.error != CODEGEN_ERROR_NONE)
                {
                    result.error = buffer.error;
                    return result;
                }
                instruction_id = instruction->next;
            }
        }
        for (u32 patch_index = 0; patch_index < branch_patch_count; patch_index += 1)
        {
            CCanonicalBranchPatch patch = branch_patches[patch_index];
            if (patch.target.value >= function->block_count)
            {
                result.error = CODEGEN_ERROR_INVALID_IR;
                return result;
            }
            s64 delta = (s64)block_offsets[patch.target.value] - (s64)patch.offset;
            if (!patch.aarch64)
            {
                delta -= 4;
                if (delta < INT32_MIN || delta > INT32_MAX)
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
                s32 encoded = (s32)delta;
                memcpy(buffer.bytes + patch.offset, &encoded, sizeof(encoded));
            }
            else
            {
                if (patch.label_address)
                {
                    if (!patch.secondary_offset || patch.secondary_offset < patch.offset || patch.secondary_offset > buffer.count || buffer.count - patch.secondary_offset < 20)
                    {
                        result.error = CODEGEN_ERROR_CAPACITY;
                        return result;
                    }
                    s64 label_address_delta = (s64)block_offsets[patch.target.value] - (s64)patch.offset;
                    u64 bits = (u64)label_address_delta;
                    u32 movz = UINT32_C(0xd280000a) | ((u32)(bits & 0xffff) << 5);
                    u32 movk16 = UINT32_C(0xf2a0000a) | ((u32)((bits >> 16) & 0xffff) << 5);
                    u32 movk32 = UINT32_C(0xf2c0000a) | ((u32)((bits >> 32) & 0xffff) << 5);
                    u32 movk48 = UINT32_C(0xf2e0000a) | ((u32)((bits >> 48) & 0xffff) << 5);
                    memcpy(buffer.bytes + patch.secondary_offset, &movz, sizeof(movz));
                    memcpy(buffer.bytes + patch.secondary_offset + 4, &movk16, sizeof(movk16));
                    memcpy(buffer.bytes + patch.secondary_offset + 8, &movk32, sizeof(movk32));
                    memcpy(buffer.bytes + patch.secondary_offset + 12, &movk48, sizeof(movk48));
                    continue;
                }
                if ((delta & 3) || (patch.conditional ? delta < -(1 << 20) || delta >= (1 << 20) : delta < -(1 << 27) || delta >= (1 << 27)))
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
                u32 instruction = 0;
                memcpy(&instruction, buffer.bytes + patch.offset, sizeof(instruction));
                u32 immediate = (u32)(delta >> 2);
                instruction |= patch.conditional ? (immediate & 0x7ffff) << 5 : immediate & 0x03ffffff;
                memcpy(buffer.bytes + patch.offset, &instruction, sizeof(instruction));
            }
        }
        u32 kept_label_address_count = 0;
        for (u32 side_index = 0; side_index < label_address_relocation_count; side_index += 1)
        {
            u32 relocation_index = label_address_relocation_indices[side_index];
            CodegenModuleRelocation* relocation = result.relocations + relocation_index;
            if (relocation->symbol.value != function->symbol.value)
            {
                label_address_relocation_indices[kept_label_address_count++] = relocation_index;
                continue;
            }
            if (relocation->label_block.value >= function->block_count)
            {
                result.error = CODEGEN_ERROR_INVALID_IR;
                return result;
            }
            s64 block_addend = (s64)block_offsets[relocation->label_block.value] - (s64)descriptor->code_offset;
            if ((block_addend > 0 && relocation->addend > INT64_MAX - block_addend) ||
                (block_addend < 0 && relocation->addend < INT64_MIN - block_addend))
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            relocation->addend += block_addend;
            relocation->label_address = false;
        }
        label_address_relocation_count = kept_label_address_count;
        descriptor->code_size = (u32)buffer.count - descriptor->code_offset;
        if (options.debug_info)
        {
            codegen_record_canonical_locations(&result, function, value_offsets, block_offsets, descriptor->code_offset, (u32)buffer.count, target, frame_size,
                                               (s32)canonical_x64_frame_base_offset, debug_location_capacity);
        }
    }
    for (u32 relocation_index = 0; !allow_unresolved_label_addresses && relocation_index < result.relocation_count; relocation_index += 1)
    {
        if (result.relocations[relocation_index].label_address)
        {
            result.error = CODEGEN_ERROR_INVALID_IR;
            return result;
        }
    }
    for (u32 assembly_index = 0; assembly_index < module->assembly_count; assembly_index += 1)
    {
        if (!codegen_emit_global_assembly(arena, program, module->assemblies[assembly_index], target, &buffer, &result))
        {
            result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
            return result;
        }
    }
    while (result.function_count < result.entry_count)
    {
        u32 function_index = result.function_count;
        CodegenModuleEntry* entry = result.entries + function_index;
        u32 end = function_index + 1 < result.entry_count ? result.entries[function_index + 1].offset : (u32)buffer.count;
        result.functions[result.function_count++] = (CodegenFunctionDescriptor){
            .symbol = entry->symbol,
            .code_offset = entry->offset,
            .code_size = end - entry->offset,
        };
    }
    result.code = (ByteSlice){
        .pointer = buffer.bytes,
        .length = buffer.count,
    };
    result.statistics.code_bytes = result.code.length;
    result.error = buffer.error;
    return result;
}

BUSTER_GLOBAL_LOCAL void codegen_function_descriptor_copy(Arena* arena, CodegenFunctionDescriptor* destination,
                                                           CodegenFunctionDescriptor const* source, u32 code_offset)
{
    *destination = *source;
    destination->code_offset += code_offset;
    destination->unwind_actions = 0;
    destination->epilog_offsets = 0;
    if (source->unwind_action_count)
    {
        destination->unwind_actions = arena_allocate(arena, CodegenUnwindAction, source->unwind_action_count);
        memcpy(destination->unwind_actions, source->unwind_actions, sizeof(*destination->unwind_actions) * source->unwind_action_count);
    }
    if (source->epilog_count)
    {
        destination->epilog_offsets = arena_allocate(arena, u32, source->epilog_count);
        memcpy(destination->epilog_offsets, source->epilog_offsets, sizeof(*destination->epilog_offsets) * source->epilog_count);
    }
}

BUSTER_GLOBAL_LOCAL void codegen_statistics_add(CodegenStatistics* destination, CodegenStatistics source)
{
    destination->instruction_count += source.instruction_count;
    destination->value_count += source.value_count;
    destination->stack_value_bytes += source.stack_value_bytes;
    destination->stack_frame_bytes += source.stack_frame_bytes;
    destination->code_bytes += source.code_bytes;
    destination->native_vector_operation_count += source.native_vector_operation_count;
    destination->split_vector_operation_count += source.split_vector_operation_count;
    destination->vzeroupper_count += source.vzeroupper_count;
    destination->forwarded_wide_vector_load_count += source.forwarded_wide_vector_load_count;
    destination->simd_operation_count += source.simd_operation_count;
    destination->function_count += source.function_count;
    destination->maximum_stack_frame_bytes = BUSTER_MAX(destination->maximum_stack_frame_bytes, source.maximum_stack_frame_bytes);
}

BUSTER_GLOBAL_LOCAL u64 codegen_module_lane_count(CodegenModuleOptions options, u32 function_count)
{
#if BUSTER_SINGLE_THREADED
    BUSTER_UNUSED(options);
    BUSTER_UNUSED(function_count);
    return 1;
#else
    u64 result = options.lane_count ? options.lane_count : os_get_logical_thread_count();
    // Matrix test processes are already admitted under this CPU quota.
    // Compiler-internal lanes share it instead of multiplying it, including
    // tests that request an otherwise-exact comparison width.
    String8 jobs_text = os_get_environment_variable(S8("BUSTER_TEST_JOBS"));
    if (jobs_text.length)
    {
        IntegerParsingU64 parsed = string8_parse_u64_decimal(jobs_text.pointer);
        if (parsed.length == jobs_text.length && parsed.value)
        {
            result = BUSTER_MIN(result, parsed.value);
        }
    }
    result = BUSTER_MAX(result, (u64)1);
    return BUSTER_MIN(result, (u64)BUSTER_MAX(function_count, 1u));
#endif
}

typedef struct CodegenCanonicalFragment CodegenCanonicalFragment;
struct CodegenCanonicalFragment
{
    CodegenModule module;
    CodegenCanonicalFragmentInfo info;
};

typedef struct CodegenCanonicalParallelState CodegenCanonicalParallelState;
struct CodegenCanonicalParallelState
{
    Arena* arena;
    Arena* fragment_arena;
    OsMutexHandle* fragment_mutex;
    IrProgram* program;
    IrModule* module;
    CodegenCanonicalFragment* fragments;
    AtomicU64 take_index;
    AtomicU64 worker_arena_failures;
    CodegenModule result;
    Target target;
    CodegenModuleOptions options;
    u64 assembly_capacity;
    u64 assembly_alignment_capacity;
    u64 worker_arena_reserve;
    u64 worker_arena_granularity;
};

BUSTER_GLOBAL_LOCAL void codegen_canonical_fragment_compact(Arena* arena, CodegenCanonicalFragment* destination, CodegenModule source,
                                                             CodegenCanonicalFragmentInfo source_info)
{
    destination->module = source;
    destination->module.ir_module = 0;
    destination->module.code = (ByteSlice){0};
    destination->module.read_only_data = (ByteSlice){0};
    destination->module.writable_data = (ByteSlice){0};
    destination->module.thread_local_data = (ByteSlice){0};
    destination->module.entries = 0;
    destination->module.functions = 0;
    destination->module.globals = 0;
    destination->module.relocations = 0;
    destination->module.data_relocations = 0;
    destination->module.line_entries = 0;
    destination->module.debug_locations = 0;
    destination->info = (CodegenCanonicalFragmentInfo){.block_count = source_info.block_count};
    if (source.error != CODEGEN_ERROR_NONE)
    {
        return;
    }

    destination->module.code = codegen_fragment_bytes_copy(arena, source.code);
    destination->module.read_only_data = codegen_fragment_bytes_copy(arena, source.read_only_data);
    destination->module.writable_data = codegen_fragment_bytes_copy(arena, source.writable_data);
    destination->module.thread_local_data = codegen_fragment_bytes_copy(arena, source.thread_local_data);
    if (source.entry_count)
    {
        destination->module.entries = arena_allocate(arena, CodegenModuleEntry, source.entry_count);
        memcpy(destination->module.entries, source.entries, sizeof(*source.entries) * source.entry_count);
    }
    if (source.function_count)
    {
        destination->module.functions = arena_allocate(arena, CodegenFunctionDescriptor, source.function_count);
        for (u32 descriptor_index = 0; descriptor_index < source.function_count; descriptor_index += 1)
        {
            codegen_function_descriptor_copy(arena, destination->module.functions + descriptor_index, source.functions + descriptor_index, 0);
        }
    }
    if (source.global_count)
    {
        destination->module.globals = arena_allocate(arena, CodegenModuleGlobal, source.global_count);
        memcpy(destination->module.globals, source.globals, sizeof(*source.globals) * source.global_count);
    }
    if (source.relocation_count)
    {
        destination->module.relocations = arena_allocate(arena, CodegenModuleRelocation, source.relocation_count);
        memcpy(destination->module.relocations, source.relocations, sizeof(*source.relocations) * source.relocation_count);
    }
    if (source.data_relocation_count)
    {
        destination->module.data_relocations = arena_allocate(arena, CodegenModuleDataRelocation, source.data_relocation_count);
        memcpy(destination->module.data_relocations, source.data_relocations,
               sizeof(*source.data_relocations) * source.data_relocation_count);
    }
    if (source.line_entry_count)
    {
        destination->module.line_entries = arena_allocate(arena, CodegenLineEntry, source.line_entry_count);
        memcpy(destination->module.line_entries, source.line_entries, sizeof(*source.line_entries) * source.line_entry_count);
    }
    if (source.debug_location_count)
    {
        destination->module.debug_locations = arena_allocate(arena, DebugLocationSeed, source.debug_location_count);
        memcpy(destination->module.debug_locations, source.debug_locations, sizeof(*source.debug_locations) * source.debug_location_count);
    }
    if (source_info.block_count)
    {
        destination->info.block_offsets = arena_allocate(arena, u32, source_info.block_count);
        memcpy(destination->info.block_offsets, source_info.block_offsets, sizeof(*source_info.block_offsets) * source_info.block_count);
    }
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_merge_add_u32(u32 value, u32 addend, u32* result)
{
    if (value > UINT32_MAX - addend)
    {
        return false;
    }
    *result = value + addend;
    return true;
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_fragments_merge(CodegenCanonicalParallelState* state)
{
    CodegenModule result = state->result;
    result.ir_module = state->module;
    for (u32 function_index = 0; function_index < state->module->function_count; function_index += 1)
    {
        CodegenModule* fragment = &state->fragments[function_index].module;
        if (fragment->error != CODEGEN_ERROR_NONE)
        {
            result.error = fragment->error;
            result.failed_function = (IrFunctionId){.value = function_index};
            result.failed_instruction = fragment->failed_instruction;
            result.failed_opcode = fragment->failed_opcode;
            state->result = result;
            return;
        }
    }

    u64 function_code_size = 0;
    u64 relocation_capacity_64 = result.relocation_count;
    u64 line_entry_capacity_64 = 0;
    u64 debug_location_capacity_64 = 0;
    for (u32 function_index = 0; function_index < state->module->function_count; function_index += 1)
    {
        if (state->module->functions[function_index].state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        CodegenModule* fragment = &state->fragments[function_index].module;
        u64 alignment = state->target.cpu_arch == CPU_ARCH_AARCH64 ? 4 : 16;
        if (function_code_size > UINT64_MAX - (alignment - 1))
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            state->result = result;
            return;
        }
        function_code_size = (function_code_size + alignment - 1) & ~(alignment - 1);
        if (fragment->code.length > UINT64_MAX - function_code_size)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            state->result = result;
            return;
        }
        function_code_size += fragment->code.length;
        relocation_capacity_64 += fragment->relocation_count;
        line_entry_capacity_64 += fragment->line_entry_count;
        debug_location_capacity_64 += fragment->debug_location_count;
    }
    if (function_code_size > UINT32_MAX || relocation_capacity_64 > UINT32_MAX || line_entry_capacity_64 > UINT32_MAX ||
        debug_location_capacity_64 > UINT32_MAX || state->assembly_alignment_capacity > UINT64_MAX - function_code_size - 64)
    {
        result.error = CODEGEN_ERROR_CAPACITY;
        state->result = result;
        return;
    }
    u64 code_capacity = function_code_size + state->assembly_alignment_capacity + 64;
    if (state->assembly_capacity > (UINT64_MAX - code_capacity) / 4)
    {
        result.error = CODEGEN_ERROR_CAPACITY;
        state->result = result;
        return;
    }
    code_capacity += state->assembly_capacity * 4;

    u32 global_relocation_count = result.relocation_count;
    CodegenModuleRelocation* global_relocations = result.relocations;
    u32 entry_capacity = state->module->function_count +
                         (u32)BUSTER_MIN(state->assembly_capacity, (u64)UINT32_MAX - state->module->function_count);
    result.entries = arena_allocate(state->arena, CodegenModuleEntry, entry_capacity);
    result.functions = arena_allocate(state->arena, CodegenFunctionDescriptor, entry_capacity);
    result.relocations = arena_allocate(state->arena, CodegenModuleRelocation, (u32)relocation_capacity_64);
    result.line_entries = state->options.debug_info ? arena_allocate(state->arena, CodegenLineEntry, (u32)line_entry_capacity_64) : 0;
    result.debug_locations = state->options.debug_info ? arena_allocate(state->arena, DebugLocationSeed, (u32)debug_location_capacity_64) : 0;
    result.entry_count = 0;
    result.function_count = 0;
    result.relocation_count = global_relocation_count;
    result.line_entry_count = 0;
    result.debug_location_count = 0;
    result.statistics = (CodegenStatistics){0};
    if (global_relocation_count)
    {
        memcpy(result.relocations, global_relocations, sizeof(*result.relocations) * global_relocation_count);
    }

    if (code_capacity > UINT32_MAX)
    {
        result.error = CODEGEN_ERROR_CAPACITY;
        state->result = result;
        return;
    }
    bool code_buffer_exhausted = false;
    CodegenBuffer buffer = {
        .bytes = arena_allocate(state->arena, u8, code_capacity),
        .capacity = code_capacity,
        .exhausted = &code_buffer_exhausted,
    };
    for (u32 function_index = 0; function_index < state->module->function_count; function_index += 1)
    {
        if (state->module->functions[function_index].state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        CodegenCanonicalFragment* canonical_fragment = state->fragments + function_index;
        CodegenModule* fragment = &canonical_fragment->module;
        result.failed_function = (IrFunctionId){.value = function_index};
        result.failed_instruction = fragment->failed_instruction;
        result.failed_opcode = fragment->failed_opcode;
        u64 alignment = state->target.cpu_arch == CPU_ARCH_AARCH64 ? 4 : 16;
        while (buffer.count % alignment && !buffer.error)
        {
            codegen_emit_u8(&buffer, state->target.cpu_arch == CPU_ARCH_X86_64 ? 0x90 : 0);
        }
        if (buffer.error || buffer.count > UINT32_MAX || fragment->code.length > buffer.capacity - buffer.count)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            state->result = result;
            return;
        }
        u32 function_offset = (u32)buffer.count;
        if (fragment->code.length)
        {
            memcpy(buffer.bytes + buffer.count, fragment->code.pointer, fragment->code.length);
            buffer.count += fragment->code.length;
        }
        for (u32 entry_index = 0; entry_index < fragment->entry_count; entry_index += 1)
        {
            CodegenModuleEntry entry = fragment->entries[entry_index];
            if (!codegen_canonical_merge_add_u32(entry.offset, function_offset, &entry.offset))
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                state->result = result;
                return;
            }
            result.entries[result.entry_count++] = entry;
        }
        for (u32 descriptor_index = 0; descriptor_index < fragment->function_count; descriptor_index += 1)
        {
            CodegenFunctionDescriptor* source = fragment->functions + descriptor_index;
            if (source->code_offset > UINT32_MAX - function_offset)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                state->result = result;
                return;
            }
            codegen_function_descriptor_copy(state->arena, result.functions + result.function_count++, source, function_offset);
        }
        for (u32 relocation_index = 0; relocation_index < fragment->relocation_count; relocation_index += 1)
        {
            CodegenModuleRelocation relocation = fragment->relocations[relocation_index];
            if (relocation.source == CODEGEN_MODULE_RELOCATION_CODE &&
                !codegen_canonical_merge_add_u32(relocation.offset, function_offset, &relocation.offset))
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                state->result = result;
                return;
            }
            result.relocations[result.relocation_count++] = relocation;
        }
        for (u32 line_index = 0; line_index < fragment->line_entry_count; line_index += 1)
        {
            CodegenLineEntry line = fragment->line_entries[line_index];
            if (!codegen_canonical_merge_add_u32(line.code_offset, function_offset, &line.code_offset))
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                state->result = result;
                return;
            }
            codegen_record_line(result.line_entries, &result.line_entry_count, (u32)line_entry_capacity_64, line.code_offset, line.source, line.line,
                                line.column);
        }
        for (u32 location_index = 0; location_index < fragment->debug_location_count; location_index += 1)
        {
            DebugLocationSeed location = fragment->debug_locations[location_index];
            if (!codegen_canonical_merge_add_u32(location.start, function_offset, &location.start) ||
                !codegen_canonical_merge_add_u32(location.end, function_offset, &location.end))
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                state->result = result;
                return;
            }
            result.debug_locations[result.debug_location_count++] = location;
        }
        codegen_statistics_add(&result.statistics, fragment->statistics);
    }

    // Static locals may contain addresses of labels in their owning function.
    // Resolve those global contributions only after every fragment has exposed
    // its local block offsets; no worker mutates the shared relocation array.
    for (u32 relocation_index = 0; relocation_index < global_relocation_count; relocation_index += 1)
    {
        CodegenModuleRelocation* relocation = result.relocations + relocation_index;
        if (!relocation->label_address)
        {
            continue;
        }
        bool found = false;
        for (u32 function_index = 0; function_index < state->module->function_count; function_index += 1)
        {
            IrFunction* function = state->module->functions + function_index;
            CodegenCanonicalFragmentInfo* info = &state->fragments[function_index].info;
            if (function->symbol.value != relocation->symbol.value)
            {
                continue;
            }
            if (!info->block_offsets || relocation->label_block.value >= info->block_count)
            {
                result.error = CODEGEN_ERROR_INVALID_IR;
                state->result = result;
                return;
            }
            s64 block_addend = info->block_offsets[relocation->label_block.value];
            if (block_addend > 0 && relocation->addend > INT64_MAX - block_addend)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                state->result = result;
                return;
            }
            relocation->addend += block_addend;
            relocation->label_address = false;
            found = true;
            break;
        }
        if (!found)
        {
            result.error = CODEGEN_ERROR_INVALID_IR;
            state->result = result;
            return;
        }
    }

    for (u32 assembly_index = 0; assembly_index < state->module->assembly_count; assembly_index += 1)
    {
        if (!codegen_emit_global_assembly(state->arena, state->program, state->module->assemblies[assembly_index], state->target, &buffer, &result))
        {
            result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
            state->result = result;
            return;
        }
    }
    if (buffer.count > UINT32_MAX)
    {
        result.error = CODEGEN_ERROR_CAPACITY;
        state->result = result;
        return;
    }
    while (result.function_count < result.entry_count)
    {
        u32 function_index = result.function_count;
        CodegenModuleEntry* entry = result.entries + function_index;
        u32 end = function_index + 1 < result.entry_count ? result.entries[function_index + 1].offset : (u32)buffer.count;
        result.functions[result.function_count++] = (CodegenFunctionDescriptor){
            .symbol = entry->symbol,
            .code_offset = entry->offset,
            .code_size = end - entry->offset,
        };
    }
    result.code = (ByteSlice){
        .pointer = buffer.bytes,
        .length = buffer.count,
    };
    result.statistics.code_bytes = result.code.length;
    result.error = buffer.error;
    state->result = result;
}

BUSTER_GLOBAL_LOCAL ThreadReturnType codegen_canonical_parallel_lane(void* argument)
{
    CodegenCanonicalParallelState* state = (CodegenCanonicalParallelState*)argument;
    // Source-position recovery amortizes through mutable single-consumer
    // cursor state. The source tables remain shared and read-only, but each
    // lane needs its own cursor while emitting debug line rows.
    IrProgram emission_program = *state->program;
    emission_program.source_cursor = IR_SOURCE_MAP_CURSOR_EMPTY;
    // A large generated function can require more transient codegen state
    // than the general-purpose 64 MiB thread scratch arena. The worker arena
    // is reset after each function; only an exact compact copy survives.
    Arena* emission_arena = codegen_worker_arena_create(state->worker_arena_reserve, state->worker_arena_granularity);
    if (!emission_arena)
    {
        atomic_u64_increment(&state->worker_arena_failures);
    }
    while (emission_arena)
    {
        u64 function_index = atomic_u64_increment(&state->take_index);
        if (function_index >= state->module->function_count)
        {
            break;
        }
        if (state->module->functions[function_index].state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        IrModule fragment_module = *state->module;
        fragment_module.functions = state->module->functions + function_index;
        fragment_module.function_count = 1;
        fragment_module.function_capacity = 1;
        fragment_module.globals = 0;
        fragment_module.global_count = 0;
        fragment_module.global_capacity = 0;
        fragment_module.assemblies = 0;
        fragment_module.assembly_count = 0;
        fragment_module.assembly_capacity = 0;
        CodegenModuleOptions fragment_options = state->options;
        fragment_options.assume_validated = true;
        fragment_options.lane_count = 1;
        CodegenCanonicalFragment* fragment = state->fragments + function_index;
        CodegenCanonicalFragmentInfo emitted_info = {0};
        CodegenModule emitted = {0};
        TemporalArena attempt_scope = arena_begin_temporal(emission_arena);
        for (u64 capacity_scale = 1;; capacity_scale *= 2)
        {
            bool code_buffer_exhausted = false;
            emitted_info = (CodegenCanonicalFragmentInfo){0};
            emitted = codegen_generate_canonical_module_attempt(emission_arena, &emission_program, &fragment_module, state->target, fragment_options,
                                                                 capacity_scale, &code_buffer_exhausted, false, &emitted_info);
            if (!code_buffer_exhausted)
            {
                break;
            }
            scratch_end(attempt_scope);
        }
        os_mutex_lock(state->fragment_mutex);
        codegen_fragment_arena_align(state->fragment_arena);
        codegen_canonical_fragment_compact(state->fragment_arena, fragment, emitted, emitted_info);
        codegen_fragment_arena_align(state->fragment_arena);
        os_mutex_unlock(state->fragment_mutex);
        if (!codegen_worker_arena_reset(&emission_arena, state->worker_arena_reserve, state->worker_arena_granularity))
        {
            atomic_u64_increment(&state->worker_arena_failures);
        }
    }
    if (emission_arena)
    {
        BUSTER_CHECK(arena_destroy(emission_arena, 1));
    }
    lane_sync();
    if (lane_index() == 0)
    {
        if (state->worker_arena_failures)
        {
            state->result.error = CODEGEN_ERROR_CAPACITY;
        }
        else
        {
            codegen_canonical_fragments_merge(state);
        }
    }
    lane_sync();
}

CodegenModule codegen_generate_canonical_module(Arena* arena, IrProgram* program, IrModule* module, Target target, CodegenModuleOptions options)
{
    CodegenModule result = {
        .ir_module = module,
        .abi = codegen_abi_for_target(target),
    };
    if (!arena || !program || !module || result.abi >= CODEGEN_ABI_COUNT || (target.cpu_arch != CPU_ARCH_X86_64 && target.cpu_arch != CPU_ARCH_AARCH64))
    {
        result.error = CODEGEN_ERROR_UNSUPPORTED_TARGET;
        return result;
    }
    codegen_prewarm();
    // ABI records and the target-for-ABI cache are mutable on first use. Freeze
    // both on the caller before any worker reads them.
    ir_prepare_program_abi(program, codegen_canonical_ir_abi_convention(result.abi));
    if (!options.assume_validated)
    {
        IrValidationResult validation = ir_validate_canonical_module(program, module);
        if (validation.error != IR_VALIDATION_NONE)
        {
            result.error = CODEGEN_ERROR_INVALID_IR;
            return result;
        }
    }
    IrModule globals_module = *module;
    globals_module.functions = 0;
    globals_module.function_count = 0;
    globals_module.function_capacity = 0;
    globals_module.assemblies = 0;
    globals_module.assembly_count = 0;
    globals_module.assembly_capacity = 0;
    CodegenModuleOptions serial_options = options;
    serial_options.assume_validated = true;
    serial_options.lane_count = 1;
    bool globals_buffer_exhausted = false;
    result = codegen_generate_canonical_module_attempt(arena, program, &globals_module, target, serial_options, 1, &globals_buffer_exhausted, true, 0);
    BUSTER_CHECK(!globals_buffer_exhausted);
    result.ir_module = module;
    if (result.error != CODEGEN_ERROR_NONE)
    {
        return result;
    }

    u64 assembly_capacity = 0;
    u64 assembly_alignment_capacity = 0;
    for (u32 assembly_index = 0; assembly_index < module->assembly_count; assembly_index += 1)
    {
        if (module->assemblies[assembly_index].source.length > UINT64_MAX - assembly_capacity)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        assembly_capacity += module->assemblies[assembly_index].source.length;
        u64 alignment_padding = codegen_global_assembly_alignment_padding(module->assemblies[assembly_index].source);
        if (alignment_padding > UINT64_MAX - assembly_alignment_capacity)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        assembly_alignment_capacity += alignment_padding;
    }
    CodegenCanonicalFragment* fragments = arena_allocate(arena, CodegenCanonicalFragment, module->function_count);
    if (module->function_count)
    {
        memset(fragments, 0, sizeof(*fragments) * module->function_count);
    }
    Arena* fragment_arena = codegen_worker_arena_create(arena->reserved_size, arena->granularity);
    OsMutexHandle* fragment_mutex = os_mutex_create();
    if (!fragment_arena || !fragment_mutex)
    {
        if (fragment_mutex)
        {
            os_mutex_destroy(fragment_mutex);
        }
        if (fragment_arena)
        {
            BUSTER_CHECK(arena_destroy(fragment_arena, 1));
        }
        result.error = CODEGEN_ERROR_CAPACITY;
        return result;
    }
    CodegenCanonicalParallelState state = {
        .arena = arena,
        .fragment_arena = fragment_arena,
        .fragment_mutex = fragment_mutex,
        .program = program,
        .module = module,
        .fragments = fragments,
        .result = result,
        .target = target,
        .options = options,
        .assembly_capacity = assembly_capacity,
        .assembly_alignment_capacity = assembly_alignment_capacity,
        .worker_arena_reserve = arena->reserved_size,
        .worker_arena_granularity = arena->granularity,
    };
    lane_run(codegen_module_lane_count(options, module->function_count), &codegen_canonical_parallel_lane, &state);
    os_mutex_destroy(fragment_mutex);
    BUSTER_CHECK(arena_destroy(fragment_arena, 1));
    return state.result;
}

CodegenExecutable codegen_make_executable(CodegenFunction function)
{
    CodegenExecutable result = {0};
    if (function.error != CODEGEN_ERROR_NONE || !function.code.length)
    {
        result.error = function.error ? function.error : CODEGEN_ERROR_INVALID_IR;
        return result;
    }
    u64 page_size = os_get_page_size();
    u64 data_offset = (function.code.length + 15) & ~(u64)15;
    u64 image_size = data_offset + function.read_only_data.length;
    u64 allocation_size = (image_size + page_size - 1) & ~(page_size - 1);
    void* address = os_reserve(0, allocation_size, (ProtectionFlags){.read = 1, .write = 1}, (MapFlags){.priv = 1, .anonymous = 1});
    if (!address)
    {
        result.error = CODEGEN_ERROR_EXECUTABLE_MEMORY;
        return result;
    }
    memcpy(address, function.code.pointer, function.code.length);
    memcpy((u8*)address + data_offset, function.read_only_data.pointer, function.read_only_data.length);
    for (CodegenDataRelocation* relocation = function.first_data_relocation; relocation; relocation = relocation->next)
    {
        u8* patch = (u8*)address + relocation->code_offset;
        u8* target = (u8*)address + data_offset + relocation->data_offset;
        if (relocation->kind == CODEGEN_DATA_RELOCATION_X86_64_PC32)
        {
            s64 displacement = target - (patch + 4);
            if (displacement < INT32_MIN || displacement > INT32_MAX)
            {
                os_unreserve(address, allocation_size);
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            s32 displacement_32 = (s32)displacement;
            memcpy(patch, &displacement_32, sizeof(displacement_32));
        }
        else if (relocation->kind == CODEGEN_DATA_RELOCATION_ABSOLUTE64)
        {
            u64 value = (u64)(uintptr_t)target;
            memcpy(patch, &value, sizeof(value));
        }
        else
        {
            os_unreserve(address, allocation_size);
            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
            return result;
        }
    }
    if (!os_commit(address, allocation_size, (ProtectionFlags){.read = 1, .execute = 1}, false))
    {
        os_unreserve(address, allocation_size);
        result.error = CODEGEN_ERROR_EXECUTABLE_MEMORY;
        return result;
    }
    if (!os_flush_instruction_cache(address, image_size))
    {
        os_unreserve(address, allocation_size);
        result.error = CODEGEN_ERROR_EXECUTABLE_MEMORY;
        return result;
    }
    result.address = address;
    result.allocation_size = allocation_size;
    return result;
}

void codegen_release_executable(CodegenExecutable executable)
{
    if (executable.address && executable.allocation_size)
    {
        os_unreserve(executable.address, executable.allocation_size);
    }
}
