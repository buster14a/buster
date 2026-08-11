#include <buster/tests/compiler/codegen/codegen_test.h>
#if BUSTER_INCLUDE_TESTS

typedef u64 CodegenTestFunction2(u64 left, u64 right);
typedef u64 CodegenTestFunction1(u64 value);
typedef f64 CodegenTestIntegerToFloatFunction(s32 value);
typedef s64 CodegenTestFloatToIntegerFunction(f64 value);
typedef u64 CodegenTestFunction0(void);
typedef f64 CodegenTestFloatFunction2(f64 left, f64 right);
typedef f64 CodegenTestFloatFunction0(void);
typedef struct CodegenTestAbiPair
{
    s64 left;
    s64 right;
} CodegenTestAbiPair;
typedef struct CodegenTestAbiMixed
{
    f64 value;
    s64 count;
} CodegenTestAbiMixed;
typedef struct CodegenTestAbiLarge
{
    s64 first;
    s64 second;
    s64 third;
} CodegenTestAbiLarge;
typedef s64 CodegenTestAbiPairSumFunction(CodegenTestAbiPair pair);
typedef CodegenTestAbiPair CodegenTestAbiPairMakeFunction(s64 left, s64 right);
typedef f64 CodegenTestAbiMixedSumFunction(CodegenTestAbiMixed mixed);
typedef s64 CodegenTestAbiLargeSumFunction(CodegenTestAbiLarge large);
typedef CodegenTestAbiLarge CodegenTestAbiLargeMakeFunction(s64 first, s64 second, s64 third);

#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
// The native f80 differential below is a test harness only.  The backend
// itself never consults host long double; the host ABI is the independent
// oracle we call through after the generated module has been emitted.
typedef long double CodegenTestHostF80;
typedef CodegenTestHostF80 CodegenTestHostF80Function0(void);
typedef CodegenTestHostF80 CodegenTestHostF80Function1(CodegenTestHostF80 value);
typedef CodegenTestHostF80 CodegenTestHostF80Function2(CodegenTestHostF80 first, CodegenTestHostF80 second);
typedef s64 CodegenTestHostF80LayoutFunction(s64 a, s64 b, s64 c, s64 d, s64 e, s64 f, s64 g, CodegenTestHostF80 first, CodegenTestHostF80 second,
                                             s64 tail);

BUSTER_GLOBAL_LOCAL CodegenTestHostF80 codegen_test_host_f80_probe(CodegenTestHostF80 first, CodegenTestHostF80 second)
{
    (void)first;
    return second;
}

BUSTER_GLOBAL_LOCAL bool codegen_test_promote_canonical_f64_to_f80(IrProgram* program)
{
    if (!program)
    {
        return false;
    }
    bool promoted = false;
    for (u32 type_index = 0; type_index < program->types.count; type_index += 1)
    {
        IrType* type = program->types.types + type_index;
        if (type->kind == IR_TYPE_FLOAT && type->bit_width == 64)
        {
            type->bit_width = 80;
            type->layout.size = 16;
            type->layout.alignment = 16;
            type->abi = 0;
            promoted = true;
        }
    }
    for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
    {
        IrModule* module = program->modules + module_index;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
            {
                IrType* value_type = ir_type_from_id(&program->types, function->values[value_index].canonical_type);
                if (value_type && value_type->kind == IR_TYPE_FLOAT && value_type->bit_width == 80 && function->values[value_index].alignment < 16)
                {
                    function->values[value_index].alignment = 16;
                }
            }
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                if (instruction->opcode == IR_OPCODE_CONSTANT_FLOAT && type && type->kind == IR_TYPE_FLOAT && type->bit_width == 80)
                {
                    u64* immediates = arena_allocate(program->arena, u64, 2);
                    instruction->immediate_count = 2;
                    instruction->immediate_is_negative = false;
                    immediates[0] = UINT64_C(0x8000000000000000); // +1.0
                    immediates[1] = UINT64_C(0x3fff);
                    instruction->immediates = immediates;
                }
            }
        }
    }
    return promoted;
}

BUSTER_GLOBAL_LOCAL void codegen_test_host_f80_set(CodegenTestHostF80* value, u64 significand, u16 sign_exponent)
{
    u8 bytes[16] = {0};
    memcpy(bytes, &significand, sizeof(significand));
    memcpy(bytes + 8, &sign_exponent, sizeof(sign_exponent));
    u64 copy_size = BUSTER_MIN((u64)sizeof(*value), (u64)sizeof(bytes));
    memcpy(value, bytes, copy_size);
}

BUSTER_GLOBAL_LOCAL bool codegen_test_host_f80_semantic_equal(CodegenTestHostF80 value, u64 significand, u16 sign_exponent)
{
    u8 expected[10] = {0};
    memcpy(expected, &significand, sizeof(significand));
    memcpy(expected + 8, &sign_exponent, sizeof(sign_exponent));
    u8 actual[16] = {0};
    u64 copy_size = BUSTER_MIN((u64)sizeof(value), (u64)sizeof(actual));
    memcpy(actual, &value, copy_size);
    return copy_size >= sizeof(expected) && !memcmp(actual, expected, sizeof(expected));
}

BUSTER_GLOBAL_LOCAL void codegen_test_patch_local_canonical_calls(CodegenModule* module)
{
    if (!module || module->error != CODEGEN_ERROR_NONE)
    {
        return;
    }
    for (u32 relocation_index = 0; relocation_index < module->relocation_count; relocation_index += 1)
    {
        CodegenModuleRelocation* relocation = module->relocations + relocation_index;
        if (relocation->source != CODEGEN_MODULE_RELOCATION_CODE || relocation->absolute || relocation->aarch64 ||
            relocation->offset > module->code.length || module->code.length - relocation->offset < 4)
        {
            continue;
        }
        for (u32 entry_index = 0; entry_index < module->entry_count; entry_index += 1)
        {
            CodegenModuleEntry* entry = module->entries + entry_index;
            if (entry->symbol.value != relocation->symbol.value)
            {
                continue;
            }
            s64 displacement = (s64)entry->offset - ((s64)relocation->offset + 4);
            if (displacement < INT32_MIN || displacement > INT32_MAX)
            {
                continue;
            }
            s32 encoded = (s32)displacement;
            memcpy(module->code.pointer + relocation->offset, &encoded, sizeof(encoded));
            break;
        }
    }
}
#endif

BUSTER_GLOBAL_LOCAL AnalysisEntity* codegen_test_entity_find(AnalysisResult* analysis, String8 name)
{
    for (u32 index = 0; index < analysis->module.entity_count; index += 1)
    {
        AnalysisEntity* entity = analysis->module.entities + index;
        if (entity->kind == ANALYSIS_ENTITY_CODE && string_equal(entity->name, name))
        {
            return entity;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL IrFunction* codegen_test_function_find(IrModule* module, AnalysisEntityId entity)
{
    for (u32 index = 0; index < module->function_count; index += 1)
    {
        if (module->functions[index].entity.module.value == entity.module.value && module->functions[index].entity.index.value == entity.index.value)
        {
            return module->functions + index;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL CodegenModuleEntry* codegen_test_module_entry_find(CodegenModule* module, AnalysisEntityId entity)
{
    for (u32 index = 0; index < module->entry_count; index += 1)
    {
        CodegenModuleEntry* entry = module->entries + index;
        if (entry->entity.module.value == entity.module.value && entry->entity.index.value == entity.index.value)
        {
            return entry;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL IrFunction* codegen_test_c_function_find(IrModule* module, String8 name)
{
    for (u32 index = 0; index < module->function_count; index += 1)
    {
        if (string_equal(module->functions[index].name, name))
        {
            return module->functions + index;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL CodegenFunctionDescriptor* codegen_test_c_descriptor_find(CodegenModule* module, IrSymbolId symbol)
{
    for (u32 index = 0; index < module->function_count; index += 1)
    {
        if (module->functions[index].symbol.value == symbol.value)
        {
            return module->functions + index;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL u32 codegen_test_canonical_value_frame_size(IrProgram* program, IrFunction* function)
{
    u64 value_bytes = 0;
    for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
    {
        IrType* type = ir_type_from_id(&program->types, function->values[value_index].canonical_type);
        IrInstructionId definition = function->values[value_index].definition;
        bool global_place = definition.value < function->instruction_count && function->instructions[definition.value].opcode == IR_OPCODE_GLOBAL;
        u64 slot_size = global_place ? 8 : (type->layout.size + 7) & ~(u64)7;
        slot_size = BUSTER_MAX(slot_size, 8u);
        u64 alignment = global_place ? 8 : BUSTER_MAX(BUSTER_MAX(type->layout.alignment, function->values[value_index].alignment), 8u);
        value_bytes += slot_size;
        u64 remainder = value_bytes % alignment;
        if (remainder)
        {
            value_bytes += alignment - remainder;
        }
    }
    return (u32)((value_bytes + 15) & ~(u64)15);
}

BUSTER_GLOBAL_LOCAL u32 codegen_test_canonical_descriptor_stack_size(CodegenFunctionDescriptor* descriptor)
{
    u32 stack_size = 0;
    if (descriptor)
    {
        for (u32 action_index = 0; action_index < descriptor->unwind_action_count; action_index += 1)
        {
            CodegenUnwindAction* action = descriptor->unwind_actions + action_index;
            if (action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK)
            {
                stack_size += action->value;
            }
        }
    }
    return stack_size;
}

typedef struct CodegenTestX64Modrm CodegenTestX64Modrm;
struct CodegenTestX64Modrm
{
    u32 length;
    u8 mod;
    u8 reg;
    u8 rm;
    u8 base;
    s32 displacement;
    bool memory;
    bool rsp_memory;
};

typedef struct CodegenTestX64Instruction CodegenTestX64Instruction;
struct CodegenTestX64Instruction
{
    u32 length;
    u32 stack_store_end;
    bool call;
    bool indirect_call;
    bool rsp_change;
    bool add_rsp;
    bool lea_rsp_frame;
    s32 rsp_adjust;
    s32 lea_rsp_displacement;
    bool stack_store;
};

BUSTER_GLOBAL_LOCAL bool codegen_test_x64_parse_modrm(ByteSlice code, u64 offset, u64 end, u8 rex, CodegenTestX64Modrm* result)
{
    if (!result || offset >= end)
    {
        return false;
    }
    u64 cursor = offset;
    u8 modrm = code.pointer[cursor++];
    result->mod = modrm >> 6;
    result->reg = (u8)(((modrm >> 3) & 7) | ((rex & 4) ? 8 : 0));
    result->rm = (u8)((modrm & 7) | ((rex & 1) ? 8 : 0));
    result->base = result->rm;
    result->memory = result->mod != 3;
    result->rsp_memory = false;
    result->displacement = 0;
    if (result->memory && (modrm & 7) == 4)
    {
        if (cursor >= end)
        {
            return false;
        }
        u8 sib = code.pointer[cursor++];
        result->base = (u8)((sib & 7) | ((rex & 1) ? 8 : 0));
        if (result->mod == 0 && (sib & 7) == 5)
        {
            result->base = UINT8_MAX;
        }
    }
    else if (result->memory && result->mod == 0 && (modrm & 7) == 5)
    {
        result->base = UINT8_MAX;
    }
    u32 displacement_size = result->memory ? result->mod == 1 ? 1 : (result->mod == 2 || result->base == UINT8_MAX) ? 4 : 0 : 0;
    if (cursor + displacement_size > end)
    {
        return false;
    }
    if (displacement_size == 1)
    {
        result->displacement = (s8)code.pointer[cursor];
    }
    else if (displacement_size == 4)
    {
        memcpy(&result->displacement, code.pointer + cursor, sizeof(result->displacement));
    }
    result->rsp_memory = result->memory && result->base == 4;
    result->length = (u32)(cursor + displacement_size - offset);
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_test_x64_modrm_rm_writes(u8 opcode)
{
    switch (opcode)
    {
    case 0x00:
    case 0x01:
    case 0x08:
    case 0x09:
    case 0x10:
    case 0x11:
    case 0x18:
    case 0x19:
    case 0x20:
    case 0x21:
    case 0x28:
    case 0x29:
    case 0x30:
    case 0x31:
    case 0x80:
    case 0x81:
    case 0x83:
    case 0x88:
    case 0x89:
    case 0x8f:
    case 0xc0:
    case 0xc1:
    case 0xc6:
    case 0xc7:
    case 0xfe:
        return true;
        break;
    default:
        return false;
        break;
    }
}

BUSTER_GLOBAL_LOCAL bool codegen_test_x64_modrm_reg_writes(u8 opcode)
{
    switch (opcode)
    {
    case 0x02:
    case 0x03:
    case 0x0a:
    case 0x0b:
    case 0x12:
    case 0x13:
    case 0x1a:
    case 0x1b:
    case 0x22:
    case 0x23:
    case 0x2a:
    case 0x2b:
    case 0x32:
    case 0x33:
    case 0x63:
    case 0x69:
    case 0x6b:
    case 0x8a:
    case 0x8b:
    case 0x8d:
        return true;
        break;
    default:
        return false;
        break;
    }
}

BUSTER_GLOBAL_LOCAL bool codegen_test_x64_decode_instruction(ByteSlice code, u64 offset, u64 end, CodegenTestX64Instruction* result)
{
    if (!result || offset >= end)
    {
        return false;
    }
    *result = (CodegenTestX64Instruction){0};
    u64 cursor = offset;
    u8 rex = 0;
    bool operand16 = false;
    for (u32 prefix_count = 0; prefix_count < 15 && cursor < end; prefix_count += 1)
    {
        u8 byte = code.pointer[cursor];
        if (byte == 0x66)
        {
            operand16 = true;
            cursor += 1;
        }
        else if (byte == 0x67 || byte == 0xf0 || byte == 0xf2 || byte == 0xf3 || byte == 0x2e || byte == 0x36 || byte == 0x3e || byte == 0x26 ||
                 byte == 0x64 || byte == 0x65)
        {
            cursor += 1;
        }
        else if (byte >= 0x40 && byte <= 0x4f)
        {
            rex = byte;
            cursor += 1;
        }
        else
        {
            break;
        }
    }
    if (cursor >= end)
    {
        return false;
    }
    u8 opcode = code.pointer[cursor++];
    u8 secondary = 0;
    bool has_modrm = false;
    bool immediate8 = false;
    u32 immediate_size = 0;
    if (opcode == 0x0f)
    {
        if (cursor >= end)
        {
            return false;
        }
        secondary = code.pointer[cursor++];
        if (secondary >= 0x80 && secondary <= 0x8f)
        {
            immediate_size = 4;
        }
        else if (secondary == 0x38 || secondary == 0x3a)
        {
            bool has_immediate8 = secondary == 0x3a;
            if (cursor >= end)
            {
                return false;
            }
            secondary = code.pointer[cursor++];
            has_modrm = true;
            immediate8 = has_immediate8;
        }
        else if (secondary != 0x05 && secondary != 0x06 && secondary != 0x07 && secondary != 0x0b && secondary != 0x31 && secondary != 0x32 &&
                 secondary != 0x33 && secondary != 0x34 && secondary != 0x35 && secondary != 0x37 && secondary != 0x77 && secondary != 0xa2 &&
                 secondary != 0xae)
        {
            has_modrm = true;
        }
    }
    else if (opcode == 0xc4 || opcode == 0xc5 || opcode == 0x62)
    {
        return false;
    }
    else if ((opcode >= 0x70 && opcode <= 0x7f) || opcode == 0xeb || opcode == 0xe0 || opcode == 0xe1 || opcode == 0xe2 || opcode == 0xe3)
    {
        immediate_size = 1;
    }
    else if (opcode == 0xe8 || opcode == 0xe9)
    {
        immediate_size = 4;
        result->call = opcode == 0xe8;
    }
    else if (opcode == 0x68)
    {
        immediate_size = operand16 ? 2 : 4;
    }
    else if (opcode == 0x6a)
    {
        immediate_size = 1;
    }
    else if (opcode >= 0xb0 && opcode <= 0xb7)
    {
        immediate_size = 1;
    }
    else if (opcode >= 0xb8 && opcode <= 0xbf)
    {
        immediate_size = rex & 8 ? 8 : operand16 ? 2 : 4;
    }
    else if (opcode == 0xc2)
    {
        immediate_size = 2;
        result->rsp_change = true;
    }
    else if (opcode == 0xc3 || opcode == 0xcb || opcode == 0xcf)
    {
        result->rsp_change = true;
    }
    else if (opcode == 0xc9)
    {
        result->rsp_change = true;
    }
    else if (opcode >= 0x50 && opcode <= 0x5f)
    {
        result->rsp_change = true;
    }
    else
    {
        has_modrm = opcode == 0x00 || opcode == 0x01 || opcode == 0x02 || opcode == 0x03 || opcode == 0x08 || opcode == 0x09 || opcode == 0x0a ||
                    opcode == 0x0b || opcode == 0x10 || opcode == 0x11 || opcode == 0x12 || opcode == 0x13 || opcode == 0x18 || opcode == 0x19 ||
                    opcode == 0x1a || opcode == 0x1b || opcode == 0x20 || opcode == 0x21 || opcode == 0x22 || opcode == 0x23 || opcode == 0x28 ||
                    opcode == 0x29 || opcode == 0x2a || opcode == 0x2b || opcode == 0x30 || opcode == 0x31 || opcode == 0x32 || opcode == 0x33 ||
                    opcode == 0x39 || opcode == 0x3b || opcode == 0x63 || opcode == 0x69 || opcode == 0x6b || opcode == 0x80 || opcode == 0x81 ||
                    opcode == 0x83 || opcode == 0x84 || opcode == 0x85 || opcode == 0x87 || opcode == 0x88 || opcode == 0x89 || opcode == 0x8a ||
                    opcode == 0x8b || opcode == 0x8d || opcode == 0x8f || opcode == 0xc0 || opcode == 0xc1 || opcode == 0xc6 || opcode == 0xc7 ||
                    opcode == 0xd0 || opcode == 0xd1 || opcode == 0xd2 || opcode == 0xd3 || opcode == 0xf6 || opcode == 0xf7 || opcode == 0xfe ||
                    opcode == 0xff;
        if (opcode == 0x80 || opcode == 0xc0 || opcode == 0xc1 || opcode == 0xc6)
        {
            immediate8 = true;
        }
        else if (opcode == 0x81 || opcode == 0xc7 || opcode == 0x69)
        {
            immediate_size = operand16 ? 2 : 4;
        }
        else if (opcode == 0x83 || opcode == 0x6b)
        {
            immediate8 = true;
        }
    }
    CodegenTestX64Modrm modrm = {0};
    if (has_modrm)
    {
        if (!codegen_test_x64_parse_modrm(code, cursor, end, rex, &modrm))
        {
            return false;
        }
        cursor += modrm.length;
        if (modrm.rsp_memory && modrm.displacement < 0)
        {
            return false;
        }
        if (opcode == 0x0f)
        {
            result->stack_store = modrm.rsp_memory && (secondary == 0x11 || secondary == 0x29 || secondary == 0x7f);
            result->stack_store_end = result->stack_store ? (u32)modrm.displacement + 16 : 0;
        }
        else
        {
            result->stack_store = modrm.rsp_memory && codegen_test_x64_modrm_rm_writes(opcode);
            u32 store_size = rex & 8 ? 8 : operand16 ? 2 : 4;
            if (opcode == 0x88 || opcode == 0xc6)
            {
                store_size = 1;
            }
            result->stack_store_end = result->stack_store ? (u32)modrm.displacement + store_size : 0;
            if (opcode == 0xff && modrm.reg == 2)
            {
                result->call = true;
                result->indirect_call = true;
            }
            if (opcode == 0xff && modrm.reg == 6)
            {
                result->rsp_change = true;
            }
            if (opcode == 0x8f && modrm.reg == 0)
            {
                result->rsp_change = true;
            }
            if (modrm.mod == 3 && modrm.rm == 4 && codegen_test_x64_modrm_rm_writes(opcode))
            {
                result->rsp_change = true;
            }
            if (modrm.mod == 3 && modrm.reg == 4 && codegen_test_x64_modrm_reg_writes(opcode))
            {
                result->rsp_change = true;
            }
            if (opcode == 0x8d && modrm.memory && modrm.reg == 4)
            {
                result->rsp_change = true;
                if (modrm.mod != 0 && modrm.rm == 5)
                {
                    result->lea_rsp_frame = true;
                    result->lea_rsp_displacement = modrm.displacement;
                }
            }
            if ((opcode == 0x81 || opcode == 0x83) && (rex & 8) && modrm.mod == 3 && modrm.rm == 4 && (modrm.reg == 0 || modrm.reg == 5))
            {
                result->add_rsp = modrm.reg == 0;
                result->rsp_change = true;
            }
        }
        if (opcode == 0xf6 && modrm.reg == 0)
        {
            immediate8 = true;
        }
        if (opcode == 0xf7 && modrm.reg == 0)
        {
            immediate_size = operand16 ? 2 : 4;
        }
    }
    if (immediate8)
    {
        immediate_size = 1;
    }
    if (cursor + immediate_size > end)
    {
        return false;
    }
    if (result->rsp_change && (opcode == 0x81 || opcode == 0x83) && modrm.mod == 3 && modrm.rm == 4 && (modrm.reg == 0 || modrm.reg == 5))
    {
        s32 immediate = 0;
        if (immediate_size == 1)
        {
            immediate = (s8)code.pointer[cursor];
        }
        else if (immediate_size == sizeof(immediate))
        {
            memcpy(&immediate, code.pointer + cursor, sizeof(immediate));
        }
        else
        {
            return false;
        }
        result->rsp_adjust = modrm.reg == 0 ? immediate : -immediate;
    }
    cursor += immediate_size;
    result->length = (u32)(cursor - offset);
    return result->length != 0;
}

CodegenTestX64BodyScan codegen_test_x64_scan_body(ByteSlice code, u64 start, u64 end, u32 allocation,
                                                                      u32 frame_restore_displacement)
{
    CodegenTestX64BodyScan result = {.valid = true};
    for (u64 offset = start; offset < end;)
    {
        CodegenTestX64Instruction instruction = {0};
        if (!codegen_test_x64_decode_instruction(code, offset, end, &instruction))
        {
            result.valid = false;
            break;
        }
        if (instruction.stack_store)
        {
            result.has_stack_store = true;
            result.maximum_stack_store_end = BUSTER_MAX(result.maximum_stack_store_end, instruction.stack_store_end);
            result.valid &= instruction.stack_store_end <= allocation;
        }
        result.has_call |= instruction.call;
        result.has_indirect_call |= instruction.indirect_call;
        if (instruction.rsp_change && !instruction.call)
        {
            if (instruction.add_rsp && instruction.rsp_adjust >= 0 && (u64)instruction.rsp_adjust == allocation &&
                offset + instruction.length + 2 <= end && code.pointer[offset + instruction.length] == 0x5d &&
                code.pointer[offset + instruction.length + 1] == 0xc3)
            {
                offset += instruction.length + 2;
                continue;
            }
            if (instruction.lea_rsp_frame && frame_restore_displacement != UINT32_MAX && instruction.lea_rsp_displacement >= 0 &&
                (u64)instruction.lea_rsp_displacement == frame_restore_displacement && offset + instruction.length + 2 <= end &&
                code.pointer[offset + instruction.length] == 0x5d && code.pointer[offset + instruction.length + 1] == 0xc3)
            {
                offset += instruction.length + 2;
                continue;
            }
            if (!instruction.add_rsp && allocation == 0 && code.pointer[offset] == 0x5d && offset + 1 < end && code.pointer[offset + 1] == 0xc3)
            {
                offset += instruction.length + 1;
                continue;
            }
            result.valid = false;
        }
        offset += instruction.length;
    }
    return result;
}

UnitTestResult codegen_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u8 negative_rsp_store_bytes[] = {0x48, 0x89, 0x44, 0x24, 0xf8, 0x5d, 0xc3};
    CodegenTestX64BodyScan negative_rsp_store_scan =
        codegen_test_x64_scan_body((ByteSlice){.pointer = negative_rsp_store_bytes, .length = sizeof(negative_rsp_store_bytes)}, 0,
                                   sizeof(negative_rsp_store_bytes), 16, UINT32_MAX);
    BUSTER_TEST(arguments, !negative_rsp_store_scan.valid);
    u8 zero_frame_epilog_bytes[] = {0x5d, 0xc3};
    CodegenTestX64BodyScan exact_zero_frame_epilog_scan =
        codegen_test_x64_scan_body((ByteSlice){.pointer = zero_frame_epilog_bytes, .length = sizeof(zero_frame_epilog_bytes)}, 0,
                                   sizeof(zero_frame_epilog_bytes), 0, UINT32_MAX);
    BUSTER_TEST(arguments, exact_zero_frame_epilog_scan.valid);
    CodegenTestX64BodyScan nonzero_frame_pop_epilog_scan =
        codegen_test_x64_scan_body((ByteSlice){.pointer = zero_frame_epilog_bytes, .length = sizeof(zero_frame_epilog_bytes)}, 0,
                                   sizeof(zero_frame_epilog_bytes), 16, UINT32_MAX);
    BUSTER_TEST(arguments, !nonzero_frame_pop_epilog_scan.valid);
    CodegenTestX64BodyScan truncated_zero_frame_epilog_scan =
        codegen_test_x64_scan_body((ByteSlice){.pointer = zero_frame_epilog_bytes, .length = sizeof(zero_frame_epilog_bytes)}, 0, 1, 0, UINT32_MAX);
    BUSTER_TEST(arguments, !truncated_zero_frame_epilog_scan.valid);
    u8 exact_frame_epilog_bytes[] = {0x48, 0x83, 0xc4, 0x10, 0x5d, 0xc3};
    CodegenTestX64BodyScan exact_frame_epilog_scan =
        codegen_test_x64_scan_body((ByteSlice){.pointer = exact_frame_epilog_bytes, .length = sizeof(exact_frame_epilog_bytes)}, 0,
                                   sizeof(exact_frame_epilog_bytes), 16, UINT32_MAX);
    BUSTER_TEST(arguments, exact_frame_epilog_scan.valid);
    u8 exact_frame_lea_epilog_bytes[] = {0x48, 0x8d, 0x65, 0x10, 0x5d, 0xc3};
    CodegenTestX64BodyScan exact_frame_lea_epilog_scan =
        codegen_test_x64_scan_body((ByteSlice){.pointer = exact_frame_lea_epilog_bytes, .length = sizeof(exact_frame_lea_epilog_bytes)}, 0,
                                   sizeof(exact_frame_lea_epilog_bytes), 16, 16);
    BUSTER_TEST(arguments, exact_frame_lea_epilog_scan.valid);
    u8 wrong_frame_lea_epilog_bytes[] = {0x48, 0x8d, 0x65, 0x08, 0x5d, 0xc3};
    CodegenTestX64BodyScan wrong_frame_lea_epilog_scan =
        codegen_test_x64_scan_body((ByteSlice){.pointer = wrong_frame_lea_epilog_bytes, .length = sizeof(wrong_frame_lea_epilog_bytes)}, 0,
                                   sizeof(wrong_frame_lea_epilog_bytes), 16, 16);
    BUSTER_TEST(arguments, !wrong_frame_lea_epilog_scan.valid);
    u8 non_frame_lea_epilog_bytes[] = {0x48, 0x8d, 0x60, 0x10, 0x5d, 0xc3};
    CodegenTestX64BodyScan non_frame_lea_epilog_scan =
        codegen_test_x64_scan_body((ByteSlice){.pointer = non_frame_lea_epilog_bytes, .length = sizeof(non_frame_lea_epilog_bytes)}, 0,
                                   sizeof(non_frame_lea_epilog_bytes), 16, 16);
    BUSTER_TEST(arguments, !non_frame_lea_epilog_scan.valid);
    u8 under_restore_epilog_bytes[] = {0x48, 0x83, 0xc4, 0x08, 0x5d, 0xc3};
    CodegenTestX64BodyScan under_restore_epilog_scan =
        codegen_test_x64_scan_body((ByteSlice){.pointer = under_restore_epilog_bytes, .length = sizeof(under_restore_epilog_bytes)}, 0,
                                   sizeof(under_restore_epilog_bytes), 16, UINT32_MAX);
    BUSTER_TEST(arguments, !under_restore_epilog_scan.valid);
    u8 over_restore_epilog_bytes[] = {0x48, 0x83, 0xc4, 0x18, 0x5d, 0xc3};
    CodegenTestX64BodyScan over_restore_epilog_scan =
        codegen_test_x64_scan_body((ByteSlice){.pointer = over_restore_epilog_bytes, .length = sizeof(over_restore_epilog_bytes)}, 0,
                                   sizeof(over_restore_epilog_bytes), 16, UINT32_MAX);
    BUSTER_TEST(arguments, !over_restore_epilog_scan.valid);
    u8 adjacent_rsp_adjust_bytes[] = {0x5d, 0xc3, 0x48, 0x83, 0xec, 0x08};
    CodegenTestX64BodyScan adjacent_rsp_adjust_scan =
        codegen_test_x64_scan_body((ByteSlice){.pointer = adjacent_rsp_adjust_bytes, .length = sizeof(adjacent_rsp_adjust_bytes)}, 0,
                                   sizeof(adjacent_rsp_adjust_bytes), 0, UINT32_MAX);
    BUSTER_TEST(arguments, !adjacent_rsp_adjust_scan.valid);
    // Line rows must stop at the capacity of the array they are recorded
    // into: rows are appended while code is emitted, so running past the end
    // corrupts the arena allocations that follow and changes the code.
    CodegenLineEntry line_rows[3] = {0};
    u32 line_row_count = 0;
    for (u32 line_index = 0; line_index < 8; line_index += 1)
    {
        codegen_record_line(line_rows, &line_row_count, 2, line_index * 4, 0, line_index + 1, 1);
    }
    BUSTER_TEST(arguments, line_row_count == 2);
    BUSTER_TEST(arguments, line_rows[2].line == 0 && line_rows[2].code_offset == 0);
    u8 large_frame_operation_bytes[256] = {0};
    CodegenBuffer large_frame_operation = {
        .bytes = large_frame_operation_bytes,
        .capacity = sizeof(large_frame_operation_bytes),
    };
    BUSTER_TEST(arguments, codegen_canonical_a64_frame_memory_operation(&large_frame_operation, 9, 40000, 1, false, false));
    BUSTER_TEST(arguments, codegen_canonical_a64_frame_memory_operation(&large_frame_operation, 9, 40001, 1, true, false));
    a64_emit_load_pointer_offset(&large_frame_operation, 9, 28, 40004, 4);
    a64_emit_store_pointer_offset(&large_frame_operation, 9, 28, 40008, 4);
    BUSTER_TEST(arguments, large_frame_operation.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, large_frame_operation.count > 8);
    u32 large_stack_address_words[6] = {0};
    CodegenBuffer large_stack_address = {
        .bytes = (u8*)large_stack_address_words,
        .capacity = sizeof(large_stack_address_words),
    };
    codegen_canonical_a64_base_address(&large_stack_address, 16, 31, 40000);
    u32 expected_large_stack_address[] = {
        0xd2800000 | (40000u << 5) | 16, 0xf2a00010, 0xf2c00010, 0xf2e00010, 0x910003f1, 0x8b100230,
    };
    BUSTER_TEST(arguments, large_stack_address.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, large_stack_address.count == sizeof(expected_large_stack_address));
    BUSTER_TEST(arguments, !memcmp(large_stack_address_words, expected_large_stack_address, sizeof(expected_large_stack_address)));
    u32 unsigned_remainder_divide = codegen_canonical_a64_remainder_divide_instruction(false, false);
    BUSTER_TEST(arguments, ((unsigned_remainder_divide >> 5) & 31) == 9);
    BUSTER_TEST(arguments, ((unsigned_remainder_divide >> 16) & 31) == 10);
    BUSTER_TEST(arguments, (unsigned_remainder_divide & 31) == 11);
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);
    String8 source_parts[] = {S8("type CodegenPair = struct\n"
                                 "{\n"
                                 "    left: s32,\n"
                                 "    right: s32,\n"
                                 "}\n"
                                 "type CodegenNumber = union\n"
                                 "{\n"
                                 "    signed_value: s32,\n"
                                 "    unsigned_value: u32,\n"
                                 "}\n"
                                 "type CodegenChoice = enum\n"
                                 "{\n"
                                 "    first,\n"
                                 "    second,\n"
                                 "}\n"
                                 "type CodegenAbiPair = struct\n"
                                 "{\n"
                                 "    left: s64,\n"
                                 "    right: s64,\n"
                                 "}\n"
                                 "type CodegenAbiMixed = struct\n"
                                 "{\n"
                                 "    value: f64,\n"
                                 "    count: s64,\n"
                                 "}\n"
                                 "type CodegenAbiLarge = struct\n"
                                 "{\n"
                                 "    first: s64,\n"
                                 "    second: s64,\n"
                                 "    third: s64,\n"
                                 "}\n"
                                 "type CodegenFloat8 = vector[8]f32\n"
                                 "type CodegenFloat16 = vector[16]f32\n"
                                 "code arithmetic : fn (left: s64, right: s64) s64\n"
                                 "{\n"
                                 "    data value: s64 = left * 3;\n"
                                 "    if (left < right)\n"
                                 "    {\n"
                                 "        value += right;\n"
                                 "    }\n"
                                 "    else\n"
                                 "    {\n"
                                 "        value -= right;\n"
                                 "    }\n"
                                 "    return value;\n"
                                 "}\n"
                                 "code float_arithmetic : fn (left: f64, right: f64) f64\n"
                                 "{\n"
                                 "    return -left * 2.0 + right;\n"
                                 "}\n"),
                              S8("code vector_arithmetic : fn () s32\n"
                                 "{\n"
                                 "    data left: vector[4]f32 = [ 1.0, 2.0, 3.0, 4.0 ];\n"
                                 "    data right: vector[4]f32 = [ 4.0, 3.0, 2.0, 1.0 ];\n"
                                 "    data sum: vector[4]f32 = left + right;\n"
                                 "    data negated: vector[4]f32 = -sum;\n"
                                 "    return @cast(negated[0]);\n"
                                 "}\n"
                                 "code vector_identity : fn (value: vector[4]f32) vector[4]f32\n"
                                 "{\n"
                                 "    return value;\n"
                                 "}\n"
                                 "code string_literal_value[export] : fn () s32\n"
                                 "{\n"
                                 "    data greeting = \"hello\";\n"
                                 "    return @cast(greeting[1]);\n"
                                 "}\n"
                                 "code vector_integer_arithmetic : fn () s32\n"
                                 "{\n"
                                 "    data left: vector[4]s32 = [ 1, 2, 3, 4 ];\n"
                                 "    data right: vector[4]s32 = [ 4, 3, 2, 1 ];\n"
                                 "    data masked: vector[4]s32 = (left + right) & [ 7, 7, 7, 7 ];\n"
                                 "    return masked[0];\n"
                                 "}\n"),
                              S8("code vector_float_comparison : fn () u32\n"
                                 "{\n"
                                 "    data left: vector[4]f32 = [ 1.0, 5.0, -3.0, 8.0 ];\n"
                                 "    data right: vector[4]f32 = [ 2.0, 4.0, -3.0, 9.0 ];\n"
                                 "    data mask: vector[4]u32 = left < right;\n"
                                 "    return mask[0];\n"
                                 "}\n"
                                 "code vector_integer_comparison : fn () u32\n"
                                 "{\n"
                                 "    data left: vector[4]s32 = [ 1, 5, -3, 8 ];\n"
                                 "    data right: vector[4]s32 = [ 2, 4, -3, 9 ];\n"
                                 "    data mask: vector[4]u32 = left > right;\n"
                                 "    return mask[1];\n"
                                 "}\n"
                                 "code vector_256_arithmetic : fn () s32\n"
                                 "{\n"
                                 "    data left: CodegenFloat8 = [ 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0 ];\n"
                                 "    data right: CodegenFloat8 = [ 8.0, 7.0, 6.0, 5.0, 4.0, 3.0, 2.0, 1.0 ];\n"
                                 "    data sum: CodegenFloat8 = left + right;\n"
                                 "    data doubled: CodegenFloat8 = sum + right;\n"
                                 "    return @cast(doubled[7]);\n"
                                 "}\n"
                                 "code vector_256_commutative_rhs : fn () s32\n"
                                 "{\n"
                                 "    data left: vector[8]s32 = [ 1, 2, 3, 4, 5, 6, 7, 8 ];\n"
                                 "    data right: vector[8]s32 = [ 8, 7, 6, 5, 4, 3, 2, 1 ];\n"
                                 "    data sum: vector[8]s32 = left + right;\n"
                                 "    data doubled: vector[8]s32 = right + sum;\n"
                                 "    return doubled[7];\n"
                                 "}\n"
                                 "code vector_256_identity : fn (value: CodegenFloat8) CodegenFloat8\n"
                                 "{\n"
                                 "    return value;\n"
                                 "}\n"
                                 "code vector_512_arithmetic : fn () s32\n"
                                 "{\n"
                                 "    data left: vector[16]s32 = [ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 ];\n"
                                 "    data right: vector[16]s32 = [ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 ];\n"
                                 "    data sum: vector[16]s32 = left + right;\n"
                                 "    return sum[15];\n"
                                 "}\n"
                                 "code vector_512_identity : fn (value: CodegenFloat16) CodegenFloat16\n"
                                 "{\n"
                                 "    return value;\n"
                                 "}\n"),
                              S8("code pointer_arithmetic : fn () s64\n"
                                 "{\n"
                                 "    data value: s64 = 4;\n"
                                 "    data pointer: &s64 = &value;\n"
                                 "    pointer.& += 3;\n"
                                 "    return value;\n"
                                 "}\n"
                                 "code straight_arithmetic : fn (left: s64, right: s64) s64\n"
                                 "{\n"
                                 "    return left * 3 + right;\n"
                                 "}\n"
                                 "code register_pressure : fn (a: s64, b: s64, c: s64, d: s64, e: s64, f: s64, g: s64, h: s64, i: s64) s64\n"
                                 "{\n"
                                 "    return a + b + c + d + e + f + g + h + i;\n"
                                 "}\n"
                                 "code abi_pair_sum : fn (pair: CodegenAbiPair) s64\n"
                                 "{\n"
                                 "    return pair.left + pair.right;\n"
                                 "}\n"
                                 "code abi_pair_make : fn (left: s64, right: s64) CodegenAbiPair\n"
                                 "{\n"
                                 "    return { .left = left, .right = right };\n"
                                 "}\n"
                                 "code abi_pair_round_trip : fn () s64\n"
                                 "{\n"
                                 "    data pair: CodegenAbiPair = abi_pair_make(19, 23);\n"
                                 "    return abi_pair_sum(pair);\n"
                                 "}\n"
                                 "code abi_mixed_sum : fn (mixed: CodegenAbiMixed) f64\n"
                                 "{\n"
                                 "    return mixed.value + @cast(mixed.count);\n"
                                 "}\n"
                                 "code abi_mixed_round_trip : fn () f64\n"
                                 "{\n"
                                 "    data mixed: CodegenAbiMixed = { .value = 1.5, .count = 2 };\n"
                                 "    return abi_mixed_sum(mixed);\n"
                                 "}\n"
                                 "code abi_large_make : fn (first: s64, second: s64, third: s64) CodegenAbiLarge\n"
                                 "{\n"
                                 "    return { .first = first, .second = second, .third = third };\n"
                                 "}\n"
                                 "code abi_large_sum : fn (large: CodegenAbiLarge) s64\n"
                                 "{\n"
                                 "    return large.first + large.second + large.third;\n"
                                 "}\n"
                                 "code abi_large_round_trip : fn () s64\n"
                                 "{\n"
                                 "    data large: CodegenAbiLarge = abi_large_make(5, 7, 11);\n"
                                 "    return abi_large_sum(large);\n"
                                 "}\n"
                                 "code abi_large_merge : fn (left: CodegenAbiLarge, right: CodegenAbiLarge) CodegenAbiLarge\n"
                                 "{\n"
                                 "    return { .first = left.first + right.first, .second = left.second + right.second, .third = left.third + right.third };\n"
                                 "}\n"
                                 "code abi_large_merge_call : fn () s64\n"
                                 "{\n"
                                 "    data left: CodegenAbiLarge = { .first = 1, .second = 2, .third = 3 };\n"
                                 "    data right: CodegenAbiLarge = { .first = 4, .second = 5, .third = 6 };\n"
                                 "    data merged: CodegenAbiLarge = abi_large_merge(left, right);\n"
                                 "    return merged.first;\n"
                                 "}\n"
                                 "code range_sum : fn () s32\n"
                                 "{\n"
                                 "    data total: s32 = 0;\n"
                                 "    for (data value: s32 = 0 .. 4)\n"
                                 "    {\n"
                                 "        total += value;\n"
                                 "    }\n"
                                 "    for (data value: s32 = @reverse(0 .. 4))\n"
                                 "    {\n"
                                 "        total += value;\n"
                                 "    }\n"
                                 "    return total;\n"
                                 "}\n"
                                 "code union_value : fn () s32\n"
                                 "{\n"
                                 "    data number: CodegenNumber = { .signed_value = 17 };\n"
                                 "    return number.signed_value;\n"
                                 "}\n"
                                 "code aggregate_sum : fn () s32\n"
                                 "{\n"
                                 "    data values: [3]s32 = [ 2, 3, 4 ];\n"
                                 "    data selected: []s32 = values[1..];\n"
                                 "    data pair: CodegenPair = { .left = 2, .right = selected[1] };\n"
                                 "    pair.left += selected[0];\n"
                                 "    return pair.left + pair.right;\n"
                                 "}\n"
                                 "code collection_sum : fn () s32\n"
                                 "{\n"
                                 "    data values: [3]s32 = [ 2, 3, 4 ];\n"
                                 "    data total: s32 = 0;\n"
                                 "    for (data value: s32 = values[..])\n"
                                 "    {\n"
                                 "        total += value;\n"
                                 "    }\n"
                                 "    for (data value: s32 = @reverse(values[..]))\n"
                                 "    {\n"
                                 "        total += value;\n"
                                 "    }\n"
                                 "    return total;\n"
                                 "}\n"
                                 "code add_one : fn (value: s64) s64\n"
                                 "{\n"
                                 "    return value + 1;\n"
                                 "}\n"
                                 "code call_chain : fn (value: s64) s64\n"
                                 "{\n"
                                 "    return add_one(value) * 2;\n"
                                 "}\n"
                                 "code sum_seven : fn (a: s64, b: s64, c: s64, d: s64, e: s64, f: s64, g: s64) s64\n"
                                 "{\n"
                                 "    return a + b + c + d + e + f + g;\n"
                                 "}\n"
                                 "code call_many : fn () s64\n"
                                 "{\n"
                                 "    return sum_seven(1, 2, 3, 4, 5, 6, 7);\n"
                                 "}\n"
                                 "code integer_to_float : fn (value: s32) f64\n"
                                 "{\n"
                                 "    return @cast(value);\n"
                                 "}\n"
                                 "code float_to_integer : fn (value: f64) s64\n"
                                 "{\n"
                                 "    return @cast(value);\n"
                                 "}\n"
                                 "code choose : fn (value: CodegenChoice) s64\n"
                                 "{\n"
                                 "    switch (value)\n"
                                 "    {\n"
                                 "        .first => { return 11; },\n"
                                 "        else => { return 22; },\n"
                                 "    }\n"
                                 "}\n"
                                 "code variadic_sum : fn (first: s64, ...) s64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data copy = @va_copy(&arguments);\n"
                                 "    data second: s64 = @va_arg(&copy, s64);\n"
                                 "    data third: s64 = @va_arg(&copy, s64);\n"
                                 "    @va_end(&copy);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return first + second + third;\n"
                                 "}\n"
                                 "code variadic_call : fn () s64\n"
                                 "{\n"
                                 "    return variadic_sum(10, 20, 12);\n"
                                 "}\n"),
                              S8("code variadic_float : fn (first: s64, ...) f64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data value: f64 = @va_arg(&arguments, f64);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return value;\n"
                                 "}\n"
                                 "code variadic_float_call : fn () f64\n"
                                 "{\n"
                                 "    data value: f32 = 5.25;\n"
                                 "    return variadic_float(0, value);\n"
                                 "}\n"
                                 "code variadic_promoted : fn (...) s64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data value: s32 = @va_arg(&arguments, s32);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return @cast(value);\n"
                                 "}\n"
                                 "code variadic_promoted_call : fn () s64\n"
                                 "{\n"
                                 "    data value: u8 = 42;\n"
                                 "    return variadic_promoted(value);\n"
                                 "}\n"
                                 "code variadic_pair : fn (...) s64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data value: CodegenAbiPair = @va_arg(&arguments, CodegenAbiPair);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return value.left + value.right;\n"
                                 "}\n"
                                 "code variadic_pair_call : fn () s64\n"
                                 "{\n"
                                 "    data value: CodegenAbiPair = { .left = 19, .right = 23 };\n"
                                 "    return variadic_pair(value);\n"
                                 "}\n"
                                 "code variadic_mixed : fn (...) f64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data value: CodegenAbiMixed = @va_arg(&arguments, CodegenAbiMixed);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return value.value + @cast(value.count);\n"
                                 "}\n"
                                 "code variadic_mixed_call : fn () f64\n"
                                 "{\n"
                                 "    data value: CodegenAbiMixed = { .value = 2.25, .count = 3 };\n"
                                 "    return variadic_mixed(value);\n"
                                 "}\n"
                                 "code variadic_large : fn (...) s64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data value: CodegenAbiLarge = @va_arg(&arguments, CodegenAbiLarge);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return value.first + value.second + value.third;\n"
                                 "}\n"
                                 "code variadic_large_call : fn () s64\n"
                                 "{\n"
                                 "    data value: CodegenAbiLarge = { .first = 7, .second = 11, .third = 13 };\n"
                                 "    return variadic_large(value);\n"
                                 "}\n"),
                              S8("type CodegenAbiHfa = struct\n"
                                 "{\n"
                                 "    first: f64,\n"
                                 "    second: f64,\n"
                                 "}\n"
                                 "code abi_exhaust_float : fn (a: f64, b: f64, c: f64, d: f64, e: f64, f: f64, g: f64, pair: CodegenAbiHfa, tail: f64) f64\n"
                                 "{\n"
                                 "    return a + pair.first + tail;\n"
                                 "}\n"
                                 "code abi_exhaust_integer : fn (a: s64, b: s64, c: s64, d: s64, e: s64, f: s64, g: s64, pair: CodegenAbiPair, tail: s64) s64\n"
                                 "{\n"
                                 "    return a + pair.left + tail;\n"
                                 "}\n"
                                 "code variadic_fixed_float : fn (first: f64, ...) f64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    @va_end(&arguments);\n"
                                 "    return first;\n"
                                 "}\n"
                                 "code variadic_fixed_float_call : fn () f64\n"
                                 "{\n"
                                 "    return variadic_fixed_float(3.5, 1);\n"
                                 "}\n"
                                 "code variadic_large_stack : fn (a: s64, b: s64, c: s64, d: s64, e: s64, f: s64, g: s64, h: s64, ...) s64\n"
                                 "{\n"
                                 "    data arguments = @va_start();\n"
                                 "    data value: CodegenAbiLarge = @va_arg(&arguments, CodegenAbiLarge);\n"
                                 "    @va_end(&arguments);\n"
                                 "    return value.first + value.second + value.third;\n"
                                 "}\n"
                                 "code variadic_large_stack_call : fn () s64\n"
                                 "{\n"
                                 "    data value: CodegenAbiLarge = { .first = 7, .second = 11, .third = 13 };\n"
                                 "    return variadic_large_stack(0, 1, 2, 3, 4, 5, 6, 7, value);\n"
                                 "}\n")};
    String8 source = string_join_arena(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(source_parts), false);
    TokenizerResult tokens = tokenize(arguments->arena, source.pointer, source.length);
    ParserResult parser = parser_parse(arguments->arena, expression_arena, source, tokens);
    BUSTER_TEST(arguments, tokens.error_count == 0);
    BUSTER_TEST(arguments, parser.diagnostic_count == 0);
    AnalysisSourceInput input = {
        .path = S8("codegen-x86-64.bbb"),
        .parser = &parser,
    };
    AnalysisResult analysis = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 800}, S8("codegen-x86-64"), &input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &analysis);
    IrModule module = ir_analyze_and_generate_module(arguments->arena, &analysis);
    BUSTER_TEST(arguments, analysis.diagnostic_count == 0);
    AnalysisEntity* entity = codegen_test_entity_find(&analysis, S8("arithmetic"));
    BUSTER_TEST(arguments, entity != 0);
    IrFunction* function = entity ? codegen_test_function_find(&module, entity->id) : 0;
    BUSTER_TEST(arguments, function != 0);
    Target target = target_native;
    target.cpu_arch = CPU_ARCH_X86_64;
    Target baseline_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_LINUX,
    };
    Target avx2_target = baseline_target;
    avx2_target.cpu_features_explicit = true;
    avx2_target.cpu_features = target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2}, 3);
    Target avx512f_target = avx2_target;
    avx512f_target.cpu_features = target_cpu_features_add(avx512f_target.cpu_features, TARGET_CPU_FEATURE_X86_AVX512F);
    Target avx10_target = avx2_target;
    avx10_target.cpu_features = target_cpu_features_union(avx10_target.cpu_features, target_cpu_features_from_array((TargetCpuFeature const[]){
        TARGET_CPU_FEATURE_X86_AVX512F, TARGET_CPU_FEATURE_X86_AVX512VL, TARGET_CPU_FEATURE_X86_AVX512BW,
        TARGET_CPU_FEATURE_X86_AVX10_1, TARGET_CPU_FEATURE_X86_AVX10_2, TARGET_CPU_FEATURE_X86_AVX10_512,
        TARGET_CPU_FEATURE_X86_APX}, 7));
    BUSTER_TEST(arguments, target_vector_register_size(baseline_target) == 16);
    BUSTER_TEST(arguments, target_vector_register_size(avx2_target) == 32);
    BUSTER_TEST(arguments, target_vector_register_size(avx10_target) == 64);
    BUSTER_TEST(arguments, target_cpu_feature_has(avx10_target, TARGET_CPU_FEATURE_X86_APX));
    BUSTER_TEST(arguments, x64_target_supports_native_vector(avx512f_target, 64, 32, true));
    BUSTER_TEST(arguments, !x64_target_supports_native_vector(avx512f_target, 64, 8, true));
    BUSTER_TEST(arguments, x64_target_supports_native_vector(avx10_target, 64, 8, true));
    BUSTER_TEST(arguments, target_vector_register_size((Target){
                               .cpu_arch = CPU_ARCH_AARCH64,
                               .cpu_model = CPU_MODEL_BASELINE,
                           }) == 16);
    u8 scalar_packet_bytes[13] = {0};
    CodegenBuffer scalar_packet_buffer = {
        .bytes = scalar_packet_bytes,
        .capacity = sizeof(scalar_packet_bytes),
    };
    codegen_test_emit_scalar(&scalar_packet_buffer, 1, 0xa5);
    codegen_test_emit_scalar(&scalar_packet_buffer, 4, UINT32_C(0x44332211));
    codegen_test_emit_scalar(&scalar_packet_buffer, 8, UINT64_C(0xccbbaa9988776655));
    static u8 const expected_scalar_packets[] = {
        0xa5, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc,
    };
    BUSTER_TEST(arguments, scalar_packet_buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, scalar_packet_buffer.count == sizeof(scalar_packet_bytes));
    BUSTER_TEST(arguments, !memcmp(scalar_packet_bytes, expected_scalar_packets, sizeof(expected_scalar_packets)));

    u8 short_packet_bytes[8];
    memset(short_packet_bytes, 0x5a, sizeof(short_packet_bytes));
    CodegenBuffer short_packet_buffer = {
        .bytes = short_packet_bytes,
        .capacity = sizeof(short_packet_bytes) - 1,
    };
    codegen_test_emit_scalar(&short_packet_buffer, 8, UINT64_MAX);
    BUSTER_TEST(arguments, short_packet_buffer.error == CODEGEN_ERROR_CAPACITY);
    BUSTER_TEST(arguments, short_packet_buffer.count == 0);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(short_packet_bytes); index += 1)
    {
        BUSTER_TEST(arguments, short_packet_bytes[index] == 0x5a);
    }

    u8 boundary_packet_bytes[8];
    memset(boundary_packet_bytes, 0x3c, sizeof(boundary_packet_bytes));
    CodegenBuffer boundary_packet_buffer = {
        .bytes = boundary_packet_bytes,
        .count = 3,
        .capacity = 7,
    };
    codegen_test_emit_scalar(&boundary_packet_buffer, 4, UINT32_C(0x04030201));
    BUSTER_TEST(arguments, boundary_packet_buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, boundary_packet_buffer.count == 7);
    BUSTER_TEST(arguments, boundary_packet_bytes[3] == 1 && boundary_packet_bytes[4] == 2 && boundary_packet_bytes[5] == 3 &&
                               boundary_packet_bytes[6] == 4 && boundary_packet_bytes[7] == 0x3c);
    codegen_test_emit_scalar(&boundary_packet_buffer, 1, 0xff);
    BUSTER_TEST(arguments, boundary_packet_buffer.error == CODEGEN_ERROR_CAPACITY);
    BUSTER_TEST(arguments, boundary_packet_buffer.count == 7 && boundary_packet_bytes[7] == 0x3c);

    CodegenBuffer zero_capacity_packet_buffer = {0};
    codegen_test_emit_scalar(&zero_capacity_packet_buffer, 1, 0xff);
    BUSTER_TEST(arguments, zero_capacity_packet_buffer.error == CODEGEN_ERROR_CAPACITY);
    BUSTER_TEST(arguments, zero_capacity_packet_buffer.count == 0);

    CodegenBuffer overflow_packet_buffer = {
        .count = UINT64_MAX - 1,
        .capacity = UINT64_MAX,
    };
    codegen_test_emit_scalar(&overflow_packet_buffer, 4, UINT32_MAX);
    BUSTER_TEST(arguments, overflow_packet_buffer.error == CODEGEN_ERROR_CAPACITY);
    BUSTER_TEST(arguments, overflow_packet_buffer.count == UINT64_MAX - 1);

    u8 x64_stack_adjust_bytes[32] = {0};
    CodegenBuffer x64_stack_adjust_buffer = {
        .bytes = x64_stack_adjust_bytes,
        .capacity = sizeof(x64_stack_adjust_bytes),
    };
    codegen_canonical_x64_adjust_stack(&x64_stack_adjust_buffer, 4097, true);
    codegen_canonical_x64_adjust_stack(&x64_stack_adjust_buffer, 4097, false);
    static u8 const expected_x64_stack_adjust[] = {
        0x48, 0x81, 0xec, 0x00, 0x10, 0x00, 0x00, 0xf6, 0x04, 0x24, 0x00, 0x48, 0x83,
        0xec, 0x01, 0xf6, 0x04, 0x24, 0x00, 0x48, 0x81, 0xc4, 0x01, 0x10, 0x00, 0x00,
    };
    BUSTER_TEST(arguments, x64_stack_adjust_buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, x64_stack_adjust_buffer.count == sizeof(expected_x64_stack_adjust));
    BUSTER_TEST(arguments, !memcmp(x64_stack_adjust_bytes, expected_x64_stack_adjust, sizeof(expected_x64_stack_adjust)));
    u8 x64_wide_vector_bytes[48] = {0};
    X64Builder x64_wide_vector_builder = {
        .buffer =
            {
                .bytes = x64_wide_vector_bytes,
                .capacity = sizeof(x64_wide_vector_bytes),
            },
    };
    x64_emit_vector_native_memory(&x64_wide_vector_builder, false, 32, X64_REGISTER_R8);
    x64_emit_vector_native_binary_operation(&x64_wide_vector_builder, 0x66, 0xfe, 32, X64_REGISTER_R9);
    x64_emit_vector_native_memory(&x64_wide_vector_builder, true, 32, X64_REGISTER_R10);
    x64_emit_vector_native_memory(&x64_wide_vector_builder, false, 64, X64_REGISTER_R8);
    x64_emit_vector_native_binary_operation(&x64_wide_vector_builder, 0x66, 0xfe, 64, X64_REGISTER_R9);
    x64_emit_vector_native_memory(&x64_wide_vector_builder, true, 64, X64_REGISTER_R10);
    x64_emit_vector_native_binary_operation(&x64_wide_vector_builder, 0x66, 0xfc, 64, X64_REGISTER_R9);
    static u8 const expected_x64_wide_vector[] = {
        0xc4, 0xc1, 0x7c, 0x10, 0x00, 0xc4, 0xc1, 0x7d, 0xfe, 0x01, 0xc4, 0xc1, 0x7c, 0x11, 0x02, 0x62, 0xd1, 0x7c, 0x48, 0x10,
        0x00, 0x62, 0xd1, 0x7d, 0x48, 0xfe, 0x01, 0x62, 0xd1, 0x7c, 0x48, 0x11, 0x02, 0x62, 0xd1, 0x7d, 0x48, 0xfc, 0x01,
    };
    BUSTER_TEST(arguments, x64_wide_vector_builder.buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, x64_wide_vector_builder.buffer.count == sizeof(expected_x64_wide_vector));
    BUSTER_TEST(arguments, !memcmp(x64_wide_vector_bytes, expected_x64_wide_vector, sizeof(expected_x64_wide_vector)));
    u8 x64_vzeroupper_bytes[4] = {0};
    X64Builder x64_vzeroupper_builder = {
        .buffer =
            {
                .bytes = x64_vzeroupper_bytes,
                .capacity = sizeof(x64_vzeroupper_bytes),
            },
    };
    x64_emit_vzeroupper(&x64_vzeroupper_builder);
    BUSTER_TEST(arguments, x64_vzeroupper_builder.buffer.count == 0);
    x64_vzeroupper_builder.upper_vector_dirty = true;
    x64_emit_vzeroupper(&x64_vzeroupper_builder);
    x64_emit_vzeroupper(&x64_vzeroupper_builder);
    static u8 const expected_x64_vzeroupper[] = {
        0xc5,
        0xf8,
        0x77,
    };
    BUSTER_TEST(arguments, x64_vzeroupper_builder.buffer.count == sizeof(expected_x64_vzeroupper));
    BUSTER_TEST(arguments, x64_vzeroupper_builder.vzeroupper_count == 1);
    BUSTER_TEST(arguments, !memcmp(x64_vzeroupper_bytes, expected_x64_vzeroupper, sizeof(expected_x64_vzeroupper)));
    u32 aarch64_stack_adjust_words[6] = {0};
    CodegenBuffer aarch64_stack_adjust_buffer = {
        .bytes = (u8*)aarch64_stack_adjust_words,
        .capacity = sizeof(aarch64_stack_adjust_words),
    };
    codegen_canonical_a64_adjust_stack(&aarch64_stack_adjust_buffer, 4081, true);
    codegen_canonical_a64_adjust_stack(&aarch64_stack_adjust_buffer, 4081, false);
    u32 expected_aarch64_stack_adjust[] = {
        0xd10003ff | (4080u << 10), 0xf90003ff, 0xd10003ff | (1u << 10), 0xf90003ff, 0x910003ff | (4080u << 10), 0x910003ff | (1u << 10),
    };
    BUSTER_TEST(arguments, aarch64_stack_adjust_buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_stack_adjust_buffer.count == sizeof(expected_aarch64_stack_adjust));
    BUSTER_TEST(arguments, !memcmp(aarch64_stack_adjust_words, expected_aarch64_stack_adjust, sizeof(expected_aarch64_stack_adjust)));
    u32 aarch64_aggregate_entry_words[5] = {0};
    CodegenBuffer aarch64_aggregate_entry_buffer = {
        .bytes = (u8*)aarch64_aggregate_entry_words,
        .capacity = sizeof(aarch64_aggregate_entry_words),
    };
    u32 aarch64_aggregate_offsets[] = {64};
    a64_emit_initialize_aggregate_result(&aarch64_aggregate_entry_buffer, aarch64_aggregate_offsets, (IrValueId){.value = 0});
    a64_emit_copy_memory_registers(&aarch64_aggregate_entry_buffer, 17, 16, 15, 8);
    BUSTER_TEST(arguments, aarch64_aggregate_entry_buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_aggregate_entry_buffer.count == sizeof(aarch64_aggregate_entry_words));
    BUSTER_TEST(arguments, aarch64_aggregate_entry_words[0] == 0x910003f0);
    BUSTER_TEST(arguments, aarch64_aggregate_entry_words[1] == 0x91010210);
    BUSTER_TEST(arguments, aarch64_aggregate_entry_words[2] == 0xf90003f0);
    BUSTER_TEST(arguments, aarch64_aggregate_entry_words[3] == 0xf940020f);
    BUSTER_TEST(arguments, aarch64_aggregate_entry_words[4] == 0xf900022f);
    u32 aarch64_float_snapshot_words[2] = {0};
    CodegenBuffer aarch64_float_snapshot_buffer = {
        .bytes = (u8*)aarch64_float_snapshot_words,
        .capacity = sizeof(aarch64_float_snapshot_words),
    };
    a64_emit_float_store_offset(&aarch64_float_snapshot_buffer, 3, 32, 16);
    a64_emit_float_load_offset(&aarch64_float_snapshot_buffer, 16, 48, 8);
    BUSTER_TEST(arguments, aarch64_float_snapshot_buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_float_snapshot_buffer.count == sizeof(aarch64_float_snapshot_words));
    BUSTER_TEST(arguments, aarch64_float_snapshot_words[0] == 0x3d800be3);
    BUSTER_TEST(arguments, aarch64_float_snapshot_words[1] == 0xfd401bf0);

    // x87 f80 values are encoded from the two IR immediates, never through
    // host long double.  Check both the exact semantic bytes and the six-byte
    // canonical zero padding in the internal sixteen-byte slot.
    u8 f80_constant_bytes[128] = {0};
    CodegenBuffer f80_constant_buffer = {
        .bytes = f80_constant_bytes,
        .capacity = sizeof(f80_constant_bytes),
    };
    BUSTER_TEST(arguments, codegen_canonical_x64_store_f80_constant(&f80_constant_buffer, -16, UINT64_C(0x0123456789abcdef), 0x7fff));
    u8 f80_constant_expected_prefix[] = {
        0x48, 0xb8, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
        0x48, 0x89, 0x85, 0xf0, 0xff, 0xff, 0xff,
        0xb8, 0xff, 0x7f, 0x00, 0x00,
        0x66, 0x89, 0x85, 0xf8, 0xff, 0xff, 0xff,
    };
    BUSTER_TEST(arguments, f80_constant_buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, f80_constant_buffer.count >= sizeof(f80_constant_expected_prefix));
    BUSTER_TEST(arguments, !memcmp(f80_constant_bytes, f80_constant_expected_prefix, sizeof(f80_constant_expected_prefix)));
    u8 f80_zero_padding_expected[] = {
        0x31, 0xc0, 0x66, 0x89, 0x85, 0xfa, 0xff, 0xff, 0xff, 0x89, 0x85, 0xfc, 0xff, 0xff, 0xff,
    };
    BUSTER_TEST(arguments, f80_constant_buffer.count == sizeof(f80_constant_expected_prefix) + sizeof(f80_zero_padding_expected));
    BUSTER_TEST(arguments, !memcmp(f80_constant_bytes + sizeof(f80_constant_expected_prefix), f80_zero_padding_expected,
                                   sizeof(f80_zero_padding_expected)));
    u8 f80_copy_bytes[128] = {0};
    CodegenBuffer f80_copy_buffer = {
        .bytes = f80_copy_bytes,
        .capacity = sizeof(f80_copy_bytes),
    };
    u32 f80_x87_depth = 0;
    BUSTER_TEST(arguments, codegen_canonical_x64_emit_f80_copy(&f80_copy_buffer, X64_REGISTER_RBP, -16, X64_REGISTER_RBP, -32, &f80_x87_depth));
    u8 f80_copy_expected_prefix[] = {
        0xdb, 0xad, 0xf0, 0xff, 0xff, 0xff,
        0xdb, 0xbd, 0xe0, 0xff, 0xff, 0xff,
    };
    BUSTER_TEST(arguments, f80_copy_buffer.error == CODEGEN_ERROR_NONE && f80_x87_depth == 0);
    BUSTER_TEST(arguments, f80_copy_buffer.count >= sizeof(f80_copy_expected_prefix));
    BUSTER_TEST(arguments, !memcmp(f80_copy_bytes, f80_copy_expected_prefix, sizeof(f80_copy_expected_prefix)));
    u8 f80_extended_padding_bytes[64] = {0};
    CodegenBuffer f80_extended_padding_buffer = {
        .bytes = f80_extended_padding_bytes,
        .capacity = sizeof(f80_extended_padding_bytes),
    };
    codegen_canonical_x64_zero_f80_padding(&f80_extended_padding_buffer, X64_REGISTER_R12, -16);
    u8 f80_extended_padding_expected[] = {
        0x31, 0xc0,
        0x66, 0x41, 0x89, 0x84, 0x24, 0xfa, 0xff, 0xff, 0xff,
        0x41, 0x89, 0x84, 0x24, 0xfc, 0xff, 0xff, 0xff,
    };
    BUSTER_TEST(arguments, f80_extended_padding_buffer.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, f80_extended_padding_buffer.count == sizeof(f80_extended_padding_expected));
    BUSTER_TEST(arguments, !memcmp(f80_extended_padding_bytes, f80_extended_padding_expected, sizeof(f80_extended_padding_expected)));
    IrProgram f80_abi_program = ir_program_initialize(arguments->arena, 0, 1, 0, 0);
    IrTypeId f80_type_id = ir_program_add_type(&f80_abi_program, (IrType){
        .kind = IR_TYPE_FLOAT,
        .bit_width = 80,
        .layout = {
            .size = 16,
            .alignment = 16,
            .resolved = true,
        },
    });
    IrAbiValue f80_argument_abi = ir_type_abi_value(&f80_abi_program, f80_type_id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
    IrAbiValue f80_result_abi = ir_type_abi_value(&f80_abi_program, f80_type_id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
    BUSTER_TEST(arguments, f80_argument_abi.memory && !f80_argument_abi.indirect && f80_argument_abi.part_count == 1 &&
                           f80_argument_abi.parts[0].abi_class == IR_ABI_CLASS_MEMORY && f80_argument_abi.parts[0].size == 16);
    BUSTER_TEST(arguments, !f80_result_abi.memory && !f80_result_abi.indirect && f80_result_abi.part_count == 2 &&
                           f80_result_abi.parts[0].abi_class == IR_ABI_CLASS_X87 && f80_result_abi.parts[1].abi_class == IR_ABI_CLASS_X87_UP);
    IrProgram f80_call_program = ir_program_initialize(arguments->arena, 1, 1024, 1, 0);
    IrTypeId f80_call_type = ir_program_add_type(&f80_call_program, (IrType){
        .kind = IR_TYPE_FLOAT,
        .bit_width = 80,
        .layout = {.size = 16, .alignment = 16, .abi_class = IR_ABI_CLASS_FLOAT, .resolved = true},
    });
    IrTypeId f80_function_type = ir_program_add_type(&f80_call_program, (IrType){
        .kind = IR_TYPE_FUNCTION,
        .return_type = f80_call_type,
        .parameter_types = &f80_call_type,
        .parameter_count = 1,
        .calling_convention = IR_CALLING_CONVENTION_C,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true},
    });
    IrFunction* f80_call_function = ir_module_add_function(arguments->arena, f80_call_program.modules, (IrFunction){
                                                                                                          .canonical_type = f80_function_type,
                                                                                                          .state = IR_FUNCTION_LOWERED,
                                                                                                      });
    IrValueId f80_callee_value = ir_function_add_value(arguments->arena, f80_call_function, (IrValue){
                                                                                                  .canonical_type = f80_function_type,
                                                                                                  .category = IR_VALUE_VALUE,
                                                                                              });
    IrValueId f80_argument_value = ir_function_add_value(arguments->arena, f80_call_function, (IrValue){
                                                                                                     .canonical_type = f80_call_type,
                                                                                                     .category = IR_VALUE_VALUE,
                                                                                                 });
    IrValueId f80_call_operands[] = {f80_callee_value, f80_argument_value};
    IrInstruction f80_call_instruction = {
        .opcode = IR_OPCODE_CALL,
        .canonical_type = f80_call_type,
        .operands = f80_call_operands,
        .operand_count = BUSTER_ARRAY_LENGTH(f80_call_operands),
        .result = IR_VALUE_ID_INVALID,
    };
    CodegenCanonicalCallLayout f80_call_layout = {0};
    Target f80_call_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_LINUX,
    };
    CodegenError f80_call_layout_error = codegen_canonical_x64_call_layout(arguments->arena, &f80_call_program, f80_call_function,
                                                                              &f80_call_instruction, CODEGEN_ABI_X86_64_SYSTEM_V,
                                                                              f80_call_target, &f80_call_layout);
    BUSTER_TEST(arguments, f80_call_layout_error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, f80_call_layout.argument_count == 1 && f80_call_layout.arguments[0].on_stack &&
                           f80_call_layout.arguments[0].stack_part_count == 2 && f80_call_layout.arguments[0].stack_offset == 0 &&
                           f80_call_layout.simulated_float_registers == 0 && f80_call_layout.stack_alignment == 16);
    // The cache is a bounded per-module allocation.  A caller-provided arena
    // that cannot hold one state byte and one DFS frame per type must return a
    // capacity error before the layout walker can classify an unchecked value.
    while (f80_call_program.types.count < f80_call_program.types.capacity)
    {
        IrTypeId narrow_type = ir_program_add_type(&f80_call_program, (IrType){
            .kind = IR_TYPE_INTEGER,
            .bit_width = 32,
            .layout = {.size = 4, .alignment = 4, .resolved = true},
        });
        BUSTER_TEST(arguments, narrow_type.value != IR_ID_UNDERLYING_INVALID);
        if (narrow_type.value == IR_ID_UNDERLYING_INVALID)
        {
            break;
        }
    }
    Arena* tiny_f80_cache_arena = arena_create((ArenaCreation){
        .reserved_size = BUSTER_KB(4),
        .initial_size = BUSTER_KB(4),
        .flags.no_pool = true,
    });
    BUSTER_TEST(arguments, tiny_f80_cache_arena != 0);
    if (tiny_f80_cache_arena)
    {
        CodegenCanonicalCallLayout tiny_f80_cache_layout = {0};
        CodegenError tiny_f80_cache_error = codegen_canonical_x64_call_layout(tiny_f80_cache_arena, &f80_call_program, f80_call_function,
                                                                                &f80_call_instruction, CODEGEN_ABI_X86_64_SYSTEM_V, f80_call_target,
                                                                                &tiny_f80_cache_layout);
        BUSTER_TEST(arguments, tiny_f80_cache_error == CODEGEN_ERROR_CAPACITY);
        BUSTER_TEST(arguments, arena_destroy(tiny_f80_cache_arena, 1));
    }

    // contains_f80 is intentionally not sufficient to select fldt/fstpt: the
    // ABI classifier must prove the complete value is one canonical x87
    // payload. Keep malformed and mixed layouts visible to the tests so a
    // future aggregate fast path cannot silently truncate their active bytes.
    IrProgram f80_shape_program = ir_program_initialize(arguments->arena, 0, 8, 0, 0);
    IrTypeId shape_f80_type = ir_program_add_type(&f80_shape_program, (IrType){
        .kind = IR_TYPE_FLOAT,
        .bit_width = 80,
        .layout = {.size = 16, .alignment = 16, .resolved = true},
    });
    IrTypeId shape_i32_type = ir_program_add_type(&f80_shape_program, (IrType){
        .kind = IR_TYPE_INTEGER,
        .bit_width = 32,
        .layout = {.size = 4, .alignment = 4, .resolved = true},
    });
    IrField single_f80_field = {
        .type = shape_f80_type,
        .offset = 0,
    };
    IrTypeId single_f80_struct = ir_program_add_type(&f80_shape_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = &single_f80_field,
        .field_count = 1,
        .layout = {.size = 16, .alignment = 16, .resolved = true},
    });
    IrField mixed_union_fields[] = {
        {.type = shape_f80_type, .offset = 0},
        {.type = shape_i32_type, .offset = 0},
    };
    IrTypeId mixed_union = ir_program_add_type(&f80_shape_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = mixed_union_fields,
        .field_count = BUSTER_ARRAY_LENGTH(mixed_union_fields),
        .layout = {.size = 16, .alignment = 16, .resolved = true},
    });
    IrTypeId f80_array = ir_program_add_type(&f80_shape_program, (IrType){
        .kind = IR_TYPE_ARRAY,
        .element_type = shape_f80_type,
        .element_count = 1,
        .layout = {.size = 16, .alignment = 16, .resolved = true},
    });
    IrField nested_f80_field = {
        .type = single_f80_struct,
        .offset = 0,
    };
    IrTypeId nested_f80_struct = ir_program_add_type(&f80_shape_program, (IrType){
        .kind = IR_TYPE_STRUCT,
        .fields = &nested_f80_field,
        .field_count = 1,
        .layout = {.size = 16, .alignment = 16, .resolved = true},
    });
    IrField same_f80_union_fields[] = {
        {.type = shape_f80_type, .offset = 0},
        {.type = single_f80_struct, .offset = 0},
    };
    IrTypeId same_f80_union = ir_program_add_type(&f80_shape_program, (IrType){
        .kind = IR_TYPE_UNION,
        .fields = same_f80_union_fields,
        .field_count = BUSTER_ARRAY_LENGTH(same_f80_union_fields),
        .layout = {.size = 16, .alignment = 16, .resolved = true},
    });
    IrTypeId malformed_f80 = ir_program_add_type(&f80_shape_program, (IrType){
        .kind = IR_TYPE_FLOAT,
        .bit_width = 80,
        .layout = {.size = 16, .alignment = 8, .resolved = true},
    });
    BUSTER_TEST(arguments, codegen_canonical_x64_type_is_f80_x87_shape(&f80_shape_program, shape_f80_type));
    BUSTER_TEST(arguments, codegen_canonical_x64_type_is_f80_x87_shape(&f80_shape_program, single_f80_struct));
    BUSTER_TEST(arguments, codegen_canonical_x64_type_is_f80_x87_shape(&f80_shape_program, nested_f80_struct));
    BUSTER_TEST(arguments, codegen_canonical_x64_type_contains_f80(&f80_shape_program, mixed_union));
    BUSTER_TEST(arguments, !codegen_canonical_x64_type_is_f80_x87_shape(&f80_shape_program, mixed_union));
    BUSTER_TEST(arguments, codegen_canonical_x64_type_contains_f80(&f80_shape_program, f80_array));
    BUSTER_TEST(arguments, codegen_canonical_x64_type_is_f80_x87_shape(&f80_shape_program, f80_array));
    BUSTER_TEST(arguments, codegen_canonical_x64_type_contains_f80(&f80_shape_program, same_f80_union));
    BUSTER_TEST(arguments, codegen_canonical_x64_type_is_f80_x87_shape(&f80_shape_program, same_f80_union));
    BUSTER_TEST(arguments, codegen_canonical_x64_type_contains_f80(&f80_shape_program, malformed_f80));
    BUSTER_TEST(arguments, !codegen_canonical_x64_type_is_f80_x87_shape(&f80_shape_program, malformed_f80));
    BUSTER_TEST(arguments, !codegen_canonical_x64_type_is_f80(ir_type_from_id(&f80_shape_program.types, malformed_f80)));
    typedef struct CodegenTargetAbiCase
    {
        CpuArch cpu_arch;
        OperatingSystem os;
        CodegenAbi abi;
    } CodegenTargetAbiCase;
    CodegenTargetAbiCase target_abi_cases[] = {
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_LINUX, CODEGEN_ABI_X86_64_SYSTEM_V},
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_MACOS, CODEGEN_ABI_X86_64_SYSTEM_V},
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_WINDOWS, CODEGEN_ABI_X86_64_WINDOWS},
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_UEFI, CODEGEN_ABI_X86_64_WINDOWS},
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_ANDROID, CODEGEN_ABI_X86_64_SYSTEM_V},
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_IOS, CODEGEN_ABI_X86_64_SYSTEM_V},
        {CPU_ARCH_X86_64, OPERATING_SYSTEM_FREESTANDING, CODEGEN_ABI_X86_64_SYSTEM_V},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_LINUX, CODEGEN_ABI_AARCH64_AAPCS64},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_MACOS, CODEGEN_ABI_AARCH64_DARWIN},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_WINDOWS, CODEGEN_ABI_AARCH64_WINDOWS},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_UEFI, CODEGEN_ABI_AARCH64_AAPCS64},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_ANDROID, CODEGEN_ABI_AARCH64_AAPCS64},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_IOS, CODEGEN_ABI_AARCH64_DARWIN},
        {CPU_ARCH_AARCH64, OPERATING_SYSTEM_FREESTANDING, CODEGEN_ABI_AARCH64_AAPCS64},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(target_abi_cases); index += 1)
    {
        CodegenTargetAbiCase* test = target_abi_cases + index;
        BUSTER_TEST(arguments, codegen_abi_for_target((Target){
                                   .cpu_arch = test->cpu_arch,
                                   .os = test->os,
                               }) == test->abi);
    }
    CodegenFunction generated =
        function ? codegen_generate_function(arguments->arena, &analysis, function, target) : (CodegenFunction){.error = CODEGEN_ERROR_INVALID_IR};
    BUSTER_TEST(arguments, generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, generated.code.length > 0);
    BUSTER_TEST(arguments, generated.register_value_count > 0);
    BUSTER_TEST(arguments, generated.spilled_value_count > 0);
    BUSTER_TEST(arguments, generated.descriptor.code_offset == 0);
    BUSTER_TEST(arguments, generated.descriptor.code_size == generated.code.length);
    BUSTER_TEST(arguments, generated.descriptor.prolog_size <= generated.descriptor.code_size);
    BUSTER_TEST(arguments, generated.descriptor.unwind_action_count >= 2);
    if (generated.descriptor.unwind_action_count >= 2)
    {
        CodegenUnwindAction* push = generated.descriptor.unwind_actions;
        CodegenUnwindAction* frame = generated.descriptor.unwind_actions + 1;
        BUSTER_TEST(arguments, push->kind == CODEGEN_UNWIND_ACTION_PUSH_REGISTER && push->register_index == X64_REGISTER_RBP && push->code_offset == 1);
        BUSTER_TEST(arguments,
                    frame->kind == CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER && frame->register_index == X64_REGISTER_RBP && frame->code_offset == 4);
        u32 allocated = 0;
        for (u32 action_index = 2; action_index < generated.descriptor.unwind_action_count; action_index += 1)
        {
            CodegenUnwindAction* action = generated.descriptor.unwind_actions + action_index;
            BUSTER_TEST(arguments, action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK);
            allocated += action->value;
        }
        BUSTER_TEST(arguments, allocated == generated.stack_frame_size);
    }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable executable = codegen_make_executable(generated);
    BUSTER_TEST(arguments, executable.error == CODEGEN_ERROR_NONE);
    if (executable.address)
    {
        CodegenTestFunction2* native = 0;
        BUSTER_CT_CHECK(sizeof(native) == sizeof(executable.address));
        memcpy(&native, &executable.address, sizeof(native));
        u64 first = native(2, 5);
        u64 second = native(7, 3);
        AnalysisResult* analysis_modules[] = {&analysis};
        AnalysisProgram analysis_program = {
            .module_results = analysis_modules,
            .module_count = 1,
        };
        IrProgram ir_program = {
            .modules = &module,
            .module_count = 1,
        };
        IrExecutionArgument first_arguments[] = {
            {.bits = 2},
            {.bits = 5},
        };
        IrExecutionArgument second_arguments[] = {
            {.bits = 7},
            {.bits = 3},
        };
        IrExecutionResult first_interpreted = ir_execute(expression_arena, &analysis_program, &ir_program, entity->id, ANALYSIS_INSTANTIATION_ID_INVALID,
                                                         first_arguments, BUSTER_ARRAY_LENGTH(first_arguments), (IrExecutionOptions){0});
        IrExecutionResult second_interpreted = ir_execute(expression_arena, &analysis_program, &ir_program, entity->id, ANALYSIS_INSTANTIATION_ID_INVALID,
                                                          second_arguments, BUSTER_ARRAY_LENGTH(second_arguments), (IrExecutionOptions){0});
        BUSTER_TEST(arguments, first_interpreted.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, second_interpreted.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, first == first_interpreted.bits);
        BUSTER_TEST(arguments, second == second_interpreted.bits);
        codegen_release_executable(executable);
    }
#endif
    if (function)
    {
        CodegenAbiSignature system_v = codegen_classify_signature(arguments->arena, &analysis, function->type, CODEGEN_ABI_X86_64_SYSTEM_V);
        BUSTER_TEST(arguments, system_v.argument_count == 2);
        BUSTER_TEST(arguments, system_v.arguments != 0);
        if (system_v.arguments && system_v.argument_count >= 2)
        {
            BUSTER_TEST(arguments, system_v.arguments[0].kind == CODEGEN_ABI_LOCATION_INTEGER_REGISTER);
            BUSTER_TEST(arguments, system_v.arguments[0].index == 0);
            BUSTER_TEST(arguments, system_v.arguments[1].index == 1);
        }
    }
    AnalysisEntity* pair_sum_abi_entity = codegen_test_entity_find(&analysis, S8("abi_pair_sum"));
    AnalysisEntity* mixed_sum_abi_entity = codegen_test_entity_find(&analysis, S8("abi_mixed_sum"));
    AnalysisEntity* large_make_abi_entity = codegen_test_entity_find(&analysis, S8("abi_large_make"));
    IrFunction* pair_sum_abi_function = pair_sum_abi_entity ? codegen_test_function_find(&module, pair_sum_abi_entity->id) : 0;
    IrFunction* mixed_sum_abi_function = mixed_sum_abi_entity ? codegen_test_function_find(&module, mixed_sum_abi_entity->id) : 0;
    IrFunction* large_make_abi_function = large_make_abi_entity ? codegen_test_function_find(&module, large_make_abi_entity->id) : 0;
    BUSTER_TEST(arguments, pair_sum_abi_function != 0);
    BUSTER_TEST(arguments, mixed_sum_abi_function != 0);
    BUSTER_TEST(arguments, large_make_abi_function != 0);
    if (pair_sum_abi_function && mixed_sum_abi_function && large_make_abi_function)
    {
        CodegenAbiSignature pair_system_v = codegen_classify_signature(arguments->arena, &analysis, pair_sum_abi_function->type, CODEGEN_ABI_X86_64_SYSTEM_V);
        CodegenAbiSignature pair_windows = codegen_classify_signature(arguments->arena, &analysis, pair_sum_abi_function->type, CODEGEN_ABI_X86_64_WINDOWS);
        CodegenAbiSignature pair_aapcs = codegen_classify_signature(arguments->arena, &analysis, pair_sum_abi_function->type, CODEGEN_ABI_AARCH64_AAPCS64);
        CodegenAbiSignature pair_windows_aarch64 =
            codegen_classify_signature(arguments->arena, &analysis, pair_sum_abi_function->type, CODEGEN_ABI_AARCH64_WINDOWS);
        BUSTER_TEST(arguments, pair_system_v.valid);
        BUSTER_TEST(arguments, pair_system_v.arguments[0].part_count == 2);
        BUSTER_TEST(arguments, pair_system_v.arguments[0].parts[0].index == 0);
        BUSTER_TEST(arguments, pair_system_v.arguments[0].parts[1].index == 1);
        BUSTER_TEST(arguments, pair_windows.valid);
        BUSTER_TEST(arguments, pair_windows.arguments[0].indirect);
        BUSTER_TEST(arguments, pair_windows.arguments[0].indirect_copy_offset >= 32);
        BUSTER_TEST(arguments, pair_aapcs.valid);
        BUSTER_TEST(arguments, pair_aapcs.arguments[0].part_count == 2);
        BUSTER_TEST(arguments, pair_windows_aarch64.valid);
        BUSTER_TEST(arguments, pair_windows_aarch64.arguments[0].part_count == 2);
        CodegenAbiSignature mixed_system_v = codegen_classify_signature(arguments->arena, &analysis, mixed_sum_abi_function->type, CODEGEN_ABI_X86_64_SYSTEM_V);
        BUSTER_TEST(arguments, mixed_system_v.valid);
        BUSTER_TEST(arguments, mixed_system_v.argument_count == 1);
        BUSTER_TEST(arguments, mixed_system_v.arguments != 0);
        if (mixed_system_v.arguments && mixed_system_v.argument_count >= 1)
        {
            BUSTER_TEST(arguments, mixed_system_v.arguments[0].part_count == 2);
        }
        if (mixed_system_v.arguments && mixed_system_v.argument_count >= 1 && mixed_system_v.arguments[0].part_count >= 2)
        {
            BUSTER_TEST(arguments, mixed_system_v.arguments[0].parts[0].kind == CODEGEN_ABI_LOCATION_FLOAT_REGISTER);
            BUSTER_TEST(arguments, mixed_system_v.arguments[0].parts[1].kind == CODEGEN_ABI_LOCATION_INTEGER_REGISTER);
        }
        CodegenAbiSignature large_system_v =
            codegen_classify_signature(arguments->arena, &analysis, large_make_abi_function->type, CODEGEN_ABI_X86_64_SYSTEM_V);
        CodegenAbiSignature large_windows = codegen_classify_signature(arguments->arena, &analysis, large_make_abi_function->type, CODEGEN_ABI_X86_64_WINDOWS);
        CodegenAbiSignature large_aapcs = codegen_classify_signature(arguments->arena, &analysis, large_make_abi_function->type, CODEGEN_ABI_AARCH64_AAPCS64);
        BUSTER_TEST(arguments, large_system_v.result.indirect);
        BUSTER_TEST(arguments, large_system_v.indirect_result_register == 0);
        BUSTER_TEST(arguments, large_system_v.arguments[0].index == 1);
        BUSTER_TEST(arguments, large_windows.result.indirect);
        BUSTER_TEST(arguments, large_windows.indirect_result_register == 0);
        BUSTER_TEST(arguments, large_windows.arguments[0].index == 1);
        BUSTER_TEST(arguments, large_aapcs.result.indirect);
        BUSTER_TEST(arguments, large_aapcs.indirect_result_register == 8);
        BUSTER_TEST(arguments, large_aapcs.arguments[0].index == 0);
    }

    AnalysisEntity* exhaust_float_entity = codegen_test_entity_find(&analysis, S8("abi_exhaust_float"));
    AnalysisEntity* exhaust_integer_entity = codegen_test_entity_find(&analysis, S8("abi_exhaust_integer"));
    IrFunction* exhaust_float_function = exhaust_float_entity ? codegen_test_function_find(&module, exhaust_float_entity->id) : 0;
    IrFunction* exhaust_integer_function = exhaust_integer_entity ? codegen_test_function_find(&module, exhaust_integer_entity->id) : 0;
    BUSTER_TEST(arguments, exhaust_float_function != 0);
    BUSTER_TEST(arguments, exhaust_integer_function != 0);
    if (exhaust_float_function && exhaust_integer_function)
    {
        CodegenAbiSignature exhaust_float = codegen_classify_signature(arguments->arena, &analysis, exhaust_float_function->type, CODEGEN_ABI_AARCH64_AAPCS64);
        CodegenAbiSignature exhaust_integer =
            codegen_classify_signature(arguments->arena, &analysis, exhaust_integer_function->type, CODEGEN_ABI_AARCH64_WINDOWS);
        BUSTER_TEST(arguments, exhaust_float.valid);
        BUSTER_TEST(arguments, exhaust_float.argument_count == 9);
        BUSTER_TEST(arguments, exhaust_float.arguments[7].kind == CODEGEN_ABI_LOCATION_STACK);
        BUSTER_TEST(arguments, exhaust_float.arguments[8].kind == CODEGEN_ABI_LOCATION_STACK);
        BUSTER_TEST(arguments, exhaust_integer.valid);
        BUSTER_TEST(arguments, exhaust_integer.arguments[7].kind == CODEGEN_ABI_LOCATION_STACK);
        BUSTER_TEST(arguments, exhaust_integer.arguments[8].kind == CODEGEN_ABI_LOCATION_STACK);
    }

    AnalysisEntity* variadic_float_abi_entity = codegen_test_entity_find(&analysis, S8("variadic_float"));
    IrFunction* variadic_float_abi_function = variadic_float_abi_entity ? codegen_test_function_find(&module, variadic_float_abi_entity->id) : 0;
    BUSTER_TEST(arguments, variadic_float_abi_function != 0);
    if (variadic_float_abi_function)
    {
        AnalysisTypeId variadic_argument_types[] = {
            analysis.types.builtin.s64_type,
            analysis.types.builtin.f64_type,
        };
        CodegenAbiSignature windows_aarch64_variadic =
            codegen_classify_signature_with_arguments(arguments->arena, &analysis, variadic_float_abi_function->type, variadic_argument_types,
                                                      BUSTER_ARRAY_LENGTH(variadic_argument_types), codegen_target_for_abi(CODEGEN_ABI_AARCH64_WINDOWS));
        BUSTER_TEST(arguments, windows_aarch64_variadic.valid);
        BUSTER_TEST(arguments, windows_aarch64_variadic.argument_count == 2);
        BUSTER_TEST(arguments, windows_aarch64_variadic.arguments[0].kind == CODEGEN_ABI_LOCATION_INTEGER_REGISTER);
        BUSTER_TEST(arguments, windows_aarch64_variadic.arguments[1].kind == CODEGEN_ABI_LOCATION_INTEGER_REGISTER);
        BUSTER_TEST(arguments, windows_aarch64_variadic.arguments[1].index == 1);
    }

    AnalysisType* vector_128 = 0;
    AnalysisType* vector_256 = 0;
    AnalysisType* vector_512 = 0;
    for (u32 type_index = 0; type_index < analysis.types.count; type_index += 1)
    {
        AnalysisType* candidate = analysis.types.types + type_index;
        if (candidate->kind != ANALYSIS_TYPE_VECTOR)
        {
            continue;
        }
        if (candidate->layout.size == 16)
        {
            vector_128 = candidate;
        }
        else if (candidate->layout.size == 32)
        {
            vector_256 = candidate;
        }
        else if (candidate->layout.size == 64)
        {
            vector_512 = candidate;
        }
    }
    BUSTER_TEST(arguments, vector_128 != 0);
    BUSTER_TEST(arguments, vector_256 != 0);
    BUSTER_TEST(arguments, vector_512 != 0);
    if (vector_128 && vector_256 && vector_512)
    {
        AnalysisAbiValue systemv_128 = analysis_abi_value_classify(arguments->arena, &analysis, vector_128->id, ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64, false);
        AnalysisAbiValue systemv_256 = analysis_abi_value_classify(arguments->arena, &analysis, vector_256->id, ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64, false);
        AnalysisAbiValue systemv_512 = analysis_abi_value_classify(arguments->arena, &analysis, vector_512->id, ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64, false);
        BUSTER_TEST(arguments, systemv_128.part_count == 1 && systemv_128.parts[0].abi_class == ANALYSIS_ABI_CLASS_VECTOR && systemv_128.parts[0].size == 16);
        BUSTER_TEST(arguments, systemv_256.part_count == 1 && systemv_256.parts[0].abi_class == ANALYSIS_ABI_CLASS_VECTOR && systemv_256.parts[0].size == 32);
        BUSTER_TEST(arguments, systemv_512.part_count == 1 && systemv_512.parts[0].abi_class == ANALYSIS_ABI_CLASS_VECTOR && systemv_512.parts[0].size == 64);
        AnalysisAbiValue systemv_variadic_256 =
            analysis_abi_value_classify_variadic_argument(arguments->arena, &analysis, vector_256->id, ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64);
        BUSTER_TEST(arguments, systemv_variadic_256.parts[0].location == ANALYSIS_ABI_LOCATION_STACK);
        AnalysisAbiValue windows_128 = analysis_abi_value_classify(arguments->arena, &analysis, vector_128->id, ANALYSIS_ABI_CONVENTION_WIN64_X86_64, false);
        BUSTER_TEST(arguments, windows_128.indirect && windows_128.parts[0].abi_class == ANALYSIS_ABI_CLASS_POINTER);
        AnalysisAbiValue aapcs_128 = analysis_abi_value_classify(arguments->arena, &analysis, vector_128->id, ANALYSIS_ABI_CONVENTION_AAPCS64, false);
        BUSTER_TEST(arguments, !aapcs_128.indirect && aapcs_128.parts[0].abi_class == ANALYSIS_ABI_CLASS_VECTOR);
        AnalysisAbiValue aapcs_256 = analysis_abi_value_classify(arguments->arena, &analysis, vector_256->id, ANALYSIS_ABI_CONVENTION_AAPCS64, false);
        BUSTER_TEST(arguments, aapcs_256.indirect && aapcs_256.parts[0].abi_class == ANALYSIS_ABI_CLASS_POINTER);

        String8 identity_names[] = {
            S8_INITIALIZER("vector_256_identity"),
            S8_INITIALIZER("vector_512_identity"),
        };
        Target identity_split_targets[] = {
            baseline_target,
            avx2_target,
        };
        Target identity_native_targets[] = {
            avx2_target,
            avx10_target,
        };
        for (u32 identity_index = 0; identity_index < BUSTER_ARRAY_LENGTH(identity_names); identity_index += 1)
        {
            AnalysisEntity* identity_entity = codegen_test_entity_find(&analysis, identity_names[identity_index]);
            BUSTER_TEST(arguments, identity_entity != 0);
            if (!identity_entity)
            {
                continue;
            }
            AnalysisTypeId identity_type = analysis.module.semantics[identity_entity->id.index.value].type;
            AnalysisFunctionAbi split_abi = analysis_classify_function_abi(arguments->arena, &analysis, identity_type, identity_split_targets[identity_index]);
            AnalysisFunctionAbi native_abi =
                analysis_classify_function_abi(arguments->arena, &analysis, identity_type, identity_native_targets[identity_index]);
            BUSTER_TEST(arguments, split_abi.result.indirect);
            BUSTER_TEST(arguments, split_abi.arguments[0].parts[0].location == ANALYSIS_ABI_LOCATION_STACK);
            BUSTER_TEST(arguments, !native_abi.result.indirect);
            BUSTER_TEST(arguments, native_abi.result.parts[0].abi_class == ANALYSIS_ABI_CLASS_VECTOR);
            BUSTER_TEST(arguments, native_abi.arguments[0].parts[0].location == ANALYSIS_ABI_LOCATION_REGISTER);
        }
    }

    AnalysisEntity* float_entity = codegen_test_entity_find(&analysis, S8("float_arithmetic"));
    BUSTER_TEST(arguments, float_entity != 0);
    IrFunction* float_function = float_entity ? codegen_test_function_find(&module, float_entity->id) : 0;
    BUSTER_TEST(arguments, float_function != 0);
    CodegenFunction float_generated = float_function ? codegen_generate_function(arguments->arena, &analysis, float_function, target)
                                                     : (CodegenFunction){
                                                           .error = CODEGEN_ERROR_INVALID_IR,
                                                       };
    BUSTER_TEST(arguments, float_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable float_executable = codegen_make_executable(float_generated);
    BUSTER_TEST(arguments, float_executable.error == CODEGEN_ERROR_NONE);
    if (float_executable.address)
    {
        CodegenTestFloatFunction2* native_float = 0;
        BUSTER_CT_CHECK(sizeof(native_float) == sizeof(float_executable.address));
        memcpy(&native_float, &float_executable.address, sizeof(native_float));
        f64 native_value = native_float(3.0, 1.5);
        AnalysisResult* float_analysis_modules[] = {
            &analysis,
        };
        AnalysisProgram float_analysis_program = {
            .module_results = float_analysis_modules,
            .module_count = 1,
        };
        IrProgram float_ir_program = {
            .modules = &module,
            .module_count = 1,
        };
        u64 left_bits = 0;
        u64 right_bits = 0;
        f64 left = 3.0;
        f64 right = 1.5;
        memcpy(&left_bits, &left, sizeof(left_bits));
        memcpy(&right_bits, &right, sizeof(right_bits));
        IrExecutionArgument float_arguments[] = {
            {.bits = left_bits},
            {.bits = right_bits},
        };
        IrExecutionResult interpreted_float =
            ir_execute(expression_arena, &float_analysis_program, &float_ir_program, float_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID, float_arguments,
                       BUSTER_ARRAY_LENGTH(float_arguments), (IrExecutionOptions){0});
        f64 interpreted_value = 0.0;
        memcpy(&interpreted_value, &interpreted_float.bits, sizeof(interpreted_value));
        BUSTER_TEST(arguments, interpreted_float.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, native_value == interpreted_value);
        codegen_release_executable(float_executable);
    }
#endif

    AnalysisEntity* pointer_entity = codegen_test_entity_find(&analysis, S8("pointer_arithmetic"));
    BUSTER_TEST(arguments, pointer_entity != 0);
    IrFunction* pointer_function = pointer_entity ? codegen_test_function_find(&module, pointer_entity->id) : 0;
    BUSTER_TEST(arguments, pointer_function != 0);
    CodegenFunction pointer_generated = pointer_function ? codegen_generate_function(arguments->arena, &analysis, pointer_function, target)
                                                         : (CodegenFunction){
                                                               .error = CODEGEN_ERROR_INVALID_IR,
                                                           };
    BUSTER_TEST(arguments, pointer_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable pointer_executable = codegen_make_executable(pointer_generated);
    BUSTER_TEST(arguments, pointer_executable.error == CODEGEN_ERROR_NONE);
    if (pointer_executable.address)
    {
        CodegenTestFunction0* native_pointer = 0;
        BUSTER_CT_CHECK(sizeof(native_pointer) == sizeof(pointer_executable.address));
        memcpy(&native_pointer, &pointer_executable.address, sizeof(native_pointer));
        BUSTER_TEST(arguments, native_pointer() == 7);
        codegen_release_executable(pointer_executable);
    }
#endif

    AnalysisEntity* straight_entity = codegen_test_entity_find(&analysis, S8("straight_arithmetic"));
    BUSTER_TEST(arguments, straight_entity != 0);
    IrFunction* straight_function = straight_entity ? codegen_test_function_find(&module, straight_entity->id) : 0;
    BUSTER_TEST(arguments, straight_function != 0);
    Target aarch64_target = target;
    aarch64_target.cpu_arch = CPU_ARCH_AARCH64;
    aarch64_target.cpu_features_explicit = true;
    aarch64_target.cpu_features = target_cpu_features_singleton(TARGET_CPU_FEATURE_AARCH64_NEON);
    BUSTER_TEST(arguments, codegen_debug_frame_offset(40, target, true, 32) == -40);
    BUSTER_TEST(arguments, codegen_debug_frame_offset(40, aarch64_target, false, 32) == 8);
    CodegenFunction aarch64_generated = straight_function ? codegen_generate_function(arguments->arena, &analysis, straight_function, aarch64_target)
                                                          : (CodegenFunction){
                                                                .error = CODEGEN_ERROR_INVALID_IR,
                                                            };
    BUSTER_TEST(arguments, aarch64_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_generated.code.length >= 4);
    BUSTER_TEST(arguments, aarch64_generated.register_value_count > 0);
    BUSTER_TEST(arguments, aarch64_generated.descriptor.code_size == aarch64_generated.code.length);
    BUSTER_TEST(arguments, aarch64_generated.descriptor.unwind_action_count >= 4);
    BUSTER_TEST(arguments, aarch64_generated.descriptor.epilog_count != 0 && aarch64_generated.descriptor.epilog_offsets != 0);
    for (u32 epilog_index = 0; epilog_index < aarch64_generated.descriptor.epilog_count; epilog_index += 1)
    {
        BUSTER_TEST(arguments, aarch64_generated.descriptor.epilog_offsets[epilog_index] >= aarch64_generated.descriptor.prolog_size &&
                                   aarch64_generated.descriptor.epilog_offsets[epilog_index] < aarch64_generated.descriptor.code_size);
    }
    if (aarch64_generated.descriptor.unwind_action_count >= 4)
    {
        CodegenUnwindAction* actions = aarch64_generated.descriptor.unwind_actions;
        BUSTER_TEST(arguments, actions[0].kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK && actions[0].value == 16 && actions[0].code_offset == 4);
        BUSTER_TEST(arguments,
                    actions[1].kind == CODEGEN_UNWIND_ACTION_SAVE_REGISTER && actions[1].register_index == 29 && actions[1].value == 0);
        BUSTER_TEST(arguments,
                    actions[2].kind == CODEGEN_UNWIND_ACTION_SAVE_REGISTER && actions[2].register_index == 30 && actions[2].value == 8);
        BUSTER_TEST(arguments, actions[3].kind == CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER && actions[3].register_index == 29 && actions[3].code_offset == 8);
        u32 allocated = 0;
        for (u32 action_index = 0; action_index < aarch64_generated.descriptor.unwind_action_count; action_index += 1)
        {
            CodegenUnwindAction* action = actions + action_index;
            allocated += action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK ? action->value : 0;
        }
        BUSTER_TEST(arguments, allocated == aarch64_generated.stack_frame_size + 16);
    }
    AnalysisEntity* pressure_entity = codegen_test_entity_find(&analysis, S8("register_pressure"));
    IrFunction* pressure_function = pressure_entity ? codegen_test_function_find(&module, pressure_entity->id) : 0;
    BUSTER_TEST(arguments, pressure_function != 0);
    CodegenFunction x86_64_pressure_generated = pressure_function ? codegen_generate_function(arguments->arena, &analysis, pressure_function, target)
                                                                  : (CodegenFunction){
                                                                        .error = CODEGEN_ERROR_INVALID_IR,
                                                                    };
    CodegenFunction aarch64_pressure_generated = pressure_function ? codegen_generate_function(arguments->arena, &analysis, pressure_function, aarch64_target)
                                                                   : (CodegenFunction){
                                                                         .error = CODEGEN_ERROR_INVALID_IR,
                                                                     };
    BUSTER_TEST(arguments, x86_64_pressure_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, x86_64_pressure_generated.register_value_count > 0);
    BUSTER_TEST(arguments, x86_64_pressure_generated.spilled_value_count > 0);
    BUSTER_TEST(arguments, aarch64_pressure_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_pressure_generated.register_value_count > 0);
    BUSTER_TEST(arguments, aarch64_pressure_generated.spilled_value_count > 0);
    CodegenFunction aarch64_cfg_generated = function ? codegen_generate_function(arguments->arena, &analysis, function, aarch64_target)
                                                     : (CodegenFunction){
                                                           .error = CODEGEN_ERROR_INVALID_IR,
                                                       };
    BUSTER_TEST(arguments, aarch64_cfg_generated.error == CODEGEN_ERROR_NONE);
    CodegenFunction aarch64_float_generated = float_function ? codegen_generate_function(arguments->arena, &analysis, float_function, aarch64_target)
                                                             : (CodegenFunction){
                                                                   .error = CODEGEN_ERROR_INVALID_IR,
                                                               };
    BUSTER_TEST(arguments, aarch64_float_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_executable = codegen_make_executable(aarch64_generated);
    BUSTER_TEST(arguments, aarch64_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_executable.address)
    {
        CodegenTestFunction2* native_aarch64 = 0;
        BUSTER_CT_CHECK(sizeof(native_aarch64) == sizeof(aarch64_executable.address));
        memcpy(&native_aarch64, &aarch64_executable.address, sizeof(native_aarch64));
        BUSTER_TEST(arguments, native_aarch64(2, 5) == 11);
        codegen_release_executable(aarch64_executable);
    }
#endif

    AnalysisEntity* range_entity = codegen_test_entity_find(&analysis, S8("range_sum"));
    BUSTER_TEST(arguments, range_entity != 0);
    IrFunction* range_function = range_entity ? codegen_test_function_find(&module, range_entity->id) : 0;
    BUSTER_TEST(arguments, range_function != 0);
    CodegenFunction range_generated = range_function ? codegen_generate_function(arguments->arena, &analysis, range_function, target)
                                                     : (CodegenFunction){
                                                           .error = CODEGEN_ERROR_INVALID_IR,
                                                       };
    BUSTER_TEST(arguments, range_generated.error == CODEGEN_ERROR_NONE);
    CodegenFunction aarch64_range_generated = range_function ? codegen_generate_function(arguments->arena, &analysis, range_function, aarch64_target)
                                                             : (CodegenFunction){
                                                                   .error = CODEGEN_ERROR_INVALID_IR,
                                                               };
    BUSTER_TEST(arguments, aarch64_range_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_range_executable = codegen_make_executable(aarch64_range_generated);
    BUSTER_TEST(arguments, aarch64_range_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_range_executable.address)
    {
        CodegenTestFunction0* native_aarch64_range = 0;
        memcpy(&native_aarch64_range, &aarch64_range_executable.address, sizeof(native_aarch64_range));
        BUSTER_TEST(arguments, native_aarch64_range() == 12);
        codegen_release_executable(aarch64_range_executable);
    }
#endif
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable range_executable = codegen_make_executable(range_generated);
    BUSTER_TEST(arguments, range_executable.error == CODEGEN_ERROR_NONE);
    if (range_executable.address)
    {
        CodegenTestFunction0* native_range = 0;
        BUSTER_CT_CHECK(sizeof(native_range) == sizeof(range_executable.address));
        memcpy(&native_range, &range_executable.address, sizeof(native_range));
        BUSTER_TEST(arguments, native_range() == 12);
        codegen_release_executable(range_executable);
    }
#endif
    AnalysisEntity* aggregate_entity = codegen_test_entity_find(&analysis, S8("aggregate_sum"));
    BUSTER_TEST(arguments, aggregate_entity != 0);
    IrFunction* aggregate_function = aggregate_entity ? codegen_test_function_find(&module, aggregate_entity->id) : 0;
    BUSTER_TEST(arguments, aggregate_function != 0);
    CodegenFunction aggregate_generated = aggregate_function ? codegen_generate_function(arguments->arena, &analysis, aggregate_function, target)
                                                             : (CodegenFunction){
                                                                   .error = CODEGEN_ERROR_INVALID_IR,
                                                               };
    BUSTER_TEST(arguments, aggregate_generated.error == CODEGEN_ERROR_NONE);
    CodegenFunction aarch64_aggregate_generated = aggregate_function
                                                      ? codegen_generate_function(arguments->arena, &analysis, aggregate_function, aarch64_target)
                                                      : (CodegenFunction){
                                                            .error = CODEGEN_ERROR_INVALID_IR,
                                                        };
    BUSTER_TEST(arguments, aarch64_aggregate_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_aggregate_executable = codegen_make_executable(aarch64_aggregate_generated);
    BUSTER_TEST(arguments, aarch64_aggregate_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_aggregate_executable.address)
    {
        CodegenTestFunction0* native_aarch64_aggregate = 0;
        memcpy(&native_aarch64_aggregate, &aarch64_aggregate_executable.address, sizeof(native_aarch64_aggregate));
        BUSTER_TEST(arguments, native_aarch64_aggregate() == 9);
        codegen_release_executable(aarch64_aggregate_executable);
    }
#endif
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable aggregate_executable = codegen_make_executable(aggregate_generated);
    BUSTER_TEST(arguments, aggregate_executable.error == CODEGEN_ERROR_NONE);
    if (aggregate_executable.address)
    {
        CodegenTestFunction0* native_aggregate = 0;
        BUSTER_CT_CHECK(sizeof(native_aggregate) == sizeof(aggregate_executable.address));
        memcpy(&native_aggregate, &aggregate_executable.address, sizeof(native_aggregate));
        BUSTER_TEST(arguments, native_aggregate() == 9);
        codegen_release_executable(aggregate_executable);
    }
#endif
    AnalysisEntity* union_entity = codegen_test_entity_find(&analysis, S8("union_value"));
    IrFunction* union_function = union_entity ? codegen_test_function_find(&module, union_entity->id) : 0;
    BUSTER_TEST(arguments, union_function != 0);
    CodegenFunction union_generated = union_function ? codegen_generate_function(arguments->arena, &analysis, union_function, target)
                                                     : (CodegenFunction){
                                                           .error = CODEGEN_ERROR_INVALID_IR,
                                                       };
    CodegenFunction aarch64_union_generated = union_function ? codegen_generate_function(arguments->arena, &analysis, union_function, aarch64_target)
                                                             : (CodegenFunction){
                                                                   .error = CODEGEN_ERROR_INVALID_IR,
                                                               };
    BUSTER_TEST(arguments, union_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_union_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable union_executable = codegen_make_executable(union_generated);
    BUSTER_TEST(arguments, union_executable.error == CODEGEN_ERROR_NONE);
    if (union_executable.address)
    {
        CodegenTestFunction0* native_union = 0;
        memcpy(&native_union, &union_executable.address, sizeof(native_union));
        BUSTER_TEST(arguments, native_union() == 17);
        codegen_release_executable(union_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_union_executable = codegen_make_executable(aarch64_union_generated);
    BUSTER_TEST(arguments, aarch64_union_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_union_executable.address)
    {
        CodegenTestFunction0* native_aarch64_union = 0;
        memcpy(&native_aarch64_union, &aarch64_union_executable.address, sizeof(native_aarch64_union));
        BUSTER_TEST(arguments, native_aarch64_union() == 17);
        codegen_release_executable(aarch64_union_executable);
    }
#endif
    AnalysisEntity* vector_entity = codegen_test_entity_find(&analysis, S8("vector_arithmetic"));
    IrFunction* vector_function = vector_entity ? codegen_test_function_find(&module, vector_entity->id) : 0;
    BUSTER_TEST(arguments, vector_function != 0);
    CodegenFunction vector_generated = vector_function ? codegen_generate_function(arguments->arena, &analysis, vector_function, target)
                                                       : (CodegenFunction){
                                                             .error = CODEGEN_ERROR_INVALID_IR,
                                                         };
    CodegenFunction aarch64_vector_generated = vector_function ? codegen_generate_function(arguments->arena, &analysis, vector_function, aarch64_target)
                                                               : (CodegenFunction){
                                                                     .error = CODEGEN_ERROR_INVALID_IR,
                                                                 };
    Target aarch64_without_neon = aarch64_target;
    aarch64_without_neon.cpu_features = target_cpu_features_empty();
    CodegenFunction aarch64_without_neon_generated = vector_function
                                                         ? codegen_generate_function(arguments->arena, &analysis, vector_function, aarch64_without_neon)
                                                         : (CodegenFunction){
                                                               .error = CODEGEN_ERROR_INVALID_IR,
                                                           };
    BUSTER_TEST(arguments, vector_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_vector_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_without_neon_generated.error == CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION);
    BUSTER_TEST(arguments, vector_generated.register_value_count > 0);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable vector_executable = codegen_make_executable(vector_generated);
    BUSTER_TEST(arguments, vector_executable.error == CODEGEN_ERROR_NONE);
    if (vector_executable.address)
    {
        CodegenTestFunction0* native_vector = 0;
        memcpy(&native_vector, &vector_executable.address, sizeof(native_vector));
        BUSTER_TEST(arguments, native_vector() == UINT64_MAX - 4);
        codegen_release_executable(vector_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_vector_executable = codegen_make_executable(aarch64_vector_generated);
    BUSTER_TEST(arguments, aarch64_vector_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_vector_executable.address)
    {
        CodegenTestFunction0* native_aarch64_vector = 0;
        memcpy(&native_aarch64_vector, &aarch64_vector_executable.address, sizeof(native_aarch64_vector));
        BUSTER_TEST(arguments, native_aarch64_vector() == UINT64_MAX - 4);
        codegen_release_executable(aarch64_vector_executable);
    }
#endif
    AnalysisEntity* vector_integer_entity = codegen_test_entity_find(&analysis, S8("vector_integer_arithmetic"));
    IrFunction* vector_integer_function = vector_integer_entity ? codegen_test_function_find(&module, vector_integer_entity->id) : 0;
    BUSTER_TEST(arguments, vector_integer_function != 0);
    CodegenFunction vector_integer_generated = vector_integer_function ? codegen_generate_function(arguments->arena, &analysis, vector_integer_function, target)
                                                                       : (CodegenFunction){
                                                                             .error = CODEGEN_ERROR_INVALID_IR,
                                                                         };
    CodegenFunction aarch64_vector_integer_generated = vector_integer_function
                                                           ? codegen_generate_function(arguments->arena, &analysis, vector_integer_function, aarch64_target)
                                                           : (CodegenFunction){
                                                                 .error = CODEGEN_ERROR_INVALID_IR,
                                                             };
    BUSTER_TEST(arguments, vector_integer_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_vector_integer_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable vector_integer_executable = codegen_make_executable(vector_integer_generated);
    BUSTER_TEST(arguments, vector_integer_executable.error == CODEGEN_ERROR_NONE);
    if (vector_integer_executable.address)
    {
        CodegenTestFunction0* native_vector_integer = 0;
        memcpy(&native_vector_integer, &vector_integer_executable.address, sizeof(native_vector_integer));
        BUSTER_TEST(arguments, native_vector_integer() == 5);
        codegen_release_executable(vector_integer_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_vector_integer_executable = codegen_make_executable(aarch64_vector_integer_generated);
    BUSTER_TEST(arguments, aarch64_vector_integer_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_vector_integer_executable.address)
    {
        CodegenTestFunction0* native_aarch64_vector_integer = 0;
        memcpy(&native_aarch64_vector_integer, &aarch64_vector_integer_executable.address, sizeof(native_aarch64_vector_integer));
        BUSTER_TEST(arguments, native_aarch64_vector_integer() == 5);
        codegen_release_executable(aarch64_vector_integer_executable);
    }
#endif
    String8 vector_comparison_names[] = {
        S8_INITIALIZER("vector_float_comparison"),
        S8_INITIALIZER("vector_integer_comparison"),
    };
    for (u32 comparison_index = 0; comparison_index < 2; comparison_index += 1)
    {
        AnalysisEntity* comparison_entity = codegen_test_entity_find(&analysis, vector_comparison_names[comparison_index]);
        IrFunction* comparison_function = comparison_entity ? codegen_test_function_find(&module, comparison_entity->id) : 0;
        BUSTER_TEST(arguments, comparison_function != 0);
        CodegenFunction comparison_generated = comparison_function ? codegen_generate_function(arguments->arena, &analysis, comparison_function, target)
                                                                   : (CodegenFunction){
                                                                         .error = CODEGEN_ERROR_INVALID_IR,
                                                                     };
        CodegenFunction aarch64_comparison_generated = comparison_function
                                                           ? codegen_generate_function(arguments->arena, &analysis, comparison_function, aarch64_target)
                                                           : (CodegenFunction){
                                                                 .error = CODEGEN_ERROR_INVALID_IR,
                                                             };
        BUSTER_TEST(arguments, comparison_generated.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, aarch64_comparison_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
        CodegenExecutable comparison_executable = codegen_make_executable(comparison_generated);
        BUSTER_TEST(arguments, comparison_executable.error == CODEGEN_ERROR_NONE);
        if (comparison_executable.address)
        {
            CodegenTestFunction0* native_comparison = 0;
            memcpy(&native_comparison, &comparison_executable.address, sizeof(native_comparison));
            BUSTER_TEST(arguments, native_comparison() == UINT32_MAX);
            codegen_release_executable(comparison_executable);
        }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
        CodegenExecutable aarch64_comparison_executable = codegen_make_executable(aarch64_comparison_generated);
        BUSTER_TEST(arguments, aarch64_comparison_executable.error == CODEGEN_ERROR_NONE);
        if (aarch64_comparison_executable.address)
        {
            CodegenTestFunction0* native_aarch64_comparison = 0;
            memcpy(&native_aarch64_comparison, &aarch64_comparison_executable.address, sizeof(native_aarch64_comparison));
            BUSTER_TEST(arguments, native_aarch64_comparison() == UINT32_MAX);
            codegen_release_executable(aarch64_comparison_executable);
        }
#endif
    }
    AnalysisEntity* string_literal_entity = codegen_test_entity_find(&analysis, S8("string_literal_value"));
    IrFunction* string_literal_function = string_literal_entity ? codegen_test_function_find(&module, string_literal_entity->id) : 0;
    BUSTER_TEST(arguments, string_literal_function != 0);
    CodegenFunction string_literal_generated = string_literal_function ? codegen_generate_function(arguments->arena, &analysis, string_literal_function, target)
                                                                       : (CodegenFunction){
                                                                             .error = CODEGEN_ERROR_INVALID_IR,
                                                                         };
    CodegenFunction aarch64_string_literal_generated = string_literal_function
                                                           ? codegen_generate_function(arguments->arena, &analysis, string_literal_function, aarch64_target)
                                                           : (CodegenFunction){
                                                                 .error = CODEGEN_ERROR_INVALID_IR,
                                                             };
    BUSTER_TEST(arguments, string_literal_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, string_literal_generated.read_only_data.length == 5);
    BUSTER_TEST(arguments, string_literal_generated.first_data_relocation != 0);
    BUSTER_TEST(arguments, aarch64_string_literal_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_string_literal_generated.read_only_data.length == 5);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable string_literal_executable = codegen_make_executable(string_literal_generated);
    BUSTER_TEST(arguments, string_literal_executable.error == CODEGEN_ERROR_NONE);
    if (string_literal_executable.address)
    {
        CodegenTestFunction0* native_string_literal = 0;
        memcpy(&native_string_literal, &string_literal_executable.address, sizeof(native_string_literal));
        BUSTER_TEST(arguments, native_string_literal() == 'e');
        codegen_release_executable(string_literal_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable string_literal_executable = codegen_make_executable(aarch64_string_literal_generated);
    BUSTER_TEST(arguments, string_literal_executable.error == CODEGEN_ERROR_NONE);
    if (string_literal_executable.address)
    {
        CodegenTestFunction0* native_string_literal = 0;
        memcpy(&native_string_literal, &string_literal_executable.address, sizeof(native_string_literal));
        BUSTER_TEST(arguments, native_string_literal() == 'e');
        codegen_release_executable(string_literal_executable);
    }
#endif
    String8 wide_vector_names[] = {
        S8_INITIALIZER("vector_256_arithmetic"),
        S8_INITIALIZER("vector_256_commutative_rhs"),
        S8_INITIALIZER("vector_512_arithmetic"),
    };
    u64 wide_vector_results[] = {10, 10, 17};
    for (u32 wide_index = 0; wide_index < 3; wide_index += 1)
    {
        AnalysisEntity* wide_entity = codegen_test_entity_find(&analysis, wide_vector_names[wide_index]);
        IrFunction* wide_function = wide_entity ? codegen_test_function_find(&module, wide_entity->id) : 0;
        BUSTER_TEST(arguments, wide_function != 0);
        CodegenFunction wide_generated = wide_function ? codegen_generate_function(arguments->arena, &analysis, wide_function, target)
                                                       : (CodegenFunction){
                                                             .error = CODEGEN_ERROR_INVALID_IR,
                                                         };
        CodegenFunction aarch64_wide_generated = wide_function ? codegen_generate_function(arguments->arena, &analysis, wide_function, aarch64_target)
                                                               : (CodegenFunction){
                                                                     .error = CODEGEN_ERROR_INVALID_IR,
                                                                 };
        BUSTER_TEST(arguments, wide_generated.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, aarch64_wide_generated.error == CODEGEN_ERROR_NONE);
        Target split_target = wide_index < 2 ? baseline_target : avx2_target;
        Target native_target = wide_index < 2 ? avx2_target : avx10_target;
        CodegenFunction split_generated = wide_function ? codegen_generate_function(arguments->arena, &analysis, wide_function, split_target)
                                                        : (CodegenFunction){
                                                              .error = CODEGEN_ERROR_INVALID_IR,
                                                          };
        CodegenFunction native_generated = wide_function ? codegen_generate_function(arguments->arena, &analysis, wide_function, native_target)
                                                         : (CodegenFunction){
                                                               .error = CODEGEN_ERROR_INVALID_IR,
                                                           };
        BUSTER_TEST(arguments, split_generated.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, split_generated.native_vector_operation_count == 0);
        BUSTER_TEST(arguments, split_generated.split_vector_operation_count > 0);
        BUSTER_TEST(arguments, split_generated.vzeroupper_count == 0);
        BUSTER_TEST(arguments, native_generated.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, native_generated.native_vector_operation_count == (wide_index < 2 ? 2 : 1));
        BUSTER_TEST(arguments, native_generated.split_vector_operation_count == 0);
        BUSTER_TEST(arguments, native_generated.vzeroupper_count == 1);
        BUSTER_TEST(arguments, native_generated.forwarded_wide_vector_load_count == (wide_index < 2 ? 1 : 0));
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
        CodegenExecutable wide_executable = codegen_make_executable(wide_generated);
        BUSTER_TEST(arguments, wide_executable.error == CODEGEN_ERROR_NONE);
        if (wide_executable.address)
        {
            CodegenTestFunction0* native_wide = 0;
            memcpy(&native_wide, &wide_executable.address, sizeof(native_wide));
            BUSTER_TEST(arguments, native_wide() == wide_vector_results[wide_index]);
            codegen_release_executable(wide_executable);
        }
#else
        BUSTER_UNUSED(wide_vector_results);
#endif
    }
    AnalysisEntity* collection_entity = codegen_test_entity_find(&analysis, S8("collection_sum"));
    IrFunction* collection_function = collection_entity ? codegen_test_function_find(&module, collection_entity->id) : 0;
    BUSTER_TEST(arguments, collection_function != 0);
    CodegenFunction collection_generated = collection_function ? codegen_generate_function(arguments->arena, &analysis, collection_function, target)
                                                               : (CodegenFunction){
                                                                     .error = CODEGEN_ERROR_INVALID_IR,
                                                                 };
    CodegenFunction aarch64_collection_generated = collection_function
                                                       ? codegen_generate_function(arguments->arena, &analysis, collection_function, aarch64_target)
                                                       : (CodegenFunction){
                                                             .error = CODEGEN_ERROR_INVALID_IR,
                                                         };
    BUSTER_TEST(arguments, collection_generated.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, aarch64_collection_generated.error == CODEGEN_ERROR_NONE);
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable collection_executable = codegen_make_executable(collection_generated);
    BUSTER_TEST(arguments, collection_executable.error == CODEGEN_ERROR_NONE);
    if (collection_executable.address)
    {
        CodegenTestFunction0* native_collection = 0;
        memcpy(&native_collection, &collection_executable.address, sizeof(native_collection));
        BUSTER_TEST(arguments, native_collection() == 18);
        codegen_release_executable(collection_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenExecutable aarch64_collection_executable = codegen_make_executable(aarch64_collection_generated);
    BUSTER_TEST(arguments, aarch64_collection_executable.error == CODEGEN_ERROR_NONE);
    if (aarch64_collection_executable.address)
    {
        CodegenTestFunction0* native_aarch64_collection = 0;
        memcpy(&native_aarch64_collection, &aarch64_collection_executable.address, sizeof(native_aarch64_collection));
        BUSTER_TEST(arguments, native_aarch64_collection() == 18);
        codegen_release_executable(aarch64_collection_executable);
    }
#endif
    CodegenModule generated_module = codegen_generate_module(arguments->arena, &analysis, &module, target,
                                                              (CodegenModuleOptions){
                                                                  .lane_count = 1,
                                                                  .debug_info = true,
                                                              });
    CodegenModule parallel_generated_module = codegen_generate_module(arguments->arena, &analysis, &module, target,
                                                                       (CodegenModuleOptions){
                                                                           .lane_count = 3,
                                                                           .debug_info = true,
                                                                       });

    // A worker inherits a caller's legal commit granularity. With a 96 KiB
    // reservation, retaining the old fixed 64 KiB granularity would round the
    // first growth above 64 KiB past the reservation and abort.
    Arena* narrow_worker_arena = codegen_worker_arena_create(BUSTER_KB(96), BUSTER_KB(4));
    BUSTER_TEST(arguments, narrow_worker_arena != 0);
    if (narrow_worker_arena)
    {
        u8* worker_bytes = arena_allocate(narrow_worker_arena, u8, BUSTER_KB(80));
        worker_bytes[0] = 0x2d;
        worker_bytes[BUSTER_KB(80) - 1] = 0xd2;
        BUSTER_TEST(arguments, narrow_worker_arena->position > BUSTER_KB(64));
        BUSTER_TEST(arguments, narrow_worker_arena->os_position <= narrow_worker_arena->reserved_size);
        BUSTER_TEST(arguments, worker_bytes[0] == 0x2d && worker_bytes[BUSTER_KB(80) - 1] == 0xd2);
        BUSTER_TEST(arguments, arena_destroy(narrow_worker_arena, 1));
    }

    // Parallel scaffolding must preserve the public entry point's small-arena
    // contract even when an empty module has no function work to distribute.
    Arena* small_legacy_arena = arena_create((ArenaCreation){
        .reserved_size = BUSTER_KB(4),
        .initial_size = BUSTER_KB(4),
        .flags.no_pool = true,
    });
    BUSTER_TEST(arguments, small_legacy_arena != 0);
    if (small_legacy_arena)
    {
        IrModule empty_module = {0};
        CodegenModule small_module = codegen_generate_module(small_legacy_arena, &analysis, &empty_module, target,
                                                             (CodegenModuleOptions){
                                                                 .lane_count = 1,
                                                                 .assume_validated = true,
                                                             });
        BUSTER_TEST(arguments, small_module.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, arena_destroy(small_legacy_arena, 1));
    }
    BUSTER_TEST(arguments, generated_module.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, parallel_generated_module.error == CODEGEN_ERROR_NONE);
    BUSTER_TEST(arguments, generated_module.function_count == generated_module.entry_count);
    for (u32 function_index = 0; function_index < generated_module.function_count; function_index += 1)
    {
        CodegenFunctionDescriptor* descriptor = generated_module.functions + function_index;
        CodegenModuleEntry* entry = generated_module.entries + function_index;
        BUSTER_TEST(arguments, descriptor->symbol.value == entry->symbol.value);
        BUSTER_TEST(arguments, descriptor->code_offset == entry->offset);
        BUSTER_TEST(arguments, descriptor->code_offset + descriptor->code_size <= generated_module.code.length);
        BUSTER_TEST(arguments, descriptor->prolog_size <= descriptor->code_size);
        if (function_index + 1 < generated_module.function_count)
        {
            BUSTER_TEST(arguments, descriptor->code_offset + descriptor->code_size <= generated_module.functions[function_index + 1].code_offset);
        }
    }
    ObjectFile generated_object = object_from_codegen_module(arguments->arena, &analysis, &generated_module, target);
    BUSTER_TEST(arguments, generated_object.error == OBJECT_ERROR_NONE);
    for (u32 function_index = 0; function_index < generated_module.function_count && function_index < generated_object.symbol_count; function_index += 1)
    {
        BUSTER_TEST(arguments, generated_object.symbols[function_index].size == generated_module.functions[function_index].code_size);
    }
    BUSTER_TEST(arguments, generated_object.sections[OBJECT_SECTION_READ_ONLY_DATA].data.length >= 5);
    BUSTER_TEST(arguments, generated_object.relocation_count > generated_module.relocation_count);
    bool found_exported_string_symbol = false;
    for (u32 symbol_index = 0; symbol_index < generated_object.symbol_count; symbol_index += 1)
    {
        ObjectSymbol* symbol = &generated_object.symbols[symbol_index];
        if (symbol->global && string_equal(symbol->name, S8("string_literal_value")))
        {
            found_exported_string_symbol = true;
            break;
        }
    }
    BUSTER_TEST(arguments, found_exported_string_symbol);
    // Unwind sections and their relocations are native-format metadata. Keep
    // the object target paired with its writer instead of cross-serializing it.
    ObjectFormat artifact_format = object_format_for_target(target);
    ObjectArtifact artifact = object_write(arguments->arena, &generated_object, artifact_format);
    BUSTER_TEST(arguments, artifact.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, artifact.format == artifact_format);
    BUSTER_TEST(arguments, artifact.bytes.length > generated_module.code.length);
    if (generated_module.error == CODEGEN_ERROR_NONE && parallel_generated_module.error == CODEGEN_ERROR_NONE)
    {
        ObjectFile parallel_generated_object = object_from_codegen_module(arguments->arena, &analysis, &parallel_generated_module, target);
        ObjectArtifact parallel_artifact = object_write(arguments->arena, &parallel_generated_object, artifact_format);
        BUSTER_TEST(arguments, parallel_generated_object.error == OBJECT_ERROR_NONE);
        BUSTER_TEST(arguments, parallel_artifact.error == OBJECT_ERROR_NONE);
        BUSTER_TEST(arguments, artifact.bytes.length == parallel_artifact.bytes.length);
        if (artifact.bytes.length == parallel_artifact.bytes.length)
        {
            BUSTER_TEST(arguments, memcmp(artifact.bytes.pointer, parallel_artifact.bytes.pointer, artifact.bytes.length) == 0);
        }
    }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    ObjectExecutable generated_object_executable = object_link_executable(&generated_object);
    BUSTER_TEST(arguments, generated_object_executable.error == OBJECT_ERROR_NONE);
    CodegenModuleEntry* string_object_entry = string_literal_entity ? codegen_test_module_entry_find(&generated_module, string_literal_entity->id) : 0;
    BUSTER_TEST(arguments, string_object_entry != 0);
    if (generated_object_executable.address && string_object_entry)
    {
        void* entry_address = (u8*)generated_object_executable.address + string_object_entry->offset;
        CodegenTestFunction0* native_string_object = 0;
        memcpy(&native_string_object, &entry_address, sizeof(native_string_object));
        BUSTER_TEST(arguments, native_string_object() == 'e');
        object_release_executable(generated_object_executable);
    }
#endif
    Target windows_target = target;
    windows_target.os = OPERATING_SYSTEM_WINDOWS;
    CodegenModule windows_abi_module = codegen_generate_module(arguments->arena, &analysis, &module, windows_target, (CodegenModuleOptions){0});
    BUSTER_TEST(arguments, windows_abi_module.error == CODEGEN_ERROR_NONE);
    bool analysis_windows_body_valid = true;
    bool analysis_windows_store_bounds_valid = true;
    bool analysis_windows_call_scanned = false;
    u32 analysis_windows_call_function_count = 0;
    if (windows_abi_module.error == CODEGEN_ERROR_NONE)
    {
        for (u32 function_index = 0; function_index < windows_abi_module.function_count; function_index += 1)
        {
            CodegenFunctionDescriptor* descriptor = windows_abi_module.functions + function_index;
            bool has_call = false;
            for (u32 relocation_index = 0; relocation_index < windows_abi_module.relocation_count; relocation_index += 1)
            {
                CodegenModuleRelocation* relocation = windows_abi_module.relocations + relocation_index;
                if (relocation->source == CODEGEN_MODULE_RELOCATION_CODE && !relocation->aarch64 && !relocation->absolute && relocation->offset > descriptor->code_offset &&
                    relocation->offset + 4 <= descriptor->code_offset + descriptor->code_size && windows_abi_module.code.pointer[relocation->offset - 1] == 0xe8)
                {
                    has_call = true;
                    break;
                }
            }
            if (!has_call)
            {
                continue;
            }
            u32 allocation = 0;
            bool saw_allocation = false;
            bool frame_pointer_after_allocation = false;
            for (u32 action_index = 0; action_index < descriptor->unwind_action_count; action_index += 1)
            {
                CodegenUnwindAction* action = descriptor->unwind_actions + action_index;
                if (action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK)
                {
                    saw_allocation = true;
                    allocation = allocation > UINT32_MAX - action->value ? UINT32_MAX : allocation + action->value;
                }
                else if (action->kind == CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER)
                {
                    frame_pointer_after_allocation |= saw_allocation;
                }
            }
            if ((u64)descriptor->code_offset + descriptor->code_size > windows_abi_module.code.length || descriptor->prolog_size > descriptor->code_size)
            {
                analysis_windows_body_valid = false;
                continue;
            }
            CodegenTestX64BodyScan scan = codegen_test_x64_scan_body(windows_abi_module.code, descriptor->code_offset + descriptor->prolog_size,
                                                                       descriptor->code_offset + descriptor->code_size, allocation,
                                                                       frame_pointer_after_allocation ? allocation : UINT32_MAX);
            analysis_windows_call_function_count += 1;
            analysis_windows_call_scanned |= scan.has_call;
            analysis_windows_body_valid &= scan.valid;
            analysis_windows_store_bounds_valid &= scan.valid && (!scan.has_stack_store || scan.maximum_stack_store_end <= allocation);
        }
    }
    BUSTER_TEST(arguments, analysis_windows_call_function_count != 0);
    BUSTER_TEST(arguments, analysis_windows_call_scanned);
    BUSTER_TEST(arguments, analysis_windows_body_valid);
    BUSTER_TEST(arguments, analysis_windows_store_bounds_valid);
    Target aapcs64_target = aarch64_target;
    aapcs64_target.os = OPERATING_SYSTEM_LINUX;
    CodegenModule aapcs64_abi_module = codegen_generate_module(arguments->arena, &analysis, &module, aapcs64_target, (CodegenModuleOptions){0});
    BUSTER_TEST(arguments, aapcs64_abi_module.error == CODEGEN_ERROR_NONE);
    ObjectFile aapcs64_object = object_from_codegen_module(arguments->arena, &analysis, &aapcs64_abi_module, aapcs64_target);
    BUSTER_TEST(arguments, aapcs64_object.error == OBJECT_ERROR_NONE);
    bool found_aarch64_call_relocation = false;
    bool found_aarch64_absolute_relocation = false;
    for (u32 relocation_index = 0; relocation_index < aapcs64_object.relocation_count; relocation_index += 1)
    {
        ObjectRelocationKind kind = aapcs64_object.relocations[relocation_index].kind;
        found_aarch64_call_relocation |= kind == OBJECT_RELOCATION_AARCH64_CALL26;
        found_aarch64_absolute_relocation |= kind == OBJECT_RELOCATION_ABSOLUTE64;
    }
    BUSTER_TEST(arguments, found_aarch64_call_relocation);
    BUSTER_TEST(arguments, found_aarch64_absolute_relocation);
    ObjectArtifact aapcs64_artifact = object_write(arguments->arena, &aapcs64_object, OBJECT_FORMAT_ELF64);
    BUSTER_TEST(arguments, aapcs64_artifact.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, aapcs64_artifact.bytes.length > aapcs64_abi_module.code.length);
    Target darwin_target = aarch64_target;
    darwin_target.os = OPERATING_SYSTEM_MACOS;
    CodegenModule darwin_abi_module = codegen_generate_module(arguments->arena, &analysis, &module, darwin_target, (CodegenModuleOptions){0});
    BUSTER_TEST(arguments, darwin_abi_module.error == CODEGEN_ERROR_NONE);
    ObjectFile darwin_aarch64_object = object_from_codegen_module(arguments->arena, &analysis, &darwin_abi_module, darwin_target);
    BUSTER_TEST(arguments, darwin_aarch64_object.error == OBJECT_ERROR_NONE);
    ObjectArtifact darwin_aarch64_artifact = object_write(arguments->arena, &darwin_aarch64_object, OBJECT_FORMAT_MACH_O64);
    BUSTER_TEST(arguments, darwin_aarch64_artifact.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, darwin_aarch64_artifact.bytes.length > darwin_abi_module.code.length);
    Target windows_aarch64_target = aarch64_target;
    windows_aarch64_target.os = OPERATING_SYSTEM_WINDOWS;
    CodegenModule windows_aarch64_abi_module = codegen_generate_module(arguments->arena, &analysis, &module, windows_aarch64_target, (CodegenModuleOptions){0});
    BUSTER_TEST(arguments, windows_aarch64_abi_module.error == CODEGEN_ERROR_NONE);
    ObjectFile windows_aarch64_object = object_from_codegen_module(arguments->arena, &analysis, &windows_aarch64_abi_module, windows_aarch64_target);
    BUSTER_TEST(arguments, windows_aarch64_object.error == OBJECT_ERROR_NONE);
    ObjectArtifact windows_aarch64_artifact = object_write(arguments->arena, &windows_aarch64_object, OBJECT_FORMAT_COFF);
    BUSTER_TEST(arguments, windows_aarch64_artifact.error == OBJECT_ERROR_NONE);
    BUSTER_TEST(arguments, windows_aarch64_artifact.bytes.length > windows_aarch64_abi_module.code.length);
    AnalysisEntity* caller_entity = codegen_test_entity_find(&analysis, S8("call_chain"));
    BUSTER_TEST(arguments, caller_entity != 0);
    AnalysisEntity* call_many_entity = codegen_test_entity_find(&analysis, S8("call_many"));
    AnalysisEntity* variadic_call_entity = codegen_test_entity_find(&analysis, S8("variadic_call"));
    AnalysisEntity* variadic_float_call_entity = codegen_test_entity_find(&analysis, S8("variadic_float_call"));
    AnalysisEntity* variadic_promoted_call_entity = codegen_test_entity_find(&analysis, S8("variadic_promoted_call"));
    AnalysisEntity* variadic_pair_call_entity = codegen_test_entity_find(&analysis, S8("variadic_pair_call"));
    AnalysisEntity* variadic_mixed_call_entity = codegen_test_entity_find(&analysis, S8("variadic_mixed_call"));
    AnalysisEntity* variadic_large_call_entity = codegen_test_entity_find(&analysis, S8("variadic_large_call"));
    AnalysisEntity* variadic_fixed_float_call_entity = codegen_test_entity_find(&analysis, S8("variadic_fixed_float_call"));
    AnalysisEntity* large_merge_entity = codegen_test_entity_find(&analysis, S8("abi_large_merge"));
    AnalysisEntity* large_merge_call_entity = codegen_test_entity_find(&analysis, S8("abi_large_merge_call"));
    AnalysisEntity* add_one_entity = codegen_test_entity_find(&analysis, S8("add_one"));
    IrFunction* add_one_function = add_one_entity ? codegen_test_function_find(&module, add_one_entity->id) : 0;
    IrFunction* caller_function = caller_entity ? codegen_test_function_find(&module, caller_entity->id) : 0;
    IrFunction* large_merge_function = large_merge_entity ? codegen_test_function_find(&module, large_merge_entity->id) : 0;
    IrFunction* large_merge_call_function = large_merge_call_entity ? codegen_test_function_find(&module, large_merge_call_entity->id) : 0;
    IrFunction* variadic_fixed_float_call_function =
        variadic_fixed_float_call_entity ? codegen_test_function_find(&module, variadic_fixed_float_call_entity->id) : 0;
    BUSTER_TEST(arguments, add_one_function != 0);
    BUSTER_TEST(arguments, caller_function != 0);
    BUSTER_TEST(arguments, large_merge_function != 0);
    BUSTER_TEST(arguments, large_merge_call_function != 0);
    BUSTER_TEST(arguments, variadic_fixed_float_call_function != 0);
    if (variadic_fixed_float_call_function)
    {
        for (u32 instruction_index = 0; instruction_index < variadic_fixed_float_call_function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = variadic_fixed_float_call_function->instructions + instruction_index;
            if (instruction->opcode != IR_OPCODE_CALL)
            {
                continue;
            }
            u32 original_operand_count = instruction->operand_count;
            instruction->operand_count = 1;
            u32 invalid_variadic_stack_size = 123;
            BUSTER_TEST(arguments, codegen_x64_maximum_call_stack_size(arguments->arena, &analysis, variadic_fixed_float_call_function, windows_target,
                                                                       &invalid_variadic_stack_size) == CODEGEN_ERROR_INVALID_IR);
            BUSTER_TEST(arguments, invalid_variadic_stack_size == 0);
            instruction->operand_count = original_operand_count;
            break;
        }
    }
    if (large_merge_function)
    {
        AnalysisType* large_merge_type = analysis_type_from_id(&analysis, large_merge_function->type);
        AnalysisTypeId large_merge_argument_types[2] = {
            large_merge_type->as.function.argument_types[0],
            large_merge_type->as.function.argument_types[1],
        };
        CodegenAbiSignature large_merge_signature =
            codegen_classify_signature_with_arguments(arguments->arena, &analysis, large_merge_function->type, large_merge_argument_types, 2, windows_target);
        BUSTER_TEST(arguments, large_merge_signature.valid);
        BUSTER_TEST(arguments, large_merge_signature.result.indirect);
        BUSTER_TEST(arguments, large_merge_signature.argument_count == 2);
        if (large_merge_signature.valid && large_merge_signature.argument_count == 2)
        {
            BUSTER_TEST(arguments, large_merge_signature.arguments[0].indirect && large_merge_signature.arguments[1].indirect);
            BUSTER_TEST(arguments, large_merge_signature.arguments[0].indirect_copy_offset != large_merge_signature.arguments[1].indirect_copy_offset);
            BUSTER_TEST(arguments, large_merge_signature.arguments[0].indirect_copy_offset >= 32);
            BUSTER_TEST(arguments, large_merge_signature.arguments[1].indirect_copy_offset >= 32);
            BUSTER_TEST(arguments, large_merge_type->kind == ANALYSIS_TYPE_FUNCTION && large_merge_type->as.function.argument_count == 2);
            if (large_merge_type->kind == ANALYSIS_TYPE_FUNCTION && large_merge_type->as.function.argument_count == 2)
            {
                for (u32 argument_index = 0; argument_index < 2; argument_index += 1)
                {
                    AnalysisType* argument_type = analysis_type_from_id(&analysis, large_merge_type->as.function.argument_types[argument_index]);
                    u64 copy_end = (u64)large_merge_signature.arguments[argument_index].indirect_copy_offset + argument_type->layout.size;
                    BUSTER_TEST(arguments, (large_merge_signature.arguments[argument_index].indirect_copy_offset & 15) == 0);
                    BUSTER_TEST(arguments, copy_end <= large_merge_signature.stack_size);
                }
                u64 first_copy_end = (u64)large_merge_signature.arguments[0].indirect_copy_offset +
                                     analysis_type_from_id(&analysis, large_merge_type->as.function.argument_types[0])->layout.size;
                u64 second_copy_end = (u64)large_merge_signature.arguments[1].indirect_copy_offset +
                                      analysis_type_from_id(&analysis, large_merge_type->as.function.argument_types[1])->layout.size;
                BUSTER_TEST(arguments, first_copy_end <= large_merge_signature.arguments[1].indirect_copy_offset ||
                                         second_copy_end <= large_merge_signature.arguments[0].indirect_copy_offset);
            }
        }
    }
    if (large_merge_call_function)
    {
        CodegenFunction large_merge_call_generated = codegen_generate_function(arguments->arena, &analysis, large_merge_call_function, windows_target);
        BUSTER_TEST(arguments, large_merge_call_generated.error == CODEGEN_ERROR_NONE);
        u32 large_merge_call_allocations = 0;
        for (u32 action_index = 0; action_index < large_merge_call_generated.descriptor.unwind_action_count; action_index += 1)
        {
            large_merge_call_allocations += large_merge_call_generated.descriptor.unwind_actions[action_index].kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK;
        }
        BUSTER_TEST(arguments, large_merge_call_allocations == 1);
    }
    CodegenModuleEntry* caller_entry = 0;
    CodegenModuleEntry* call_many_entry = 0;
    CodegenModuleEntry* variadic_call_entry = variadic_call_entity ? codegen_test_module_entry_find(&generated_module, variadic_call_entity->id) : 0;
    CodegenModuleEntry* variadic_float_call_entry =
        variadic_float_call_entity ? codegen_test_module_entry_find(&generated_module, variadic_float_call_entity->id) : 0;
    CodegenModuleEntry* variadic_promoted_call_entry =
        variadic_promoted_call_entity ? codegen_test_module_entry_find(&generated_module, variadic_promoted_call_entity->id) : 0;
    CodegenModuleEntry* variadic_pair_call_entry =
        variadic_pair_call_entity ? codegen_test_module_entry_find(&generated_module, variadic_pair_call_entity->id) : 0;
    CodegenModuleEntry* variadic_mixed_call_entry =
        variadic_mixed_call_entity ? codegen_test_module_entry_find(&generated_module, variadic_mixed_call_entity->id) : 0;
    CodegenModuleEntry* variadic_large_call_entry =
        variadic_large_call_entity ? codegen_test_module_entry_find(&generated_module, variadic_large_call_entity->id) : 0;
    if (caller_entity)
    {
        for (u32 index = 0; index < generated_module.entry_count; index += 1)
        {
            if (generated_module.entries[index].entity.module.value == caller_entity->id.module.value &&
                generated_module.entries[index].entity.index.value == caller_entity->id.index.value)
            {
                caller_entry = generated_module.entries + index;
                break;
            }
        }
    }
    if (call_many_entity)
    {
        for (u32 index = 0; index < generated_module.entry_count; index += 1)
        {
            if (generated_module.entries[index].entity.module.value == call_many_entity->id.module.value &&
                generated_module.entries[index].entity.index.value == call_many_entity->id.index.value)
            {
                call_many_entry = generated_module.entries + index;
                break;
            }
        }
    }
    BUSTER_TEST(arguments, caller_entry != 0);
    BUSTER_TEST(arguments, call_many_entry != 0);
    BUSTER_TEST(arguments, variadic_call_entry != 0);
    BUSTER_TEST(arguments, variadic_float_call_entry != 0);
    BUSTER_TEST(arguments, variadic_promoted_call_entry != 0);
    BUSTER_TEST(arguments, variadic_pair_call_entry != 0);
    BUSTER_TEST(arguments, variadic_mixed_call_entry != 0);
    BUSTER_TEST(arguments, variadic_large_call_entry != 0);
    AnalysisEntity* integer_to_float_entity = codegen_test_entity_find(&analysis, S8("integer_to_float"));
    AnalysisEntity* float_to_integer_entity = codegen_test_entity_find(&analysis, S8("float_to_integer"));
    AnalysisEntity* choose_entity = codegen_test_entity_find(&analysis, S8("choose"));
    CodegenModuleEntry* integer_to_float_entry = integer_to_float_entity ? codegen_test_module_entry_find(&generated_module, integer_to_float_entity->id) : 0;
    CodegenModuleEntry* float_to_integer_entry = float_to_integer_entity ? codegen_test_module_entry_find(&generated_module, float_to_integer_entity->id) : 0;
    CodegenModuleEntry* choose_entry = choose_entity ? codegen_test_module_entry_find(&generated_module, choose_entity->id) : 0;
    AnalysisEntity* abi_pair_round_trip_entity = codegen_test_entity_find(&analysis, S8("abi_pair_round_trip"));
    AnalysisEntity* abi_mixed_round_trip_entity = codegen_test_entity_find(&analysis, S8("abi_mixed_round_trip"));
    AnalysisEntity* abi_large_round_trip_entity = codegen_test_entity_find(&analysis, S8("abi_large_round_trip"));
    AnalysisEntity* abi_pair_sum_entity = codegen_test_entity_find(&analysis, S8("abi_pair_sum"));
    AnalysisEntity* abi_pair_make_entity = codegen_test_entity_find(&analysis, S8("abi_pair_make"));
    AnalysisEntity* abi_mixed_sum_entity = codegen_test_entity_find(&analysis, S8("abi_mixed_sum"));
    AnalysisEntity* abi_large_sum_entity = codegen_test_entity_find(&analysis, S8("abi_large_sum"));
    AnalysisEntity* abi_large_make_entity = codegen_test_entity_find(&analysis, S8("abi_large_make"));
    CodegenModuleEntry* abi_pair_round_trip_entry =
        abi_pair_round_trip_entity ? codegen_test_module_entry_find(&generated_module, abi_pair_round_trip_entity->id) : 0;
    CodegenModuleEntry* abi_mixed_round_trip_entry =
        abi_mixed_round_trip_entity ? codegen_test_module_entry_find(&generated_module, abi_mixed_round_trip_entity->id) : 0;
    CodegenModuleEntry* abi_large_round_trip_entry =
        abi_large_round_trip_entity ? codegen_test_module_entry_find(&generated_module, abi_large_round_trip_entity->id) : 0;
    CodegenModuleEntry* abi_pair_sum_entry = abi_pair_sum_entity ? codegen_test_module_entry_find(&generated_module, abi_pair_sum_entity->id) : 0;
    CodegenModuleEntry* abi_pair_make_entry = abi_pair_make_entity ? codegen_test_module_entry_find(&generated_module, abi_pair_make_entity->id) : 0;
    CodegenModuleEntry* abi_mixed_sum_entry = abi_mixed_sum_entity ? codegen_test_module_entry_find(&generated_module, abi_mixed_sum_entity->id) : 0;
    CodegenModuleEntry* abi_large_sum_entry = abi_large_sum_entity ? codegen_test_module_entry_find(&generated_module, abi_large_sum_entity->id) : 0;
    CodegenModuleEntry* abi_large_make_entry = abi_large_make_entity ? codegen_test_module_entry_find(&generated_module, abi_large_make_entity->id) : 0;
    BUSTER_TEST(arguments, integer_to_float_entry != 0);
    BUSTER_TEST(arguments, float_to_integer_entry != 0);
    BUSTER_TEST(arguments, choose_entry != 0);
    BUSTER_TEST(arguments, abi_pair_round_trip_entry != 0);
    BUSTER_TEST(arguments, abi_mixed_round_trip_entry != 0);
    BUSTER_TEST(arguments, abi_large_round_trip_entry != 0);
    BUSTER_TEST(arguments, abi_pair_sum_entry != 0);
    BUSTER_TEST(arguments, abi_pair_make_entry != 0);
    BUSTER_TEST(arguments, abi_mixed_sum_entry != 0);
    BUSTER_TEST(arguments, abi_large_sum_entry != 0);
    BUSTER_TEST(arguments, abi_large_make_entry != 0);
    IrFunction* choose_function = choose_entity ? codegen_test_function_find(&module, choose_entity->id) : 0;
    CodegenFunction aarch64_switch_generated = choose_function ? codegen_generate_function(arguments->arena, &analysis, choose_function, aarch64_target)
                                                               : (CodegenFunction){
                                                                     .error = CODEGEN_ERROR_INVALID_IR,
                                                                 };
    BUSTER_TEST(arguments, aarch64_switch_generated.error == CODEGEN_ERROR_NONE);
    if (add_one_function && caller_function)
    {
        IrFunction* call_functions = arena_allocate(arguments->arena, IrFunction, 2);
        call_functions[0] = *add_one_function;
        call_functions[1] = *caller_function;
        IrModule call_module = {
            .functions = call_functions,
            .function_count = 2,
            .lowered_function_count = 2,
        };
        CodegenModule aarch64_call_module = codegen_generate_module(arguments->arena, &analysis, &call_module, aarch64_target, (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, aarch64_call_module.error == CODEGEN_ERROR_NONE);
        bool found_link = false;
        for (u64 offset = 0; offset + 4 <= aarch64_call_module.code.length; offset += 4)
        {
            u32 encoded = 0;
            memcpy(&encoded, aarch64_call_module.code.pointer + offset, sizeof(encoded));
            found_link |= (encoded & 0xfc000000) == 0x94000000;
        }
        BUSTER_TEST(arguments, found_link);
    }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    CodegenExecutable module_executable = codegen_make_executable((CodegenFunction){
        .code = generated_module.code,
        .error = generated_module.error,
    });
    BUSTER_TEST(arguments, module_executable.error == CODEGEN_ERROR_NONE);
    if (module_executable.address && caller_entry)
    {
        void* caller_address = (u8*)module_executable.address + caller_entry->offset;
        CodegenTestFunction1* native_caller = 0;
        BUSTER_CT_CHECK(sizeof(native_caller) == sizeof(caller_address));
        memcpy(&native_caller, &caller_address, sizeof(native_caller));
        BUSTER_TEST(arguments, native_caller(20) == 42);
        if (call_many_entry)
        {
            void* call_many_address = (u8*)module_executable.address + call_many_entry->offset;
            CodegenTestFunction0* native_call_many = 0;
            memcpy(&native_call_many, &call_many_address, sizeof(native_call_many));
            BUSTER_TEST(arguments, native_call_many() == 28);
        }
        if (variadic_call_entry)
        {
            void* variadic_call_address = (u8*)module_executable.address + variadic_call_entry->offset;
            CodegenTestFunction0* native_variadic_call = 0;
            memcpy(&native_variadic_call, &variadic_call_address, sizeof(native_variadic_call));
            BUSTER_TEST(arguments, native_variadic_call() == 42);
        }
        if (variadic_float_call_entry)
        {
            void* address = (u8*)module_executable.address + variadic_float_call_entry->offset;
            CodegenTestFloatFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 5.25);
        }
        if (variadic_promoted_call_entry)
        {
            void* address = (u8*)module_executable.address + variadic_promoted_call_entry->offset;
            CodegenTestFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 42);
        }
        if (variadic_pair_call_entry)
        {
            void* address = (u8*)module_executable.address + variadic_pair_call_entry->offset;
            CodegenTestFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 42);
        }
        if (variadic_mixed_call_entry)
        {
            void* address = (u8*)module_executable.address + variadic_mixed_call_entry->offset;
            CodegenTestFloatFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 5.25);
        }
        if (variadic_large_call_entry)
        {
            void* address = (u8*)module_executable.address + variadic_large_call_entry->offset;
            CodegenTestFunction0* native = 0;
            memcpy(&native, &address, sizeof(native));
            BUSTER_TEST(arguments, native() == 31);
        }
        if (integer_to_float_entry && float_to_integer_entry)
        {
            void* integer_to_float_address = (u8*)module_executable.address + integer_to_float_entry->offset;
            void* float_to_integer_address = (u8*)module_executable.address + float_to_integer_entry->offset;
            CodegenTestIntegerToFloatFunction* native_integer_to_float = 0;
            CodegenTestFloatToIntegerFunction* native_float_to_integer = 0;
            memcpy(&native_integer_to_float, &integer_to_float_address, sizeof(native_integer_to_float));
            memcpy(&native_float_to_integer, &float_to_integer_address, sizeof(native_float_to_integer));
            BUSTER_TEST(arguments, native_integer_to_float(-7) == -7.0);
            BUSTER_TEST(arguments, native_float_to_integer(8.75) == 8);
        }
        if (choose_entry)
        {
            void* choose_address = (u8*)module_executable.address + choose_entry->offset;
            CodegenTestFunction1* native_choose = 0;
            memcpy(&native_choose, &choose_address, sizeof(native_choose));
            BUSTER_TEST(arguments, native_choose(0) == 11);
            BUSTER_TEST(arguments, native_choose(1) == 22);
        }
        if (abi_pair_round_trip_entry && abi_mixed_round_trip_entry && abi_large_round_trip_entry)
        {
            void* pair_address = (u8*)module_executable.address + abi_pair_round_trip_entry->offset;
            void* mixed_address = (u8*)module_executable.address + abi_mixed_round_trip_entry->offset;
            void* large_address = (u8*)module_executable.address + abi_large_round_trip_entry->offset;
            CodegenTestFunction0* native_pair = 0;
            CodegenTestFunction0* native_large = 0;
            CodegenTestFloatFunction0* native_mixed = 0;
            memcpy(&native_pair, &pair_address, sizeof(native_pair));
            memcpy(&native_large, &large_address, sizeof(native_large));
            memcpy(&native_mixed, &mixed_address, sizeof(native_mixed));
            u64 native_pair_value = native_pair();
            BUSTER_TEST(arguments, native_pair_value == 42);
            BUSTER_TEST(arguments, native_large() == 23);
            BUSTER_TEST(arguments, native_mixed() == 3.5);
        }
#if !BUSTER_COMPILER_TCC
        if (abi_pair_sum_entry && abi_pair_make_entry && abi_mixed_sum_entry && abi_large_sum_entry && abi_large_make_entry)
        {
            void* pair_sum_address = (u8*)module_executable.address + abi_pair_sum_entry->offset;
            void* pair_make_address = (u8*)module_executable.address + abi_pair_make_entry->offset;
            void* mixed_sum_address = (u8*)module_executable.address + abi_mixed_sum_entry->offset;
            void* large_sum_address = (u8*)module_executable.address + abi_large_sum_entry->offset;
            void* large_make_address = (u8*)module_executable.address + abi_large_make_entry->offset;
            CodegenTestAbiPairSumFunction* pair_sum = 0;
            CodegenTestAbiPairMakeFunction* pair_make = 0;
            CodegenTestAbiMixedSumFunction* mixed_sum = 0;
            CodegenTestAbiLargeSumFunction* large_sum = 0;
            CodegenTestAbiLargeMakeFunction* large_make = 0;
            memcpy(&pair_sum, &pair_sum_address, sizeof(pair_sum));
            memcpy(&pair_make, &pair_make_address, sizeof(pair_make));
            memcpy(&mixed_sum, &mixed_sum_address, sizeof(mixed_sum));
            memcpy(&large_sum, &large_sum_address, sizeof(large_sum));
            memcpy(&large_make, &large_make_address, sizeof(large_make));
            CodegenTestAbiPair pair = {13, 17};
            BUSTER_TEST(arguments, pair_sum(pair) == 30);
            pair = pair_make(29, 31);
            BUSTER_TEST(arguments, pair.left == 29);
            BUSTER_TEST(arguments, pair.right == 31);
            CodegenTestAbiMixed mixed = {2.25, 3};
            BUSTER_TEST(arguments, mixed_sum(mixed) == 5.25);
            CodegenTestAbiLarge large = {2, 3, 5};
            BUSTER_TEST(arguments, large_sum(large) == 10);
            large = large_make(7, 11, 13);
            BUSTER_TEST(arguments, large.first == 7);
            BUSTER_TEST(arguments, large.second == 11);
            BUSTER_TEST(arguments, large.third == 13);
        }
#endif
        codegen_release_executable(module_executable);
    }
#endif
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_SANITIZE
    CodegenModule native_aarch64_module = codegen_generate_module(arguments->arena, &analysis, &module, aarch64_target, (CodegenModuleOptions){0});
    BUSTER_TEST(arguments, native_aarch64_module.error == CODEGEN_ERROR_NONE);
    CodegenModuleEntry* native_aarch64_pair_sum_entry =
        abi_pair_sum_entity ? codegen_test_module_entry_find(&native_aarch64_module, abi_pair_sum_entity->id) : 0;
    CodegenModuleEntry* native_aarch64_pair_make_entry =
        abi_pair_make_entity ? codegen_test_module_entry_find(&native_aarch64_module, abi_pair_make_entity->id) : 0;
    CodegenModuleEntry* native_aarch64_mixed_sum_entry =
        abi_mixed_sum_entity ? codegen_test_module_entry_find(&native_aarch64_module, abi_mixed_sum_entity->id) : 0;
    CodegenModuleEntry* native_aarch64_large_sum_entry =
        abi_large_sum_entity ? codegen_test_module_entry_find(&native_aarch64_module, abi_large_sum_entity->id) : 0;
    CodegenModuleEntry* native_aarch64_large_make_entry =
        abi_large_make_entity ? codegen_test_module_entry_find(&native_aarch64_module, abi_large_make_entity->id) : 0;
    CodegenExecutable native_aarch64_executable = codegen_make_executable((CodegenFunction){
        .code = native_aarch64_module.code,
        .error = native_aarch64_module.error,
    });
    BUSTER_TEST(arguments, native_aarch64_executable.error == CODEGEN_ERROR_NONE);
    if (native_aarch64_executable.address && native_aarch64_pair_sum_entry && native_aarch64_pair_make_entry && native_aarch64_mixed_sum_entry &&
        native_aarch64_large_sum_entry && native_aarch64_large_make_entry)
    {
        void* pair_sum_address = (u8*)native_aarch64_executable.address + native_aarch64_pair_sum_entry->offset;
        void* pair_make_address = (u8*)native_aarch64_executable.address + native_aarch64_pair_make_entry->offset;
        void* mixed_sum_address = (u8*)native_aarch64_executable.address + native_aarch64_mixed_sum_entry->offset;
        void* large_sum_address = (u8*)native_aarch64_executable.address + native_aarch64_large_sum_entry->offset;
        void* large_make_address = (u8*)native_aarch64_executable.address + native_aarch64_large_make_entry->offset;
        CodegenTestAbiPairSumFunction* pair_sum = 0;
        CodegenTestAbiPairMakeFunction* pair_make = 0;
        CodegenTestAbiMixedSumFunction* mixed_sum = 0;
        CodegenTestAbiLargeSumFunction* large_sum = 0;
        CodegenTestAbiLargeMakeFunction* large_make = 0;
        memcpy(&pair_sum, &pair_sum_address, sizeof(pair_sum));
        memcpy(&pair_make, &pair_make_address, sizeof(pair_make));
        memcpy(&mixed_sum, &mixed_sum_address, sizeof(mixed_sum));
        memcpy(&large_sum, &large_sum_address, sizeof(large_sum));
        memcpy(&large_make, &large_make_address, sizeof(large_make));
        BUSTER_TEST(arguments, pair_sum((CodegenTestAbiPair){13, 17}) == 30);
        CodegenTestAbiPair pair = pair_make(29, 31);
        BUSTER_TEST(arguments, pair.left == 29);
        BUSTER_TEST(arguments, pair.right == 31);
        BUSTER_TEST(arguments, mixed_sum((CodegenTestAbiMixed){2.25, 3}) == 5.25);
        BUSTER_TEST(arguments, large_sum((CodegenTestAbiLarge){2, 3, 5}) == 10);
        CodegenTestAbiLarge large = large_make(7, 11, 13);
        BUSTER_TEST(arguments, large.first == 7);
        BUSTER_TEST(arguments, large.second == 11);
        BUSTER_TEST(arguments, large.third == 13);
    }
    codegen_release_executable(native_aarch64_executable);
#endif
    Target canonical_windows_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_WINDOWS,
    };
    IrInstruction malformed_call = {
        .opcode = IR_OPCODE_CALL,
    };
    IrFunction malformed_function = {
        .instructions = &malformed_call,
        .instruction_count = 1,
    };
    u32 malformed_stack_size = 123;
    BUSTER_TEST(arguments, codegen_x64_maximum_call_stack_size(arguments->arena, &analysis, &malformed_function, canonical_windows_target, &malformed_stack_size) ==
                                 CODEGEN_ERROR_INVALID_IR);
    BUSTER_TEST(arguments, malformed_stack_size == 0);
    if (caller_function)
    {
        IrInstruction* mutation_call = 0;
        for (u32 instruction_index = 0; instruction_index < caller_function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = caller_function->instructions + instruction_index;
            if (instruction->opcode == IR_OPCODE_CALL && instruction->operand_count && instruction->operands)
            {
                mutation_call = instruction;
                break;
            }
        }
        BUSTER_TEST(arguments, mutation_call != 0);
        if (mutation_call && mutation_call->operands[0].value < caller_function->value_count)
        {
            AnalysisTypeId invalid_type = {.value = analysis.types.count};
            IrValue* callee_value = caller_function->values + mutation_call->operands[0].value;
            AnalysisTypeId saved_callee_type = callee_value->type;
            callee_value->type = invalid_type;
            u32 invalid_callee_stack_size = 123;
            BUSTER_TEST(arguments, codegen_x64_maximum_call_stack_size(arguments->arena, &analysis, caller_function, canonical_windows_target,
                                                                       &invalid_callee_stack_size) == CODEGEN_ERROR_INVALID_IR);
            BUSTER_TEST(arguments, invalid_callee_stack_size == 0);
            callee_value->type = saved_callee_type;
            AnalysisType* callee_type = saved_callee_type.value < analysis.types.count ? analysis.types.types + saved_callee_type.value : 0;
            AnalysisTypeId function_type_id = callee_type && callee_type->kind == ANALYSIS_TYPE_POINTER ? callee_type->as.element_type : saved_callee_type;
            AnalysisType* function_type = function_type_id.value < analysis.types.count ? analysis.types.types + function_type_id.value : 0;
            BUSTER_TEST(arguments, function_type && function_type->kind == ANALYSIS_TYPE_FUNCTION);
            if (function_type && function_type->kind == ANALYSIS_TYPE_FUNCTION)
            {
                AnalysisTypeId saved_return_type = function_type->as.function.return_type;
                function_type->as.function.return_type = invalid_type;
                u32 invalid_return_stack_size = 123;
                BUSTER_TEST(arguments, codegen_x64_maximum_call_stack_size(arguments->arena, &analysis, caller_function, canonical_windows_target,
                                                                           &invalid_return_stack_size) == CODEGEN_ERROR_INVALID_IR);
                BUSTER_TEST(arguments, invalid_return_stack_size == 0);
                function_type->as.function.return_type = saved_return_type;
                for (u32 parameter_index = 0; parameter_index < function_type->as.function.argument_count; parameter_index += 1)
                {
                    AnalysisTypeId saved_parameter_type = function_type->as.function.argument_types[parameter_index];
                    function_type->as.function.argument_types[parameter_index] = invalid_type;
                    u32 invalid_parameter_stack_size = 123;
                    BUSTER_TEST(arguments, codegen_x64_maximum_call_stack_size(arguments->arena, &analysis, caller_function, canonical_windows_target,
                                                                               &invalid_parameter_stack_size) == CODEGEN_ERROR_INVALID_IR);
                    BUSTER_TEST(arguments, invalid_parameter_stack_size == 0);
                    function_type->as.function.argument_types[parameter_index] = saved_parameter_type;
                }
                for (u32 operand_index = 1; operand_index < mutation_call->operand_count; operand_index += 1)
                {
                    IrValueId operand = mutation_call->operands[operand_index];
                    if (operand.value >= caller_function->value_count)
                    {
                        continue;
                    }
                    IrValue* operand_value = caller_function->values + operand.value;
                    AnalysisTypeId saved_operand_type = operand_value->type;
                    operand_value->type = invalid_type;
                    u32 invalid_operand_stack_size = 123;
                    BUSTER_TEST(arguments, codegen_x64_maximum_call_stack_size(arguments->arena, &analysis, caller_function, canonical_windows_target,
                                                                               &invalid_operand_stack_size) == CODEGEN_ERROR_INVALID_IR);
                    BUSTER_TEST(arguments, invalid_operand_stack_size == 0);
                    operand_value->type = saved_operand_type;
                }
            }
        }
    }
    String8 canonical_windows_c_source = S8(
        "typedef void *va_list;\n"
        "struct Pair { int left; int right; };\n"
        "struct Big { long long first; long long second; long long third; };\n"
        "static int sum_pair(struct Pair value) { return value.left + value.right; }\n"
        "static int sum_five(int a, int b, int c, int d, int e) { return a + b + c + d + e; }\n"
        "static int callback_one(int value) { return value + 1; }\n"
        "int indirect_layout(void) { int (*entry)(int) = callback_one; return entry(7); }\n"
        "static int variadic_sum(int first, ...) {\n"
        "    va_list arguments;\n"
        "    int second;\n"
        "    __builtin_va_start(arguments, first);\n"
        "    second = __builtin_va_arg(arguments, int);\n"
        "    __builtin_va_end(arguments);\n"
        "    return first + second;\n"
        "}\n"
        "static void mutate_big(struct Big value) { value.first += 9; }\n"
        "static void mutate_big_after_four(int a, int b, int c, int d, struct Big value) { value.first += a + b + c + d; }\n"
        "static struct Big make_big(long long first, long long second, long long third) {\n"
        "    return (struct Big){first, second, third};\n"
        "}\n"
        "static struct Big transform_big(struct Big value) { value.first += 1; return value; }\n"
        "int layout_mix(void) {\n"
        "    struct Pair pair = {2, 3};\n"
        "    struct Big original = {1, 2, 3};\n"
        "    struct Big made = make_big(4, 5, 6);\n"
        "    struct Big transformed = transform_big(original);\n"
        "    mutate_big(original);\n"
        "    mutate_big_after_four(1, 2, 3, 4, original);\n"
        "    return sum_pair(pair) + sum_five(1, 2, 3, 4, 5) + variadic_sum(6, 7) + original.first + made.first + transformed.first;\n"
        "}\n"
        "int leaf_layout(void) { return 7; }\n"
        "int large_layout(void) {\n"
        "    unsigned char padding[5000];\n"
        "    struct Big original = {1, 2, 3};\n"
        "    padding[0] = 4;\n"
        "    mutate_big(original);\n"
        "    return original.first + padding[0];\n"
        "}\n"
        "int dynamic_layout(int count) {\n"
        "    unsigned char values[count];\n"
        "    values[32] = 0x5a;\n"
        "    return sum_five(1, 2, 3, 4, 5) + values[32];\n"
        "}\n");
    CPreprocessResult canonical_windows_tokens = c_preprocess(arguments->arena, canonical_windows_c_source, (CPreprocessOptions){0});
    CParseResult canonical_windows_parse = c_parse(arguments->arena, canonical_windows_tokens);
    CIRLowerResult canonical_windows_ir = c_lower_to_ir(arguments->arena, S8("canonical-windows-call-layout.c"), canonical_windows_tokens,
                                                         canonical_windows_parse, canonical_windows_target);
    BUSTER_TEST(arguments, canonical_windows_tokens.error_count == 0);
    BUSTER_TEST(arguments, canonical_windows_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, canonical_windows_ir.diagnostic_count == 0);
    if (canonical_windows_ir.program)
    {
        IrProgram* canonical_program = canonical_windows_ir.program;
        IrModule* canonical_module = canonical_program->modules;
        IrValidationResult canonical_validation = ir_validate_canonical_module(canonical_program, canonical_module);
        BUSTER_TEST(arguments, canonical_validation.error == IR_VALIDATION_NONE);

        Arena* small_canonical_arena = arena_create((ArenaCreation){
            .reserved_size = BUSTER_KB(4),
            .initial_size = BUSTER_KB(4),
            .flags.no_pool = true,
        });
        BUSTER_TEST(arguments, small_canonical_arena != 0);
        if (small_canonical_arena)
        {
            IrModule empty_module = {0};
            CodegenModule small_module = codegen_generate_canonical_module(
                small_canonical_arena, canonical_program, &empty_module, canonical_windows_target,
                (CodegenModuleOptions){
                    .lane_count = 1,
                    .assume_validated = true,
                });
            BUSTER_TEST(arguments, small_module.error == CODEGEN_ERROR_NONE);
            BUSTER_TEST(arguments, arena_destroy(small_canonical_arena, 1));
        }
        CodegenModule canonical_windows_module = codegen_generate_canonical_module(arguments->arena, canonical_program, canonical_module,
                                                                                    canonical_windows_target, (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, canonical_windows_module.error == CODEGEN_ERROR_NONE);
        // Function workers may complete in any order, but the stable merge
        // must make the complete COFF/CodeView/unwind artifact independent of
        // lane width. Debug output is the strongest aggregate oracle because
        // it also consumes line rows, location seeds, descriptors, symbols,
        // and relocation order.
        CodegenModule canonical_windows_serial = codegen_generate_canonical_module(
            arguments->arena, canonical_program, canonical_module, canonical_windows_target,
            (CodegenModuleOptions){
                .lane_count = 1,
                .debug_info = true,
                .assume_validated = true,
            });
        CodegenModule canonical_windows_parallel = codegen_generate_canonical_module(
            arguments->arena, canonical_program, canonical_module, canonical_windows_target,
            (CodegenModuleOptions){
                .lane_count = 3,
                .debug_info = true,
                .assume_validated = true,
            });
        BUSTER_TEST(arguments, canonical_windows_serial.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, canonical_windows_parallel.error == CODEGEN_ERROR_NONE);
        if (canonical_windows_serial.error == CODEGEN_ERROR_NONE && canonical_windows_parallel.error == CODEGEN_ERROR_NONE)
        {
            ObjectFile serial_object =
                object_from_canonical_codegen_module(arguments->arena, canonical_program, &canonical_windows_serial, canonical_windows_target);
            ObjectFile parallel_object =
                object_from_canonical_codegen_module(arguments->arena, canonical_program, &canonical_windows_parallel, canonical_windows_target);
            ObjectArtifact serial_artifact = object_write(arguments->arena, &serial_object, object_format_for_target(canonical_windows_target));
            ObjectArtifact parallel_artifact = object_write(arguments->arena, &parallel_object, object_format_for_target(canonical_windows_target));
            BUSTER_TEST(arguments, serial_artifact.error == OBJECT_ERROR_NONE);
            BUSTER_TEST(arguments, parallel_artifact.error == OBJECT_ERROR_NONE);
            BUSTER_TEST(arguments, serial_artifact.bytes.length == parallel_artifact.bytes.length);
            if (serial_artifact.bytes.length == parallel_artifact.bytes.length)
            {
                BUSTER_TEST(arguments, memcmp(serial_artifact.bytes.pointer, parallel_artifact.bytes.pointer, serial_artifact.bytes.length) == 0);
            }
            BUSTER_TEST(arguments, canonical_windows_serial.statistics.function_count == canonical_windows_parallel.statistics.function_count);
            BUSTER_TEST(arguments, canonical_windows_serial.statistics.code_bytes == canonical_windows_parallel.statistics.code_bytes);
        }
        IrFunction* layout_mix_function = codegen_test_c_function_find(canonical_module, S8("layout_mix"));
        IrFunction* leaf_layout_function = codegen_test_c_function_find(canonical_module, S8("leaf_layout"));
        IrFunction* large_layout_function = codegen_test_c_function_find(canonical_module, S8("large_layout"));
        IrFunction* dynamic_layout_function = codegen_test_c_function_find(canonical_module, S8("dynamic_layout"));
        BUSTER_TEST(arguments, layout_mix_function != 0);
        BUSTER_TEST(arguments, leaf_layout_function != 0);
        BUSTER_TEST(arguments, large_layout_function != 0);
        BUSTER_TEST(arguments, dynamic_layout_function != 0);
        if (layout_mix_function && leaf_layout_function && large_layout_function && dynamic_layout_function &&
            canonical_windows_module.error == CODEGEN_ERROR_NONE)
        {
            CodegenFunctionDescriptor* layout_mix_descriptor = codegen_test_c_descriptor_find(&canonical_windows_module, layout_mix_function->symbol);
            CodegenFunctionDescriptor* leaf_layout_descriptor = codegen_test_c_descriptor_find(&canonical_windows_module, leaf_layout_function->symbol);
            CodegenFunctionDescriptor* large_layout_descriptor = codegen_test_c_descriptor_find(&canonical_windows_module, large_layout_function->symbol);
            CodegenFunctionDescriptor* dynamic_layout_descriptor = codegen_test_c_descriptor_find(&canonical_windows_module, dynamic_layout_function->symbol);
            BUSTER_TEST(arguments, layout_mix_descriptor != 0);
            BUSTER_TEST(arguments, leaf_layout_descriptor != 0);
            BUSTER_TEST(arguments, large_layout_descriptor != 0);
            BUSTER_TEST(arguments, dynamic_layout_descriptor != 0);
            u32 maximum_layout_stack_size = 0;
            u32 layout_stack_size_count = 0;
            bool found_register_indirect_copy = false;
            bool found_stack_indirect_copy = false;
            bool found_hidden_indirect_result = false;
            bool found_variadic_call = false;
            bool found_more_than_four_arguments = false;
            bool found_differing_outgoing_sizes = false;
            u32 first_layout_stack_size = 0;
            for (u32 instruction_index = 0; instruction_index < layout_mix_function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = layout_mix_function->instructions + instruction_index;
                if (instruction->opcode != IR_OPCODE_CALL)
                {
                    continue;
                }
                CodegenCanonicalCallLayout call_layout = {0};
                CodegenError call_error = codegen_canonical_x64_call_layout(arguments->arena, canonical_program, layout_mix_function, instruction,
                                                                             CODEGEN_ABI_X86_64_WINDOWS,
                                                                             codegen_target_for_abi(CODEGEN_ABI_X86_64_WINDOWS), &call_layout);
                BUSTER_TEST(arguments, call_error == CODEGEN_ERROR_NONE);
                if (call_error != CODEGEN_ERROR_NONE)
                {
                    continue;
                }
                if (!layout_stack_size_count)
                {
                    first_layout_stack_size = call_layout.windows_stack_size;
                }
                else
                {
                    found_differing_outgoing_sizes |= first_layout_stack_size != call_layout.windows_stack_size;
                }
                layout_stack_size_count += 1;
                maximum_layout_stack_size = BUSTER_MAX(maximum_layout_stack_size, call_layout.windows_stack_size);
                found_more_than_four_arguments |= call_layout.argument_count > 4;
                found_hidden_indirect_result |= call_layout.windows_indirect_return;
                IrType* callee_type = ir_type_from_id(&canonical_program->types, layout_mix_function->values[instruction->operands[0].value].canonical_type);
                if (callee_type && callee_type->kind == IR_TYPE_POINTER)
                {
                    callee_type = ir_type_from_id(&canonical_program->types, callee_type->element_type);
                }
                found_variadic_call |= callee_type && callee_type->kind == IR_TYPE_FUNCTION && callee_type->is_variadic;
                u32 stack_slots_end = 32 + call_layout.stack_part_count * 8;
                for (u32 argument_index = 0; argument_index < call_layout.argument_count; argument_index += 1)
                {
                    CodegenCanonicalCallArgument* call_argument = call_layout.arguments + argument_index;
                    if (!call_argument->windows_indirect)
                    {
                        continue;
                    }
                    BUSTER_TEST(arguments, call_argument->copy_size == call_argument->type->layout.size);
                    BUSTER_TEST(arguments, call_argument->copy_alignment == 16);
                    BUSTER_TEST(arguments, (call_argument->copy_offset & 15) == 0);
                    BUSTER_TEST(arguments, call_argument->copy_offset >= stack_slots_end);
                    BUSTER_TEST(arguments, call_argument->copy_offset + call_argument->copy_size <= call_layout.windows_stack_size);
                    found_register_indirect_copy |= !call_argument->on_stack;
                    found_stack_indirect_copy |= call_argument->on_stack;
                }
            }
            BUSTER_TEST(arguments, layout_stack_size_count >= 4);
            BUSTER_TEST(arguments, maximum_layout_stack_size >= 32);
            BUSTER_TEST(arguments, found_more_than_four_arguments);
            BUSTER_TEST(arguments, found_register_indirect_copy);
            BUSTER_TEST(arguments, found_stack_indirect_copy);
            BUSTER_TEST(arguments, found_hidden_indirect_result);
            BUSTER_TEST(arguments, found_variadic_call);
            BUSTER_TEST(arguments, found_differing_outgoing_sizes);
            u32 layout_allocated = 0;
            u32 layout_allocation_count = 0;
            if (layout_mix_descriptor)
            {
                for (u32 action_index = 0; action_index < layout_mix_descriptor->unwind_action_count; action_index += 1)
                {
                    CodegenUnwindAction* action = layout_mix_descriptor->unwind_actions + action_index;
                    if (action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK)
                    {
                        layout_allocated += action->value;
                        layout_allocation_count += 1;
                    }
                }
                BUSTER_TEST(arguments, layout_allocation_count == 1);
                BUSTER_TEST(arguments, layout_allocated == codegen_test_canonical_value_frame_size(canonical_program, layout_mix_function) + maximum_layout_stack_size);
            }
            if (leaf_layout_descriptor)
            {
                u32 leaf_allocated = 0;
                u32 leaf_allocation_count = 0;
                for (u32 action_index = 0; action_index < leaf_layout_descriptor->unwind_action_count; action_index += 1)
                {
                    CodegenUnwindAction* action = leaf_layout_descriptor->unwind_actions + action_index;
                    if (action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK)
                    {
                        leaf_allocated += action->value;
                        leaf_allocation_count += 1;
                    }
                }
                BUSTER_TEST(arguments, leaf_allocated == codegen_test_canonical_value_frame_size(canonical_program, leaf_layout_function));
                BUSTER_TEST(arguments, leaf_allocated < 32);
                BUSTER_TEST(arguments, leaf_allocation_count <= 1);
            }
            if (large_layout_descriptor)
            {
                u32 large_allocated = 0;
                u32 large_allocation_count = 0;
                for (u32 action_index = 0; action_index < large_layout_descriptor->unwind_action_count; action_index += 1)
                {
                    CodegenUnwindAction* action = large_layout_descriptor->unwind_actions + action_index;
                    if (action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK)
                    {
                        large_allocated += action->value;
                        large_allocation_count += 1;
                    }
                }
                BUSTER_TEST(arguments, large_allocated > 4096);
                BUSTER_TEST(arguments, large_allocation_count == 1);
            }
            bool dynamic_body_decode_valid = true;
            bool dynamic_outgoing_allocation_valid = false;
            bool dynamic_outgoing_cleanup_valid = false;
            bool dynamic_call_scanned = false;
            bool dynamic_has_stack_allocate = false;
            u32 dynamic_call_count = 0;
            u32 dynamic_call_stack_size = 0;
            for (u32 instruction_index = 0; instruction_index < dynamic_layout_function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = dynamic_layout_function->instructions + instruction_index;
                dynamic_has_stack_allocate |= instruction->opcode == IR_OPCODE_STACK_ALLOCATE;
                if (instruction->opcode != IR_OPCODE_CALL)
                {
                    continue;
                }
                CodegenCanonicalCallLayout call_layout = {0};
                CodegenError call_error = codegen_canonical_x64_call_layout(arguments->arena, canonical_program, dynamic_layout_function, instruction,
                                                                             CODEGEN_ABI_X86_64_WINDOWS,
                                                                             codegen_target_for_abi(CODEGEN_ABI_X86_64_WINDOWS), &call_layout);
                BUSTER_TEST(arguments, call_error == CODEGEN_ERROR_NONE);
                if (call_error == CODEGEN_ERROR_NONE)
                {
                    dynamic_call_count += 1;
                    dynamic_call_stack_size = BUSTER_MAX(dynamic_call_stack_size, call_layout.windows_stack_size);
                }
                else
                {
                    dynamic_body_decode_valid = false;
                }
            }
            bool full_body_decode_valid = true;
            bool full_body_stack_adjust_valid = true;
            bool full_body_stack_store_bounds_valid = true;
            u32 full_body_function_count = 0;
            bool found_scanned_layout_call = false;
            bool found_scanned_indirect_call = false;
            bool found_scanned_stack_store = false;
            for (u32 function_index = 0; function_index < canonical_windows_module.function_count; function_index += 1)
            {
                CodegenFunctionDescriptor* descriptor = canonical_windows_module.functions + function_index;
                if (descriptor == dynamic_layout_descriptor)
                {
                    continue;
                }
                if ((u64)descriptor->code_offset + descriptor->code_size > canonical_windows_module.code.length || descriptor->prolog_size > descriptor->code_size)
                {
                    full_body_decode_valid = false;
                    continue;
                }
                u32 allocation = 0;
                bool saw_allocation = false;
                bool frame_pointer_after_allocation = false;
                for (u32 action_index = 0; action_index < descriptor->unwind_action_count; action_index += 1)
                {
                    CodegenUnwindAction* action = descriptor->unwind_actions + action_index;
                    if (action->kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK)
                    {
                        saw_allocation = true;
                        if (allocation > UINT32_MAX - action->value)
                        {
                            full_body_stack_store_bounds_valid = false;
                        }
                        else
                        {
                            allocation += action->value;
                        }
                    }
                    else if (action->kind == CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER)
                    {
                        frame_pointer_after_allocation |= saw_allocation;
                    }
                }
                CodegenTestX64BodyScan scan = codegen_test_x64_scan_body(canonical_windows_module.code, descriptor->code_offset + descriptor->prolog_size,
                                                                           descriptor->code_offset + descriptor->code_size, allocation,
                                                                           frame_pointer_after_allocation ? allocation : UINT32_MAX);
                full_body_function_count += 1;
                full_body_decode_valid &= scan.valid;
                full_body_stack_adjust_valid &= scan.valid;
                full_body_stack_store_bounds_valid &= scan.valid && (!scan.has_stack_store || scan.maximum_stack_store_end <= allocation);
                found_scanned_layout_call |= scan.has_call && (descriptor == layout_mix_descriptor || descriptor == large_layout_descriptor);
                found_scanned_indirect_call |= scan.has_indirect_call;
                found_scanned_stack_store |= scan.has_stack_store && (descriptor == layout_mix_descriptor || descriptor == large_layout_descriptor);
            }
            bool dynamic_all_calls_have_outgoing_allocation = true;
            bool dynamic_all_calls_have_outgoing_cleanup = true;
            u32 dynamic_relocation_call_count = 0;
            if (dynamic_layout_descriptor && dynamic_call_count && dynamic_call_stack_size <= INT32_MAX &&
                (u64)dynamic_layout_descriptor->code_offset + dynamic_layout_descriptor->code_size <= canonical_windows_module.code.length &&
                dynamic_layout_descriptor->prolog_size <= dynamic_layout_descriptor->code_size)
            {
                u64 dynamic_body_start = dynamic_layout_descriptor->code_offset + dynamic_layout_descriptor->prolog_size;
                u64 dynamic_function_end = dynamic_layout_descriptor->code_offset + dynamic_layout_descriptor->code_size;
                for (u32 relocation_index = 0; relocation_index < canonical_windows_module.relocation_count; relocation_index += 1)
                {
                    CodegenModuleRelocation* relocation = canonical_windows_module.relocations + relocation_index;
                    if (relocation->source != CODEGEN_MODULE_RELOCATION_CODE || relocation->aarch64 || relocation->absolute || relocation->offset == 0 ||
                        relocation->offset <= dynamic_layout_descriptor->code_offset || relocation->offset + 4 > dynamic_function_end ||
                        canonical_windows_module.code.pointer[relocation->offset - 1] != 0xe8)
                    {
                        continue;
                    }
                    dynamic_relocation_call_count += 1;
                    dynamic_call_scanned = true;
                    u64 call_start = relocation->offset - 1;
                    u64 call_end = relocation->offset + 4;
                    bool before_valid = true;
                    bool after_valid = true;
                    bool found_allocation = false;
                    bool found_cleanup = false;
                    for (u64 offset = dynamic_body_start; offset < call_start;)
                    {
                        CodegenTestX64Instruction instruction = {0};
                        if (!codegen_test_x64_decode_instruction(canonical_windows_module.code, offset, call_start, &instruction))
                        {
                            before_valid = false;
                            break;
                        }
                        found_allocation |= instruction.rsp_change && !instruction.call && instruction.rsp_adjust == -(s32)dynamic_call_stack_size;
                        offset += instruction.length;
                    }
                    for (u64 offset = call_end; offset < dynamic_function_end;)
                    {
                        CodegenTestX64Instruction instruction = {0};
                        if (!codegen_test_x64_decode_instruction(canonical_windows_module.code, offset, dynamic_function_end, &instruction))
                        {
                            after_valid = false;
                            break;
                        }
                        found_cleanup |= instruction.rsp_change && !instruction.call && instruction.rsp_adjust == (s32)dynamic_call_stack_size;
                        offset += instruction.length;
                    }
                    dynamic_body_decode_valid &= before_valid && after_valid;
                    dynamic_all_calls_have_outgoing_allocation &= before_valid && found_allocation;
                    dynamic_all_calls_have_outgoing_cleanup &= after_valid && found_cleanup;
                }
            }
            dynamic_outgoing_allocation_valid = dynamic_relocation_call_count != 0 && dynamic_all_calls_have_outgoing_allocation;
            dynamic_outgoing_cleanup_valid = dynamic_relocation_call_count != 0 && dynamic_all_calls_have_outgoing_cleanup;
            bool found_layout_call = false;
            bool found_layout_stack_store = false;
            bool layout_body_stack_adjust_valid = true;
            for (u32 relocation_index = 0; relocation_index < canonical_windows_module.relocation_count; relocation_index += 1)
            {
                CodegenModuleRelocation* relocation = canonical_windows_module.relocations + relocation_index;
                if (relocation->source != CODEGEN_MODULE_RELOCATION_CODE || relocation->aarch64 || relocation->absolute || relocation->offset == 0 ||
                    relocation->offset > canonical_windows_module.code.length || canonical_windows_module.code.pointer[relocation->offset - 1] != 0xe8)
                {
                    continue;
                }
                CodegenFunctionDescriptor* descriptor = 0;
                for (u32 function_index = 0; function_index < canonical_windows_module.function_count; function_index += 1)
                {
                    CodegenFunctionDescriptor* candidate = canonical_windows_module.functions + function_index;
                    if (relocation->offset - 1 >= candidate->code_offset && relocation->offset + 4 <= candidate->code_offset + candidate->code_size)
                    {
                        descriptor = candidate;
                        break;
                    }
                }
                if (!descriptor)
                {
                    continue;
                }
                if (descriptor == dynamic_layout_descriptor)
                {
                    continue;
                }
                found_layout_call |= descriptor == layout_mix_descriptor || descriptor == large_layout_descriptor;
                u64 call_start = relocation->offset - 1;
                if ((call_start >= descriptor->code_offset + 4 && canonical_windows_module.code.pointer[call_start - 4] == 0x48 &&
                     canonical_windows_module.code.pointer[call_start - 3] == 0x83 && canonical_windows_module.code.pointer[call_start - 2] == 0xec) ||
                    (call_start >= descriptor->code_offset + 7 && canonical_windows_module.code.pointer[call_start - 7] == 0x48 &&
                     canonical_windows_module.code.pointer[call_start - 6] == 0x81 && canonical_windows_module.code.pointer[call_start - 5] == 0xec))
                {
                    layout_body_stack_adjust_valid = false;
                }
                u64 call_end = relocation->offset + 4;
                if ((call_end + 4 <= descriptor->code_offset + descriptor->code_size && canonical_windows_module.code.pointer[call_end] == 0x48 &&
                     canonical_windows_module.code.pointer[call_end + 1] == 0x83 && canonical_windows_module.code.pointer[call_end + 2] == 0xc4) ||
                    (call_end + 7 <= descriptor->code_offset + descriptor->code_size && canonical_windows_module.code.pointer[call_end] == 0x48 &&
                     canonical_windows_module.code.pointer[call_end + 1] == 0x81 && canonical_windows_module.code.pointer[call_end + 2] == 0xc4))
                {
                    layout_body_stack_adjust_valid = false;
                }
            }
            if (layout_mix_descriptor)
            {
                for (u64 byte_index = layout_mix_descriptor->code_offset + layout_mix_descriptor->prolog_size;
                     byte_index + 8 <= layout_mix_descriptor->code_offset + layout_mix_descriptor->code_size; byte_index += 1)
                {
                    u8 rex = canonical_windows_module.code.pointer[byte_index];
                    u8 modrm = canonical_windows_module.code.pointer[byte_index + 2];
                    if ((rex == 0x48 || rex == 0x4c) && canonical_windows_module.code.pointer[byte_index + 1] == 0x89 && (modrm & 0xc7) == 0x84 &&
                        canonical_windows_module.code.pointer[byte_index + 3] == 0x24)
                    {
                        found_layout_stack_store = true;
                        u32 displacement = 0;
                        memcpy(&displacement, canonical_windows_module.code.pointer + byte_index + 4, sizeof(displacement));
                        BUSTER_TEST(arguments, displacement + 8 <= layout_allocated);
                    }
                }
            }
            BUSTER_TEST(arguments, found_layout_call);
            BUSTER_TEST(arguments, found_scanned_layout_call);
            BUSTER_TEST(arguments, found_scanned_stack_store || found_layout_stack_store);
            BUSTER_TEST(arguments, full_body_function_count != 0);
            BUSTER_TEST(arguments, full_body_decode_valid);
            BUSTER_TEST(arguments, full_body_stack_adjust_valid);
            BUSTER_TEST(arguments, full_body_stack_store_bounds_valid);
            BUSTER_TEST(arguments, found_scanned_indirect_call);
            BUSTER_TEST(arguments, layout_body_stack_adjust_valid);
            BUSTER_TEST(arguments, dynamic_has_stack_allocate);
            BUSTER_TEST(arguments, dynamic_call_count == 1);
            BUSTER_TEST(arguments, dynamic_call_stack_size >= 32);
            BUSTER_TEST(arguments, dynamic_call_scanned);
            BUSTER_TEST(arguments, dynamic_body_decode_valid);
            BUSTER_TEST(arguments, dynamic_outgoing_allocation_valid);
            BUSTER_TEST(arguments, dynamic_outgoing_cleanup_valid);
        }
    }
    // A System V stack argument is placed at an address respecting its own
    // alignment rather than immediately after the argument before it, and the
    // area itself starts at the widest alignment any of them asked for. This is
    // asserted on the layout and on the emitted body rather than by running a
    // fixture, because a caller and a callee that agree with each other but not
    // with the psABI pass every fixture this suite can build: the disagreement
    // only shows against an object some other compiler produced, where a
    // stack-passed 512-bit vector is read back with `vmovaps` and a
    // sixteen-aligned aggregate is read from the offset the ABI put it at.
    String8 stack_alignment_c_source = S8(
        "typedef unsigned char Bytes64 __attribute__((vector_size(64)));\n"
        "struct Wide16 { _Alignas(16) long long v[2]; };\n"
        "struct Wide64 { _Alignas(64) long long v[8]; };\n"
        "static Bytes64 vector_ninth(Bytes64 a, Bytes64 b, Bytes64 c, Bytes64 d, Bytes64 e, Bytes64 f, Bytes64 g, Bytes64 h, Bytes64 i) {\n"
        "    return i;\n"
        "}\n"
        "static long long after_wide16(long long a, long long b, long long c, long long d, long long e, long long f, long long g,\n"
        "                              struct Wide16 w) { return g + w.v[0] + w.v[1]; }\n"
        "static long long after_wide64(long long a, long long b, long long c, long long d, long long e, long long f, long long g,\n"
        "                              struct Wide64 w) { return g + w.v[0] + w.v[7]; }\n"
        "long long stack_alignment_calls(Bytes64 v, struct Wide16 w16, struct Wide64 w64) {\n"
        "    Bytes64 ninth = vector_ninth(v, v, v, v, v, v, v, v, v);\n"
        "    return after_wide16(1, 2, 3, 4, 5, 6, 7, w16) + after_wide64(1, 2, 3, 4, 5, 6, 7, w64) + ninth[0];\n"
        "}\n");
    CPreprocessResult stack_alignment_tokens = c_preprocess(arguments->arena, stack_alignment_c_source, (CPreprocessOptions){0});
    CParseResult stack_alignment_parse = c_parse(arguments->arena, stack_alignment_tokens);
    CIRLowerResult stack_alignment_ir =
        c_lower_to_ir(arguments->arena, S8("system-v-stack-argument-alignment.c"), stack_alignment_tokens, stack_alignment_parse, avx512f_target);
    BUSTER_TEST(arguments, stack_alignment_tokens.error_count == 0);
    BUSTER_TEST(arguments, stack_alignment_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, stack_alignment_ir.diagnostic_count == 0);
    if (stack_alignment_ir.program)
    {
        IrProgram* stack_alignment_program = stack_alignment_ir.program;
        IrModule* stack_alignment_module = stack_alignment_program->modules;
        CodegenModule stack_alignment_generated = codegen_generate_canonical_module(arguments->arena, stack_alignment_program, stack_alignment_module,
                                                                                    avx512f_target, (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, stack_alignment_generated.error == CODEGEN_ERROR_NONE);
        IrFunction* stack_alignment_function = codegen_test_c_function_find(stack_alignment_module, S8("stack_alignment_calls"));
        BUSTER_TEST(arguments, stack_alignment_function != 0);
        bool stack_alignment_offsets_valid = true;
        bool stack_alignment_area_valid = true;
        bool found_vector_ninth_layout = false;
        bool found_wide16_layout = false;
        bool found_wide64_layout = false;
        for (u32 instruction_index = 0; stack_alignment_function && instruction_index < stack_alignment_function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = stack_alignment_function->instructions + instruction_index;
            if (instruction->opcode != IR_OPCODE_CALL)
            {
                continue;
            }
            CodegenCanonicalCallLayout stack_alignment_layout = {0};
            CodegenError stack_alignment_error = codegen_canonical_x64_call_layout(
                arguments->arena, stack_alignment_program, stack_alignment_function, instruction, CODEGEN_ABI_X86_64_SYSTEM_V, avx512f_target,
                &stack_alignment_layout);
            BUSTER_TEST(arguments, stack_alignment_error == CODEGEN_ERROR_NONE);
            if (stack_alignment_error != CODEGEN_ERROR_NONE)
            {
                continue;
            }
            u32 widest_stack_argument = CODEGEN_X64_STACK_ALIGNMENT;
            CodegenCanonicalCallArgument* last_stack_argument = 0;
            for (u32 argument_index = 0; argument_index < stack_alignment_layout.argument_count; argument_index += 1)
            {
                CodegenCanonicalCallArgument* stack_argument = stack_alignment_layout.arguments + argument_index;
                if (!stack_argument->on_stack)
                {
                    continue;
                }
                u32 argument_alignment = codegen_canonical_x64_stack_argument_alignment(stack_argument->type);
                widest_stack_argument = BUSTER_MAX(widest_stack_argument, argument_alignment);
                stack_alignment_offsets_valid &= (stack_argument->stack_offset & (argument_alignment - 1)) == 0;
                stack_alignment_offsets_valid &=
                    stack_argument->stack_offset + stack_argument->stack_part_count * 8 <= stack_alignment_layout.stack_part_count * 8;
                last_stack_argument = stack_argument;
            }
            stack_alignment_area_valid &= stack_alignment_layout.stack_alignment == widest_stack_argument;
            if (!last_stack_argument)
            {
                continue;
            }
            // The ninth vector is the only thing on the stack, so it starts the
            // area; the aggregates follow a seventh integer that took the first
            // eightbyte and are pushed past it to their own alignment.
            if (stack_alignment_layout.argument_count == 9)
            {
                found_vector_ninth_layout = true;
                stack_alignment_offsets_valid &= last_stack_argument->stack_offset == 0 && stack_alignment_layout.stack_alignment == 64;
            }
            else if (last_stack_argument->type->layout.size == 16)
            {
                found_wide16_layout = true;
                stack_alignment_offsets_valid &= last_stack_argument->stack_offset == 16 && stack_alignment_layout.stack_alignment == 16;
            }
            else if (last_stack_argument->type->layout.size == 64)
            {
                found_wide64_layout = true;
                stack_alignment_offsets_valid &= last_stack_argument->stack_offset == 64 && stack_alignment_layout.stack_alignment == 64;
            }
        }
        BUSTER_TEST(arguments, stack_alignment_offsets_valid);
        BUSTER_TEST(arguments, stack_alignment_area_valid);
        BUSTER_TEST(arguments, found_vector_ninth_layout);
        BUSTER_TEST(arguments, found_wide16_layout);
        BUSTER_TEST(arguments, found_wide64_layout);
        // An area wanting more than the sixteen bytes the stack pointer is
        // already worth is reached by rounding the stack pointer down, which
        // pushing cannot do. `and rsp, -64` is what says the caller did it.
        bool found_stack_realignment = false;
        for (u64 byte_index = 0; byte_index + 7 <= stack_alignment_generated.code.length; byte_index += 1)
        {
            u8 const* code = stack_alignment_generated.code.pointer + byte_index;
            u32 realign_mask = 0;
            memcpy(&realign_mask, code + 3, sizeof(realign_mask));
            found_stack_realignment |= code[0] == 0x48 && code[1] == 0x81 && code[2] == 0xe4 && realign_mask == (u32)(0 - (u32)64);
        }
        BUSTER_TEST(arguments, found_stack_realignment);
    }
    // Vector operands must be reached through the same frame rebase as every
    // other canonical value. A Win64 function whose scope emits a stack restore
    // -- which every loop body does -- keeps rbp at the bottom of its frame and
    // addresses values at positive displacements, where every other target
    // keeps rbp at the top and uses negative ones. Spelling a vector operand's
    // displacement as a bare negation is only right where the rebase is the
    // identity, so it addressed outside the frame on Windows alone, and only in
    // a function that also has a loop: the self-hosted tokenizer's window loop
    // is exactly that shape, and its classification masks came back garbage.
    String8 vector_frame_c_source = S8(
        "typedef unsigned char Bytes16 __attribute__((vector_size(16)));\n"
        "void vector_with_loop(Bytes16 *out, Bytes16 *left, Bytes16 *right, int count, int *total_out) {\n"
        "    int total = 0;\n"
        "    for (int index = 0; index < count; index += 1) { total += index; }\n"
        "    *total_out = total;\n"
        "    Bytes16 sum = *left + *right;\n"
        "    *out = sum;\n"
        "}\n"
        "void vector_without_loop(Bytes16 *out, Bytes16 *left, Bytes16 *right) {\n"
        "    Bytes16 sum = *left + *right;\n"
        "    *out = sum;\n"
        "}\n");
    Target vector_frame_targets[] = {canonical_windows_target, target};
    for (u32 vector_frame_index = 0; vector_frame_index < BUSTER_ARRAY_LENGTH(vector_frame_targets); vector_frame_index += 1)
    {
        Target vector_frame_target = vector_frame_targets[vector_frame_index];
        bool vector_frame_windows = vector_frame_target.os == OPERATING_SYSTEM_WINDOWS;
        CPreprocessResult vector_frame_tokens = c_preprocess(arguments->arena, vector_frame_c_source, (CPreprocessOptions){0});
        CParseResult vector_frame_parse = c_parse(arguments->arena, vector_frame_tokens);
        CIRLowerResult vector_frame_ir =
            c_lower_to_ir(arguments->arena, S8("canonical-vector-frame.c"), vector_frame_tokens, vector_frame_parse, vector_frame_target);
        BUSTER_TEST(arguments, vector_frame_tokens.error_count == 0);
        BUSTER_TEST(arguments, vector_frame_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, vector_frame_ir.diagnostic_count == 0);
        if (!vector_frame_ir.program)
        {
            continue;
        }
        IrModule* vector_frame_module_ir = vector_frame_ir.program->modules;
        CodegenModule vector_frame_module = codegen_generate_canonical_module(arguments->arena, vector_frame_ir.program, vector_frame_module_ir,
                                                                              vector_frame_target, (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, vector_frame_module.error == CODEGEN_ERROR_NONE);
        if (vector_frame_module.error != CODEGEN_ERROR_NONE)
        {
            continue;
        }
        String8 vector_frame_names[] = {S8("vector_with_loop"), S8("vector_without_loop")};
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(vector_frame_names); name_index += 1)
        {
            IrFunction* vector_frame_function = codegen_test_c_function_find(vector_frame_module_ir, vector_frame_names[name_index]);
            BUSTER_TEST(arguments, vector_frame_function != 0);
            CodegenFunctionDescriptor* vector_frame_descriptor =
                vector_frame_function ? codegen_test_c_descriptor_find(&vector_frame_module, vector_frame_function->symbol) : 0;
            BUSTER_TEST(arguments, vector_frame_descriptor != 0);
            if (!vector_frame_descriptor || (u64)vector_frame_descriptor->code_offset + vector_frame_descriptor->code_size > vector_frame_module.code.length)
            {
                continue;
            }
            // The vector path is the only emission that addresses a frame slot
            // through r8/r9/r10, so `lea r8|r9|r10, [rbp+disp32]` names its
            // operand and result slots exactly.
            u8* vector_frame_code = vector_frame_module.code.pointer + vector_frame_descriptor->code_offset;
            u32 vector_frame_lea_count = 0;
            bool vector_frame_displacements_valid = true;
            for (u32 byte_index = 0; byte_index + 7 <= vector_frame_descriptor->code_size; byte_index += 1)
            {
                u8 modrm = vector_frame_code[byte_index + 2];
                if (vector_frame_code[byte_index] != 0x4c || vector_frame_code[byte_index + 1] != 0x8d ||
                    (modrm != 0x85 && modrm != 0x8d && modrm != 0x95))
                {
                    continue;
                }
                s32 displacement = (s32)((u32)vector_frame_code[byte_index + 3] | ((u32)vector_frame_code[byte_index + 4] << 8) |
                                         ((u32)vector_frame_code[byte_index + 5] << 16) | ((u32)vector_frame_code[byte_index + 6] << 24));
                vector_frame_lea_count += 1;
                // A Windows frame holding a stack restore is addressed upward
                // from its bottom; every other frame is addressed downward from
                // its top. Either way the slot has to be inside the frame.
                bool dynamic_frame = vector_frame_windows && string_equal(vector_frame_names[name_index], S8("vector_with_loop"));
                vector_frame_displacements_valid = vector_frame_displacements_valid && (dynamic_frame ? displacement >= 0 : displacement < 0);
            }
            BUSTER_TEST(arguments, vector_frame_lea_count >= 3);
            BUSTER_TEST(arguments, vector_frame_displacements_valid);
        }
    }
    String8 static_aggregate_c_source = S8(
        "typedef struct LargeAggregate { char padding[1024 * 1024]; int first; int second; } LargeAggregate;\n"
        "static LargeAggregate global_aggregate;\n"
        "int read_fields(void) {\n"
        "    return global_aggregate.first + global_aggregate.second + global_aggregate.first + global_aggregate.second +\n"
        "           global_aggregate.first + global_aggregate.second + global_aggregate.first + global_aggregate.second;\n"
        "}\n");
    Target static_aggregate_targets[] = {target, aarch64_target};
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(static_aggregate_targets); target_index += 1)
    {
        Target static_aggregate_target = static_aggregate_targets[target_index];
        CPreprocessResult static_aggregate_tokens = c_preprocess(arguments->arena, static_aggregate_c_source, (CPreprocessOptions){0});
        CParseResult static_aggregate_parse = c_parse(arguments->arena, static_aggregate_tokens);
        CIRLowerResult static_aggregate_ir = c_lower_to_ir(arguments->arena, S8("static-aggregate-member.c"), static_aggregate_tokens,
                                                           static_aggregate_parse, static_aggregate_target);
        BUSTER_TEST(arguments, static_aggregate_tokens.error_count == 0);
        BUSTER_TEST(arguments, static_aggregate_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, static_aggregate_ir.diagnostic_count == 0);
        if (!static_aggregate_ir.program)
        {
            continue;
        }
        IrProgram* static_aggregate_program = static_aggregate_ir.program;
        IrModule* static_aggregate_module = static_aggregate_program->modules;
        IrFunction* read_fields = codegen_test_c_function_find(static_aggregate_module, S8("read_fields"));
        IrValidationResult static_aggregate_validation = ir_validate_canonical_module(static_aggregate_program, static_aggregate_module);
        BUSTER_TEST(arguments, static_aggregate_validation.error == IR_VALIDATION_NONE);
        BUSTER_TEST(arguments, read_fields != 0);
        if (!read_fields)
        {
            continue;
        }
        u32 global_place_count = 0;
        u32 field_count = 0;
        u32 aggregate_load_count = 0;
        for (u32 instruction_index = 0; instruction_index < read_fields->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = read_fields->instructions + instruction_index;
            if (instruction->opcode == IR_OPCODE_GLOBAL && instruction->result.value < read_fields->value_count)
            {
                IrType* type = ir_type_from_id(&static_aggregate_program->types, read_fields->values[instruction->result.value].canonical_type);
                global_place_count += type && type->kind == IR_TYPE_STRUCT && type->layout.size >= BUSTER_MB(1);
            }
            field_count += instruction->opcode == IR_OPCODE_FIELD;
            if (instruction->opcode == IR_OPCODE_LOAD && instruction->result.value < read_fields->value_count)
            {
                IrType* type = ir_type_from_id(&static_aggregate_program->types, read_fields->values[instruction->result.value].canonical_type);
                aggregate_load_count += type && type->kind == IR_TYPE_STRUCT && type->layout.size >= BUSTER_MB(1);
            }
        }
        BUSTER_TEST(arguments, global_place_count == 8);
        BUSTER_TEST(arguments, field_count == 8);
        BUSTER_TEST(arguments, aggregate_load_count == 0);
        u32 expected_frame_size = codegen_test_canonical_value_frame_size(static_aggregate_program, read_fields);
        BUSTER_TEST(arguments, expected_frame_size < BUSTER_KB(64));
        CodegenModule static_aggregate_codegen = codegen_generate_canonical_module(arguments->arena, static_aggregate_program, static_aggregate_module,
                                                                                     static_aggregate_target, (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, static_aggregate_codegen.error == CODEGEN_ERROR_NONE);
        CodegenFunctionDescriptor* read_fields_descriptor = codegen_test_c_descriptor_find(&static_aggregate_codegen, read_fields->symbol);
        BUSTER_TEST(arguments, read_fields_descriptor != 0);
        u32 actual_frame_size = codegen_test_canonical_descriptor_stack_size(read_fields_descriptor);
        BUSTER_TEST(arguments, actual_frame_size < BUSTER_KB(64));
        BUSTER_TEST(arguments, actual_frame_size >= expected_frame_size);
    }
    String8 aggregate_copy_c_source = S8(
        "struct AggregatePair { int first; int second; };\n"
        "int copy_pair(void) {\n"
        "    struct AggregatePair source = {3, 4};\n"
        "    struct AggregatePair local = source;\n"
        "    local.first += 1;\n"
        "    return local.first + local.second;\n"
        "}\n");
    CPreprocessResult aggregate_copy_tokens = c_preprocess(arguments->arena, aggregate_copy_c_source, (CPreprocessOptions){0});
    CParseResult aggregate_copy_parse = c_parse(arguments->arena, aggregate_copy_tokens);
    CIRLowerResult aggregate_copy_ir = c_lower_to_ir(arguments->arena, S8("aggregate-value-copy.c"), aggregate_copy_tokens, aggregate_copy_parse, target);
    BUSTER_TEST(arguments, aggregate_copy_tokens.error_count == 0);
    BUSTER_TEST(arguments, aggregate_copy_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, aggregate_copy_ir.diagnostic_count == 0);
    if (aggregate_copy_ir.program)
    {
        IrProgram* aggregate_copy_program = aggregate_copy_ir.program;
        IrModule* aggregate_copy_module = aggregate_copy_program->modules;
        IrFunction* copy_pair = codegen_test_c_function_find(aggregate_copy_module, S8("copy_pair"));
        BUSTER_TEST(arguments, copy_pair != 0);
        if (copy_pair)
        {
            u32 aggregate_load_count = 0;
            u32 aggregate_store_count = 0;
            for (u32 instruction_index = 0; instruction_index < copy_pair->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = copy_pair->instructions + instruction_index;
                if (instruction->opcode == IR_OPCODE_LOAD && instruction->result.value < copy_pair->value_count)
                {
                    IrType* type = ir_type_from_id(&aggregate_copy_program->types, copy_pair->values[instruction->result.value].canonical_type);
                    aggregate_load_count += type && type->kind == IR_TYPE_STRUCT;
                }
                if (instruction->opcode == IR_OPCODE_STORE && instruction->operand_count >= 2 && instruction->operands[1].value < copy_pair->value_count)
                {
                    IrType* type = ir_type_from_id(&aggregate_copy_program->types, copy_pair->values[instruction->operands[1].value].canonical_type);
                    aggregate_store_count += type && type->kind == IR_TYPE_STRUCT;
                }
            }
            BUSTER_TEST(arguments, aggregate_load_count != 0);
            BUSTER_TEST(arguments, aggregate_store_count != 0);
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(aggregate_copy_program, aggregate_copy_module).error == IR_VALIDATION_NONE);
        CodegenModule aggregate_copy_codegen = codegen_generate_canonical_module(arguments->arena, aggregate_copy_program, aggregate_copy_module,
                                                                                    target, (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, aggregate_copy_codegen.error == CODEGEN_ERROR_NONE);
    }
    // A `.p2align` in global assembly can demand far more padding than the
    // directive's own source bytes, which is what the module code buffer's
    // reserve is otherwise sized from. Two page-sized alignments over a dozen
    // source bytes each is the case a source-length reserve cannot serve.
    String8 alignment_assembly_c_source = S8("extern void aligned_asm_body(void);\n"
                                             "__asm__(\".text\\n\"\n"
                                             "        \".globl aligned_asm_body\\n\"\n"
                                             "        \".p2align 12\\n\"\n"
                                             "        \"aligned_asm_body:\\n\"\n"
                                             "        \".byte 0xc3\\n\"\n"
                                             "        \".p2align 12\\n\"\n"
                                             "        \".byte 0xc3\\n\");\n"
                                             "void call_aligned_asm(void) { aligned_asm_body(); }\n");
    CPreprocessResult alignment_assembly_tokens = c_preprocess(arguments->arena, alignment_assembly_c_source, (CPreprocessOptions){0});
    CParseResult alignment_assembly_parse = c_parse(arguments->arena, alignment_assembly_tokens);
    CIRLowerResult alignment_assembly_ir =
        c_lower_to_ir(arguments->arena, S8("global-assembly-alignment.c"), alignment_assembly_tokens, alignment_assembly_parse, target);
    BUSTER_TEST(arguments, alignment_assembly_tokens.error_count == 0);
    BUSTER_TEST(arguments, alignment_assembly_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, alignment_assembly_ir.diagnostic_count == 0);
    if (alignment_assembly_ir.program)
    {
        IrProgram* alignment_assembly_program = alignment_assembly_ir.program;
        IrModule* alignment_assembly_module = alignment_assembly_program->modules;
        BUSTER_TEST(arguments, alignment_assembly_module->assembly_count == 1);
        IrSymbolId aligned_body_symbol = IR_SYMBOL_ID_INVALID;
        for (u32 symbol_index = 0; symbol_index < alignment_assembly_program->symbols.count; symbol_index += 1)
        {
            IrSymbol* symbol = alignment_assembly_program->symbols.symbols + symbol_index;
            String8 link_name = symbol->link_name.length ? symbol->link_name : symbol->name;
            if (string_equal(link_name, S8("aligned_asm_body")))
            {
                aligned_body_symbol = (IrSymbolId){
                    .value = symbol_index,
                };
                break;
            }
        }
        BUSTER_TEST(arguments, aligned_body_symbol.value != IR_ID_UNDERLYING_INVALID);
        CodegenModule alignment_assembly_codegen = codegen_generate_canonical_module(arguments->arena, alignment_assembly_program, alignment_assembly_module,
                                                                                        target, (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, alignment_assembly_codegen.error == CODEGEN_ERROR_NONE);
        CodegenFunctionDescriptor* aligned_body_descriptor =
            alignment_assembly_codegen.error == CODEGEN_ERROR_NONE ? codegen_test_c_descriptor_find(&alignment_assembly_codegen, aligned_body_symbol) : 0;
        BUSTER_TEST(arguments, aligned_body_descriptor != 0);
        if (aligned_body_descriptor)
        {
            // The label lands on the boundary the directive before it asked
            // for, its own byte follows it, and the second directive carries
            // the byte after that to the next boundary.
            u64 aligned_body_offset = aligned_body_descriptor->code_offset;
            BUSTER_TEST(arguments, (aligned_body_offset & (BUSTER_KB(4) - 1)) == 0);
            BUSTER_TEST(arguments, aligned_body_offset != 0);
            BUSTER_TEST(arguments, alignment_assembly_codegen.code.length > aligned_body_offset + BUSTER_KB(4));
            if (alignment_assembly_codegen.code.length > aligned_body_offset + BUSTER_KB(4))
            {
                BUSTER_TEST(arguments, alignment_assembly_codegen.code.pointer[aligned_body_offset] == 0xc3);
                BUSTER_TEST(arguments, alignment_assembly_codegen.code.pointer[aligned_body_offset + 1] == 0x90);
                BUSTER_TEST(arguments, alignment_assembly_codegen.code.pointer[aligned_body_offset + BUSTER_KB(4) - 1] == 0x90);
                BUSTER_TEST(arguments, alignment_assembly_codegen.code.pointer[aligned_body_offset + BUSTER_KB(4)] == 0xc3);
            }
        }
    }
    String8 deterministic_merge_source = S8(
        "extern int deterministic_asm_value(void);\n"
        "#if defined(__x86_64__) || defined(_M_X64)\n"
        "__asm__(\".text\\n.p2align 12\\n.globl deterministic_asm_value\\n.type deterministic_asm_value, @function\\n\""
        "        \"deterministic_asm_value:\\nmovl $7, %eax\\nret\\n\");\n"
        "#elif defined(__aarch64__) || defined(_M_ARM64)\n"
        "__asm__(\".text\\n.p2align 12\\n.globl deterministic_asm_value\\n.type deterministic_asm_value, %function\\n\""
        "        \"deterministic_asm_value:\\nmov w0, #7\\nret\\n\");\n"
        "#endif\n"
        "static int deterministic_dispatch(int selector) {\n"
        "    static void *targets[] = {&&zero, &&one};\n"
        "    goto *targets[selector & 1];\n"
        "zero: return 11;\n"
        "one: return 13;\n"
        "}\n"
        "static int deterministic_helper(int value) { return value + 3; }\n"
        "int deterministic_merge(int selector) {\n"
        "    return deterministic_helper(deterministic_dispatch(selector)) + deterministic_asm_value();\n"
        "}\n");
    CPreprocessResult deterministic_merge_tokens = c_preprocess(arguments->arena, deterministic_merge_source, (CPreprocessOptions){0});
    CParseResult deterministic_merge_parse = c_parse(arguments->arena, deterministic_merge_tokens);
    CIRLowerResult deterministic_merge_ir = c_lower_to_ir(arguments->arena, S8("deterministic-merge.c"), deterministic_merge_tokens,
                                                           deterministic_merge_parse, target);
    BUSTER_TEST(arguments, deterministic_merge_tokens.error_count == 0);
    BUSTER_TEST(arguments, deterministic_merge_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, deterministic_merge_ir.diagnostic_count == 0);
    if (deterministic_merge_ir.program)
    {
        IrProgram* deterministic_program = deterministic_merge_ir.program;
        IrModule* deterministic_module = deterministic_program->modules;
        BUSTER_TEST(arguments, ir_validate_canonical_module(deterministic_program, deterministic_module).error == IR_VALIDATION_NONE);
        CodegenModule deterministic_serial = codegen_generate_canonical_module(
            arguments->arena, deterministic_program, deterministic_module, target,
            (CodegenModuleOptions){
                .lane_count = 1,
                .debug_info = true,
                .assume_validated = true,
            });
        CodegenModule deterministic_parallel = codegen_generate_canonical_module(
            arguments->arena, deterministic_program, deterministic_module, target,
            (CodegenModuleOptions){
                .lane_count = 3,
                .debug_info = true,
                .assume_validated = true,
            });
        BUSTER_TEST(arguments, deterministic_serial.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, deterministic_parallel.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, deterministic_serial.code.length >= 4096);
        if (deterministic_serial.error == CODEGEN_ERROR_NONE && deterministic_parallel.error == CODEGEN_ERROR_NONE)
        {
            ObjectFile serial_object = object_from_canonical_codegen_module(arguments->arena, deterministic_program, &deterministic_serial, target);
            ObjectFile parallel_object = object_from_canonical_codegen_module(arguments->arena, deterministic_program, &deterministic_parallel, target);
            ObjectFormat format = object_format_for_target(target);
            ObjectArtifact serial_artifact = object_write(arguments->arena, &serial_object, format);
            ObjectArtifact parallel_artifact = object_write(arguments->arena, &parallel_object, format);
            BUSTER_TEST(arguments, serial_artifact.error == OBJECT_ERROR_NONE);
            BUSTER_TEST(arguments, parallel_artifact.error == OBJECT_ERROR_NONE);
            BUSTER_TEST(arguments, serial_artifact.bytes.length == parallel_artifact.bytes.length);
            if (serial_artifact.bytes.length == parallel_artifact.bytes.length)
            {
                BUSTER_TEST(arguments, memcmp(serial_artifact.bytes.pointer, parallel_artifact.bytes.pointer, serial_artifact.bytes.length) == 0);
            }
        }
    }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
    // Promote a C frontend module whose source uses ordinary double values.
    // The frontend deliberately rejects wide-float signatures, so this keeps
    // the parser/lowering contract intact while exercising the canonical
    // x87 emitter and the host compiler's independent SysV long-double ABI.
    if (target.os == OPERATING_SYSTEM_LINUX || target.os == OPERATING_SYSTEM_MACOS)
    {
        String8 f80_native_source = S8(
            "extern double f80_host_probe(double first, double second);\n"
            "double f80_identity(double value) { return value; }\n"
            "double f80_forward(double value) { return f80_identity(value); }\n"
            "double f80_second(double first, double second) { return second; }\n"
            "double f80_forward2(double first, double second) { return f80_second(first, second); }\n"
            "double f80_host_wrapper(double first, double second) { return f80_host_probe(first, second); }\n"
            "double f80_constant(void) { return 1.0; }\n"
            "long long f80_after_ints(long long a, long long b, long long c, long long d, long long e, long long f, long long g,\n"
            "                           double first, double second, long long tail) { return tail; }\n"
            "long long f80_after_ints_caller(void) {\n"
            "    return f80_after_ints(1, 2, 3, 4, 5, 6, 7, 1.0, 2.0, 0x123456789abcdefLL);\n"
            "}\n");
        CPreprocessResult f80_native_tokens = c_preprocess(arguments->arena, f80_native_source, (CPreprocessOptions){0});
        CParseResult f80_native_parse = c_parse(arguments->arena, f80_native_tokens);
        CIRLowerResult f80_native_ir = c_lower_to_ir(arguments->arena, S8("canonical-f80-native.c"), f80_native_tokens, f80_native_parse, target);
        BUSTER_TEST(arguments, f80_native_tokens.error_count == 0);
        BUSTER_TEST(arguments, f80_native_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, f80_native_ir.diagnostic_count == 0);
        if (f80_native_ir.program)
        {
            IrProgram* f80_native_program = f80_native_ir.program;
            IrModule* f80_native_module = f80_native_program->modules;
            BUSTER_TEST(arguments, ir_validate_canonical_module(f80_native_program, f80_native_module).error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, codegen_test_promote_canonical_f64_to_f80(f80_native_program));
            IrValidationResult f80_native_validation = ir_validate_canonical_module(f80_native_program, f80_native_module);
            BUSTER_TEST(arguments, f80_native_validation.error == IR_VALIDATION_NONE);
            CodegenModule f80_native_codegen = codegen_generate_canonical_module(
                arguments->arena, f80_native_program, f80_native_module, target,
                (CodegenModuleOptions){
                    .lane_count = 1,
                    .assume_validated = true,
                });
            BUSTER_TEST(arguments, f80_native_codegen.error == CODEGEN_ERROR_NONE);
            BUSTER_TEST(arguments, sizeof(CodegenTestHostF80) == 16);
            if (f80_native_codegen.error == CODEGEN_ERROR_NONE && sizeof(CodegenTestHostF80) == 16)
            {
                // Canonical module relocations are intentionally retained for
                // object/link writers.  For this in-process differential,
                // resolve the local call sites against the module entries
                // before exposing the code as executable memory.
                codegen_test_patch_local_canonical_calls(&f80_native_codegen);
                IrFunction* host_wrapper_function = codegen_test_c_function_find(f80_native_module, S8("f80_host_wrapper"));
                CodegenFunctionDescriptor* host_wrapper_descriptor =
                    host_wrapper_function ? codegen_test_c_descriptor_find(&f80_native_codegen, host_wrapper_function->symbol) : 0;
                BUSTER_TEST(arguments, host_wrapper_descriptor != 0);
                u8* f80_native_executable_code = f80_native_codegen.code.pointer;
                u64 f80_native_executable_code_length = f80_native_codegen.code.length;
                bool f80_host_relocation_patched = false;
                if (host_wrapper_descriptor && f80_native_codegen.code.length <= UINT64_MAX - 14)
                {
                    // Keep the generated call's normal rel32 relocation, but
                    // point it at an in-image RIP-indirect trampoline.  The
                    // trampoline's absolute target is the host C helper, so
                    // this exercises generated-to-host argument marshalling
                    // without asking the executable mapper to expose a host
                    // symbol or to relax a short displacement.
                    u64 trampoline_offset = f80_native_codegen.code.length;
                    f80_native_executable_code_length += 14;
                    f80_native_executable_code = arena_allocate(arguments->arena, u8, f80_native_executable_code_length);
                    memcpy(f80_native_executable_code, f80_native_codegen.code.pointer, f80_native_codegen.code.length);
                    u8 trampoline[14] = {0xff, 0x25, 0, 0, 0, 0};
                    CodegenTestHostF80Function2* host_probe_function = codegen_test_host_f80_probe;
                    u64 host_probe_address = 0;
                    memcpy(&host_probe_address, &host_probe_function, sizeof(host_probe_address));
                    memcpy(trampoline + 6, &host_probe_address, sizeof(host_probe_address));
                    memcpy(f80_native_executable_code + trampoline_offset, trampoline, sizeof(trampoline));
                    for (u32 relocation_index = 0; relocation_index < f80_native_codegen.relocation_count; relocation_index += 1)
                    {
                        CodegenModuleRelocation* relocation = f80_native_codegen.relocations + relocation_index;
                        IrSymbol* symbol = relocation->symbol.value < f80_native_program->symbols.count
                                               ? f80_native_program->symbols.symbols + relocation->symbol.value
                                               : 0;
                        String8 symbol_name = symbol && symbol->link_name.length ? symbol->link_name : symbol ? symbol->name : (String8){0};
                        if (relocation->source != CODEGEN_MODULE_RELOCATION_CODE || relocation->absolute || relocation->aarch64 ||
                            !string_equal(symbol_name, S8("f80_host_probe")) || relocation->offset > f80_native_codegen.code.length ||
                            f80_native_codegen.code.length - relocation->offset < 4)
                        {
                            continue;
                        }
                        s64 displacement = (s64)trampoline_offset - ((s64)relocation->offset + 4);
                        if (displacement < INT32_MIN || displacement > INT32_MAX)
                        {
                            continue;
                        }
                        s32 encoded_displacement = (s32)displacement;
                        memcpy(f80_native_executable_code + relocation->offset, &encoded_displacement, sizeof(encoded_displacement));
                        f80_host_relocation_patched = true;
                        break;
                    }
                }
                BUSTER_TEST(arguments, f80_host_relocation_patched);
                CodegenExecutable f80_native_executable = codegen_make_executable((CodegenFunction){
                    .code = {.pointer = f80_native_executable_code, .length = f80_native_executable_code_length},
                    .error = f80_native_codegen.error,
                });
                BUSTER_TEST(arguments, f80_native_executable.error == CODEGEN_ERROR_NONE);
                if (f80_native_executable.address && f80_host_relocation_patched)
                {
                    IrFunction* identity_function = codegen_test_c_function_find(f80_native_module, S8("f80_identity"));
                    IrFunction* forward_function = codegen_test_c_function_find(f80_native_module, S8("f80_forward"));
                    IrFunction* second_function = codegen_test_c_function_find(f80_native_module, S8("f80_second"));
                    IrFunction* forward2_function = codegen_test_c_function_find(f80_native_module, S8("f80_forward2"));
                    IrFunction* constant_function = codegen_test_c_function_find(f80_native_module, S8("f80_constant"));
                    IrFunction* layout_function = codegen_test_c_function_find(f80_native_module, S8("f80_after_ints"));
                    IrFunction* layout_caller_function = codegen_test_c_function_find(f80_native_module, S8("f80_after_ints_caller"));
                    CodegenFunctionDescriptor* identity_descriptor =
                        identity_function ? codegen_test_c_descriptor_find(&f80_native_codegen, identity_function->symbol) : 0;
                    CodegenFunctionDescriptor* forward_descriptor =
                        forward_function ? codegen_test_c_descriptor_find(&f80_native_codegen, forward_function->symbol) : 0;
                    CodegenFunctionDescriptor* second_descriptor =
                        second_function ? codegen_test_c_descriptor_find(&f80_native_codegen, second_function->symbol) : 0;
                    CodegenFunctionDescriptor* forward2_descriptor =
                        forward2_function ? codegen_test_c_descriptor_find(&f80_native_codegen, forward2_function->symbol) : 0;
                    CodegenFunctionDescriptor* constant_descriptor =
                        constant_function ? codegen_test_c_descriptor_find(&f80_native_codegen, constant_function->symbol) : 0;
                    CodegenFunctionDescriptor* layout_descriptor =
                        layout_function ? codegen_test_c_descriptor_find(&f80_native_codegen, layout_function->symbol) : 0;
                    CodegenFunctionDescriptor* layout_caller_descriptor =
                        layout_caller_function ? codegen_test_c_descriptor_find(&f80_native_codegen, layout_caller_function->symbol) : 0;
                    BUSTER_TEST(arguments,
                                identity_descriptor && forward_descriptor && second_descriptor && forward2_descriptor && host_wrapper_descriptor &&
                                    constant_descriptor && layout_descriptor && layout_caller_descriptor);
                    if (identity_descriptor && forward_descriptor && second_descriptor && forward2_descriptor && host_wrapper_descriptor && constant_descriptor &&
                        layout_descriptor && layout_caller_descriptor)
                    {
                        void* identity_address = (u8*)f80_native_executable.address + identity_descriptor->code_offset;
                        void* forward_address = (u8*)f80_native_executable.address + forward_descriptor->code_offset;
                        void* second_address = (u8*)f80_native_executable.address + second_descriptor->code_offset;
                        void* forward2_address = (u8*)f80_native_executable.address + forward2_descriptor->code_offset;
                        void* host_wrapper_address = (u8*)f80_native_executable.address + host_wrapper_descriptor->code_offset;
                        void* constant_address = (u8*)f80_native_executable.address + constant_descriptor->code_offset;
                        void* layout_address = (u8*)f80_native_executable.address + layout_descriptor->code_offset;
                        void* layout_caller_address = (u8*)f80_native_executable.address + layout_caller_descriptor->code_offset;
                        CodegenTestHostF80Function1* native_identity = 0;
                        CodegenTestHostF80Function1* native_forward = 0;
                        CodegenTestHostF80Function2* native_second = 0;
                        CodegenTestHostF80Function2* native_forward2 = 0;
                        CodegenTestHostF80Function2* native_host_wrapper = 0;
                        CodegenTestHostF80Function0* native_constant = 0;
                        CodegenTestHostF80LayoutFunction* native_layout = 0;
                        s64 (*native_layout_caller)(void) = 0;
                        memcpy(&native_identity, &identity_address, sizeof(native_identity));
                        memcpy(&native_forward, &forward_address, sizeof(native_forward));
                        memcpy(&native_second, &second_address, sizeof(native_second));
                        memcpy(&native_forward2, &forward2_address, sizeof(native_forward2));
                        memcpy(&native_host_wrapper, &host_wrapper_address, sizeof(native_host_wrapper));
                        memcpy(&native_constant, &constant_address, sizeof(native_constant));
                        memcpy(&native_layout, &layout_address, sizeof(native_layout));
                        memcpy(&native_layout_caller, &layout_caller_address, sizeof(native_layout_caller));
                        struct
                        {
                            u64 significand;
                            u16 sign_exponent;
                        } f80_cases[] = {
                            {UINT64_C(0x8000000000000000), 0x3fff}, // +1
                            {UINT64_C(0x0000000000000000), 0x8000}, // -0
                            {UINT64_C(0x0000000000000001), 0x0000}, // minimum subnormal
                            {UINT64_C(0x8000000000000000), 0x7fff}, // +infinity
                            {UINT64_C(0xc000000000000001), 0x7fff}, // quiet NaN payload
                        };
                        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(f80_cases); case_index += 1)
                        {
                            CodegenTestHostF80 f80_input = 0;
                            codegen_test_host_f80_set(&f80_input, f80_cases[case_index].significand, f80_cases[case_index].sign_exponent);
                            CodegenTestHostF80 identity_output = native_identity(f80_input);
                            CodegenTestHostF80 forward_output = native_forward(f80_input);
                            CodegenTestHostF80 second_input = 0;
                            codegen_test_host_f80_set(&second_input, f80_cases[(case_index + 1) % BUSTER_ARRAY_LENGTH(f80_cases)].significand,
                                                     f80_cases[(case_index + 1) % BUSTER_ARRAY_LENGTH(f80_cases)].sign_exponent);
                            CodegenTestHostF80 second_output = native_second(f80_input, second_input);
                            CodegenTestHostF80 forward2_output = native_forward2(f80_input, second_input);
                            BUSTER_TEST(arguments, codegen_test_host_f80_semantic_equal(identity_output, f80_cases[case_index].significand,
                                                                                         f80_cases[case_index].sign_exponent));
                            BUSTER_TEST(arguments, codegen_test_host_f80_semantic_equal(forward_output, f80_cases[case_index].significand,
                                                                                         f80_cases[case_index].sign_exponent));
                            BUSTER_TEST(arguments,
                                        codegen_test_host_f80_semantic_equal(second_output,
                                                                             f80_cases[(case_index + 1) % BUSTER_ARRAY_LENGTH(f80_cases)].significand,
                                                                             f80_cases[(case_index + 1) % BUSTER_ARRAY_LENGTH(f80_cases)].sign_exponent));
                            BUSTER_TEST(arguments,
                                        codegen_test_host_f80_semantic_equal(forward2_output,
                                                                             f80_cases[(case_index + 1) % BUSTER_ARRAY_LENGTH(f80_cases)].significand,
                                                                             f80_cases[(case_index + 1) % BUSTER_ARRAY_LENGTH(f80_cases)].sign_exponent));
                        }
                        CodegenTestHostF80 constant_output = native_constant();
                        BUSTER_TEST(arguments, codegen_test_host_f80_semantic_equal(constant_output, UINT64_C(0x8000000000000000), 0x3fff));
                        CodegenTestHostF80 first = 0;
                        CodegenTestHostF80 second = 0;
                        codegen_test_host_f80_set(&first, UINT64_C(0x0000000000000001), 0x0000);
                        codegen_test_host_f80_set(&second, UINT64_C(0xc000000000000001), 0x7fff);
                        CodegenTestHostF80 host_probe_output = native_host_wrapper(first, second);
                        BUSTER_TEST(arguments, codegen_test_host_f80_semantic_equal(host_probe_output, UINT64_C(0xc000000000000001), 0x7fff));
                        s64 expected_tail = INT64_C(0x123456789abcdef);
                        BUSTER_TEST(arguments, native_layout(1, 2, 3, 4, 5, 6, 7, first, second, expected_tail) == expected_tail);
                        BUSTER_TEST(arguments, native_layout_caller() == expected_tail);
                    }
                    codegen_release_executable(f80_native_executable);
                }
            }
        }
    }
#endif
    BUSTER_CHECK(arena_destroy(expression_arena, 1));
    arena_set_position(temporary.arena, temporary.position);
    return result;
}
#endif
