#include <buster/tests/compiler/codegen/codegen_test.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
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
    bool result;
    if (!program)
    {
        result = false;
    }
    else
    {
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
        result = promoted;
    }

    return result;
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

BUSTER_GLOBAL_LOCAL CodegenModuleGlobal* codegen_test_c_global_find(CodegenModule* module, IrProgram* program, String8 name)
{
    CodegenModuleGlobal* result = 0;
    for (u32 index = 0; index < module->global_count; index += 1)
    {
        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, module->globals[index].symbol);
        if (!result && symbol && string_equal(symbol->name, name))
        {
            result = module->globals + index;
        }
    }
    return result;
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
    bool result;
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
        result = true;
        break;
    default:
        result = false;
        break;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool codegen_test_x64_modrm_reg_writes(u8 opcode)
{
    bool result;
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
        result = true;
        break;
    default:
        result = false;
        break;
    }

    return result;
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

BUSTER_GLOBAL_LOCAL u32 codegen_test_x64_vector_frame_lea_count(ByteSlice code, bool dynamic_frame, bool* displacements_valid)
{
    u32 result = 0;
    bool valid = true;
    if (!displacements_valid)
    {
        return 0;
    }
    for (u64 byte_index = 0; byte_index + 3 <= code.length; byte_index += 1)
    {
        // The canonical vector path uses REX.WRX (4c) LEA into r8/r9/r10
        // from rbp. Accept either ModRM displacement width: the metadata
        // emitter is allowed to use disp8 when the frame slot fits it.
        if (code.pointer[byte_index] != 0x4c || code.pointer[byte_index + 1] != 0x8d)
        {
            continue;
        }
        u8 modrm = code.pointer[byte_index + 2];
        u8 mod = modrm >> 6;
        u8 reg = (modrm >> 3) & 7;
        if (mod == 3 || (modrm & 7) != 5 || reg > 2)
        {
            continue;
        }
        s32 displacement = 0;
        if (mod == 1)
        {
            if (byte_index + 4 > code.length)
            {
                continue;
            }
            displacement = (s8)code.pointer[byte_index + 3];
        }
        else if (mod == 2)
        {
            if (byte_index + 7 > code.length)
            {
                continue;
            }
            memcpy(&displacement, code.pointer + byte_index + 3, sizeof(displacement));
        }
        else
        {
            // ModRM mod=0/rm=5 is RIP-relative, not a frame slot.
            continue;
        }
        result += 1;
        valid &= dynamic_frame ? displacement >= 0 : displacement < 0;
    }
    *displacements_valid = valid;
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
    Target target = target_native;
    target.cpu_arch = CPU_ARCH_X86_64;
    // This target drives the System V stack-argument test below. Keep its ABI
    // fixed when the test executable itself was built on Windows.
    Target avx512f_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_LINUX,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_from_array((TargetCpuFeature const[])
        {
            TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX, TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AVX512F,
        }, 4),
    };
    Target aarch64_target = target;
    aarch64_target.cpu_arch = CPU_ARCH_AARCH64;
    aarch64_target.cpu_features_explicit = true;
    aarch64_target.cpu_features = target_cpu_features_singleton(TARGET_CPU_FEATURE_AARCH64_NEON);
    Target canonical_windows_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_WINDOWS,
    };
    // Scalar frame/memory emission in the canonical backend is routed through
    // the same checked physical metadata encoder used here.  Keep a few
    // representative ModRM/SIB shapes as a byte-level differential oracle:
    // frame-pointer disp8, byte zero-extension through RSP, and an extended
    // register base/source through R8.  These checks deliberately exercise
    // shortest-form displacement selection rather than the old forced-disp32
    // spelling.
    {
        BusterX86MetadataPhysicalOperand mov_rbp_operands[2] = {
            {
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
                .width = 64,
                .reg = {
                    .index = 0,
                    .width = 64,
                    .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
                },
            },
            {
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
                .width = 64,
                .memory = {
                    .base = {
                        .index = 5,
                        .width = 64,
                        .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
                    },
                    .displacement = 8,
                    .address_size = 64,
                    .scale = 1,
                    .has_base = true,
                    .has_displacement = true,
                },
            },
        };
        u8 mov_rbp_bytes[16] = {0};
        BusterX86MetadataEmitResult mov_rbp_result = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = {
                .mnemonic = S8("MOV"),
                .operands = mov_rbp_operands,
                .operand_count = BUSTER_ARRAY_LENGTH(mov_rbp_operands),
                .address_size = 64,
                .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
            },
            .output = mov_rbp_bytes,
            .output_capacity = sizeof(mov_rbp_bytes),
        });
        u8 expected_mov_rbp[] = {0x48, 0x8b, 0x45, 0x08};
        BUSTER_TEST(arguments, mov_rbp_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS);
        BUSTER_TEST(arguments, mov_rbp_result.byte_count == sizeof(expected_mov_rbp));
        BUSTER_TEST(arguments, !memcmp(mov_rbp_bytes, expected_mov_rbp, sizeof(expected_mov_rbp)));

        BusterX86MetadataPhysicalOperand movzx_rsp_operands[2] = {
            {
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
                .width = 32,
                .reg = {
                    .index = 0,
                    .width = 32,
                    .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
                },
            },
            {
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
                .width = 8,
                .memory = {
                    .base = {
                        .index = 4,
                        .width = 64,
                        .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
                    },
                    .displacement = 16,
                    .address_size = 64,
                    .scale = 1,
                    .has_base = true,
                    .has_displacement = true,
                },
            },
        };
        u8 movzx_rsp_bytes[16] = {0};
        BusterX86MetadataEmitResult movzx_rsp_result = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = {
                .mnemonic = S8("MOVZX"),
                .operands = movzx_rsp_operands,
                .operand_count = BUSTER_ARRAY_LENGTH(movzx_rsp_operands),
                .address_size = 64,
                .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
            },
            .output = movzx_rsp_bytes,
            .output_capacity = sizeof(movzx_rsp_bytes),
        });
        u8 expected_movzx_rsp[] = {0x0f, 0xb6, 0x44, 0x24, 0x10};
        BUSTER_TEST(arguments, movzx_rsp_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS);
        BUSTER_TEST(arguments, movzx_rsp_result.byte_count == sizeof(expected_movzx_rsp));
        BUSTER_TEST(arguments, !memcmp(movzx_rsp_bytes, expected_movzx_rsp, sizeof(expected_movzx_rsp)));

        BusterX86MetadataPhysicalOperand mov_r8_operands[2] = {
            {
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
                .width = 64,
                .memory = {
                    .base = {
                        .index = 8,
                        .width = 64,
                        .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
                    },
                    .displacement = 32,
                    .address_size = 64,
                    .scale = 1,
                    .has_base = true,
                    .has_displacement = true,
                },
            },
            {
                .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
                .width = 64,
                .reg = {
                    .index = 8,
                    .width = 64,
                    .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
                },
            },
        };
        u8 mov_r8_bytes[16] = {0};
        BusterX86MetadataEmitResult mov_r8_result = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
            .physical = {
                .mnemonic = S8("MOV"),
                .operands = mov_r8_operands,
                .operand_count = BUSTER_ARRAY_LENGTH(mov_r8_operands),
                .address_size = 64,
                .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
            },
            .output = mov_r8_bytes,
            .output_capacity = sizeof(mov_r8_bytes),
        });
        u8 expected_mov_r8[] = {0x4d, 0x89, 0x40, 0x20};
        BUSTER_TEST(arguments, mov_r8_result.status == BUSTER_X86_METADATA_ENCODE_SUCCESS);
        BUSTER_TEST(arguments, mov_r8_result.byte_count == sizeof(expected_mov_r8));
        BUSTER_TEST(arguments, !memcmp(mov_r8_bytes, expected_mov_r8, sizeof(expected_mov_r8)));
    }
    // The optimized canonical path must carry exact-form telemetry from the
    // C frontend all the way through machine emission.  This source contains
    // exactly one MFENCE-producing seq_cst fence and one INT3-producing debug
    // trap, plus the scalar FAMILY rematerialization rows now migrated to the
    // exact bridge, so FAST reports four exact attempts with no fallback.
    String8 exact_encoder_stats_source = S8(
        "void exact_encoder_stats(void) {\n"
        "    __c11_atomic_thread_fence(__ATOMIC_SEQ_CST);\n"
        "    __builtin_debugtrap();\n"
        "}\n");
    Target exact_encoder_stats_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_LINUX,
    };
    CPreprocessResult exact_encoder_stats_tokens = c_preprocess(arguments->arena, exact_encoder_stats_source, (CPreprocessOptions){0});
    CParseResult exact_encoder_stats_parse = c_parse(arguments->arena, exact_encoder_stats_tokens);
    CIRLowerResult exact_encoder_stats_ir =
        c_lower_to_ir(arguments->arena, S8("exact-encoder-stats.c"), exact_encoder_stats_tokens, exact_encoder_stats_parse, exact_encoder_stats_target);
    BUSTER_TEST(arguments, exact_encoder_stats_tokens.error_count == 0);
    BUSTER_TEST(arguments, exact_encoder_stats_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, exact_encoder_stats_ir.diagnostic_count == 0);
    if (exact_encoder_stats_ir.program)
    {
        IrModule* exact_encoder_stats_module_ir = exact_encoder_stats_ir.program->modules;
        BUSTER_TEST(arguments, ir_validate_canonical_module(exact_encoder_stats_ir.program, exact_encoder_stats_module_ir).error == IR_VALIDATION_NONE);
        CodegenModule exact_encoder_stats_module = codegen_generate_canonical_module(
            arguments->arena, exact_encoder_stats_ir.program, exact_encoder_stats_module_ir, exact_encoder_stats_target,
            (CodegenModuleOptions){
                .register_allocator = CODEGEN_REGISTER_ALLOCATOR_FAST,
            });
        BUSTER_TEST(arguments, exact_encoder_stats_module.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, exact_encoder_stats_module.statistics.exact_attempts == 4);
        BUSTER_TEST(arguments, exact_encoder_stats_module.statistics.exact_successes == 4);
        BUSTER_TEST(arguments, exact_encoder_stats_module.statistics.exact_failures == 0);
    }
    // Keep a canonical-IR witness for the AArch64 i128 count extension. The
    // GNU clzll/ctzll spellings normally describe 64-bit operands; Buster's
    // frontend deliberately preserves an i128 operand width, so this test
    // checks the IR type directly and then runs the canonical emitter. The
    // runtime fixture exercises the resulting pair stores separately.
    String8 i128_count_source = S8(
        "typedef unsigned __int128 U128;\n"
        "void count_i128(U128 value, U128 *leading, U128 *trailing) {\n"
        "    *leading = __builtin_clzll(value);\n"
        "    *trailing = __builtin_ctzll(value);\n"
        "}\n");
    Target i128_count_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .cpu_model = CPU_MODEL_A64_GENERIC,
        .os = OPERATING_SYSTEM_LINUX,
    };
    CPreprocessResult i128_count_tokens = c_preprocess(arguments->arena, i128_count_source, (CPreprocessOptions){0});
    CParseResult i128_count_parse = c_parse(arguments->arena, i128_count_tokens);
    CIRLowerResult i128_count_ir = c_lower_to_ir(arguments->arena, S8("aarch64-i128-count.c"), i128_count_tokens, i128_count_parse, i128_count_target);
    BUSTER_TEST(arguments, i128_count_tokens.error_count == 0);
    BUSTER_TEST(arguments, i128_count_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, i128_count_ir.diagnostic_count == 0);
    if (i128_count_ir.program)
    {
        bool saw_leading = false;
        bool saw_trailing = false;
        for (u32 module_index = 0; module_index < i128_count_ir.program->module_count; module_index += 1)
        {
            IrModule* module = i128_count_ir.program->modules + module_index;
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = function->instructions + instruction_index;
                    bool count_instruction = instruction->opcode == IR_OPCODE_UNARY &&
                                             (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS ||
                                              instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS);
                    if (!count_instruction)
                    {
                        continue;
                    }
                    IrType* result_type = ir_type_from_id(&i128_count_ir.program->types, instruction->canonical_type);
                    IrType* operand_type = instruction->operand_count ?
                                               ir_type_from_id(&i128_count_ir.program->types,
                                                               function->values[instruction->operands[0].value].canonical_type)
                                                                     : 0;
                    BUSTER_TEST(arguments, result_type && result_type->kind == IR_TYPE_INTEGER && result_type->bit_width == 128);
                    BUSTER_TEST(arguments, operand_type && operand_type->kind == IR_TYPE_INTEGER && operand_type->bit_width == 128);
                    saw_leading |= instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS;
                    saw_trailing |= instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS;
                }
            }
        }
        BUSTER_TEST(arguments, saw_leading && saw_trailing);
        BUSTER_TEST(arguments, ir_validate_canonical_module(i128_count_ir.program, i128_count_ir.program->modules).error == IR_VALIDATION_NONE);
        CodegenModule i128_count_codegen = codegen_generate_canonical_module(
            arguments->arena, i128_count_ir.program, i128_count_ir.program->modules, i128_count_target,
            (CodegenModuleOptions){.register_allocator = CODEGEN_REGISTER_ALLOCATOR_NONE});
        BUSTER_TEST(arguments, i128_count_codegen.error == CODEGEN_ERROR_NONE);
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
                    .assume_validated = true,
                });
            BUSTER_TEST(arguments, small_module.error == CODEGEN_ERROR_NONE);
            BUSTER_TEST(arguments, arena_destroy(small_canonical_arena, 1));
        }
        CodegenModule canonical_windows_module = codegen_generate_canonical_module(arguments->arena, canonical_program, canonical_module,
                                                                                    canonical_windows_target, (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, canonical_windows_module.error == CODEGEN_ERROR_NONE);
        // Generating the same module twice must produce the same complete
        // COFF/CodeView/unwind artifact. Debug output is the strongest
        // aggregate oracle because it also consumes line rows, location seeds,
        // descriptors, symbols, and relocation order, so a value read before it
        // was written shows up here rather than as a rare mismatch downstream.
        CodegenModule canonical_windows_first = codegen_generate_canonical_module(
            arguments->arena, canonical_program, canonical_module, canonical_windows_target,
            (CodegenModuleOptions){
                .debug_info = true,
                .assume_validated = true,
            });
        CodegenModule canonical_windows_second = codegen_generate_canonical_module(
            arguments->arena, canonical_program, canonical_module, canonical_windows_target,
            (CodegenModuleOptions){
                .debug_info = true,
                .assume_validated = true,
            });
        BUSTER_TEST(arguments, canonical_windows_first.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, canonical_windows_second.error == CODEGEN_ERROR_NONE);
        if (canonical_windows_first.error == CODEGEN_ERROR_NONE && canonical_windows_second.error == CODEGEN_ERROR_NONE)
        {
            ObjectFile first_object =
                object_from_canonical_codegen_module(arguments->arena, canonical_program, &canonical_windows_first, canonical_windows_target);
            ObjectFile second_object =
                object_from_canonical_codegen_module(arguments->arena, canonical_program, &canonical_windows_second, canonical_windows_target);
            ObjectArtifact first_artifact = object_write(arguments->arena, &first_object, object_format_for_target(canonical_windows_target));
            ObjectArtifact second_artifact = object_write(arguments->arena, &second_object, object_format_for_target(canonical_windows_target));
            BUSTER_TEST(arguments, first_artifact.error == OBJECT_ERROR_NONE);
            BUSTER_TEST(arguments, second_artifact.error == OBJECT_ERROR_NONE);
            BUSTER_TEST(arguments, first_artifact.bytes.length == second_artifact.bytes.length);
            if (first_artifact.bytes.length == second_artifact.bytes.length)
            {
                BUSTER_TEST(arguments, memcmp(first_artifact.bytes.pointer, second_artifact.bytes.pointer, first_artifact.bytes.length) == 0);
            }
            BUSTER_TEST(arguments, canonical_windows_first.statistics.function_count == canonical_windows_second.statistics.function_count);
            BUSTER_TEST(arguments, canonical_windows_first.statistics.code_bytes == canonical_windows_second.statistics.code_bytes);
        }
        // The canonical x86 template cache is allocated in the caller's
        // arena.  A fresh mapping has a dirty watermark at its header, while
        // a pooled mapping carries the previous high-water mark and therefore
        // must clear the old cache prefix before probing it.  Compile through
        // both lifetimes and compare the bytes to catch a stale template hit.
        {
            ArenaCreation pooled_codegen_creation = {
                .reserved_size = BUSTER_MB(64),
                .initial_size = BUSTER_MB(1),
                .flags = {.pool_reuse = 1},
            };
            arena_pool_release_thread();
            Arena* pooled_first_arena = arena_create(pooled_codegen_creation);
            BUSTER_TEST(arguments, pooled_first_arena != 0);
            if (pooled_first_arena)
            {
                CodegenModule pooled_first = codegen_generate_canonical_module(
                    pooled_first_arena, canonical_program, canonical_module, canonical_windows_target,
                    (CodegenModuleOptions){
                        .assume_validated = true,
                    });
                BUSTER_TEST(arguments, pooled_first.error == CODEGEN_ERROR_NONE);
                ByteSlice pooled_expected_code = {0};
                if (pooled_first.error == CODEGEN_ERROR_NONE)
                {
                    pooled_expected_code.length = pooled_first.code.length;
                    pooled_expected_code.pointer = arena_allocate(arguments->arena, u8, pooled_expected_code.length);
                    memcpy(pooled_expected_code.pointer, pooled_first.code.pointer, pooled_expected_code.length);
                }
                BUSTER_TEST(arguments, arena_dirty_position(pooled_first_arena) > arena_minimum_position);
                BUSTER_TEST(arguments, arena_destroy(pooled_first_arena, 1));

                Arena* pooled_second_arena = arena_create(pooled_codegen_creation);
                BUSTER_TEST(arguments, pooled_second_arena != 0);
                if (pooled_second_arena)
                {
                    CodegenModule pooled_second = codegen_generate_canonical_module(
                        pooled_second_arena, canonical_program, canonical_module, canonical_windows_target,
                        (CodegenModuleOptions){
                            .assume_validated = true,
                        });
                    BUSTER_TEST(arguments, pooled_second.error == CODEGEN_ERROR_NONE);
                    BUSTER_TEST(arguments, pooled_second.code.length == pooled_expected_code.length);
                    if (pooled_second.error == CODEGEN_ERROR_NONE && pooled_second.code.length == pooled_expected_code.length)
                    {
                        BUSTER_TEST(arguments, !memcmp(pooled_second.code.pointer, pooled_expected_code.pointer, pooled_expected_code.length));
                    }
                    BUSTER_TEST(arguments, arena_destroy(pooled_second_arena, 1));
                }
                arena_pool_release_thread();
            }
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
                    BUSTER_TEST(arguments, codegen_module_relocation_valid(relocation));
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
                BUSTER_TEST(arguments, codegen_module_relocation_valid(relocation));
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
            found_stack_realignment |= code[0] == 0x48 && code[1] == 0x83 && code[2] == 0xe4 && code[3] == (u8)(0 - (u8)64);
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
            // through r8/r9/r10, so `lea r8|r9|r10, [rbp+disp]` names its
            // operand and result slots exactly. The displacement may be a
            // canonical disp8 or disp32 depending on the slot offset.
            u8* vector_frame_code = vector_frame_module.code.pointer + vector_frame_descriptor->code_offset;
            bool vector_frame_displacements_valid = true;
            bool dynamic_frame = vector_frame_windows && string_equal(vector_frame_names[name_index], S8("vector_with_loop"));
            u32 vector_frame_lea_count = codegen_test_x64_vector_frame_lea_count(
                (ByteSlice){.pointer = vector_frame_code, .length = vector_frame_descriptor->code_size}, dynamic_frame,
                &vector_frame_displacements_valid);
            BUSTER_TEST(arguments, vector_frame_lea_count >= 3);
            BUSTER_TEST(arguments, vector_frame_displacements_valid);
        }
    }
    // A 1-, 2- or 4-byte vector rides a general-purpose register on System V:
    // GCC deviates from a literal psABI reading for vectors smaller than an
    // eightbyte and clang follows, so INTEGER is the convention, bare or
    // wrapped in a struct. It is also what makes these compile at all -- the
    // canonical emitter's vector moves start at four bytes, so an SSE-classed
    // two-byte part reported CODEGEN_ERROR_UNSUPPORTED_ABI from every caller.
    // Eight bytes up stays SSE, and AAPCS64 keeps its vector classes.
    String8 tiny_vector_c_source = S8(
        "typedef signed char V1 __attribute__((vector_size(1)));\n"
        "typedef signed char V2 __attribute__((vector_size(2)));\n"
        "typedef signed char V4 __attribute__((vector_size(4)));\n"
        "typedef signed char V8 __attribute__((vector_size(8)));\n"
        "typedef float F1 __attribute__((vector_size(4)));\n"
        "typedef struct TinyWrap { V2 lanes; } TinyWrap;\n"
        "static V1 tiny_one(V1 value) { return value + value; }\n"
        "static V2 tiny_identity(V2 value) { return value; }\n"
        "static V4 tiny_four(V4 value) { return value; }\n"
        "static F1 tiny_float(F1 value) { return value + value; }\n"
        "static V8 wide_eight(V8 value) { return value; }\n"
        "static TinyWrap tiny_wrap(TinyWrap value) { value.lanes += value.lanes; return value; }\n"
        "V2 tiny_calls(V2 value) {\n"
        "    TinyWrap wrapped = { value };\n"
        "    wrapped = tiny_wrap(wrapped);\n"
        "    V1 one_value = { 3 };\n"
        "    V1 one = tiny_one(one_value);\n"
        "    V4 four_value = { 1, 2, 3, 4 };\n"
        "    V4 four = tiny_four(four_value);\n"
        "    F1 float_value = { 1.5f };\n"
        "    F1 floated = tiny_float(float_value);\n"
        "    V8 eight_value = { 1, 2, 3, 4, 5, 6, 7, 8 };\n"
        "    V8 eight = wide_eight(eight_value);\n"
        "    V2 result = tiny_identity(wrapped.lanes);\n"
        "    result[0] += one[0] + four[2] + (signed char)floated[0];\n"
        "    result[1] += eight[5];\n"
        "    return result;\n"
        "}\n");
    Target tiny_vector_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_LINUX,
    };
    CPreprocessResult tiny_vector_tokens = c_preprocess(arguments->arena, tiny_vector_c_source, (CPreprocessOptions){0});
    CParseResult tiny_vector_parse = c_parse(arguments->arena, tiny_vector_tokens);
    CIRLowerResult tiny_vector_ir =
        c_lower_to_ir(arguments->arena, S8("system-v-tiny-vector.c"), tiny_vector_tokens, tiny_vector_parse, tiny_vector_target);
    BUSTER_TEST(arguments, tiny_vector_tokens.error_count == 0);
    BUSTER_TEST(arguments, tiny_vector_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, tiny_vector_ir.diagnostic_count == 0);
    if (tiny_vector_ir.program)
    {
        IrProgram* tiny_vector_program = tiny_vector_ir.program;
        CodegenModule tiny_vector_module = codegen_generate_canonical_module(arguments->arena, tiny_vector_program, tiny_vector_program->modules,
                                                                              tiny_vector_target, (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, tiny_vector_module.error == CODEGEN_ERROR_NONE);
        u32 tiny_vector_class_count = 0;
        u32 wide_vector_class_count = 0;
        for (u32 type_index = 0; type_index < tiny_vector_program->types.count; type_index += 1)
        {
            IrType* type = tiny_vector_program->types.types + type_index;
            if (type->kind != IR_TYPE_VECTOR || !type->layout.resolved)
            {
                continue;
            }
            IrAbiValue argument_abi = ir_type_abi_value(tiny_vector_program, type->id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
            IrAbiValue result_abi = ir_type_abi_value(tiny_vector_program, type->id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
            IrAbiValue aapcs_abi = ir_type_abi_value(tiny_vector_program, type->id, IR_ABI_CONVENTION_AAPCS64, IR_ABI_USE_ARGUMENT);
            BUSTER_TEST(arguments, argument_abi.part_count == 1 && !argument_abi.indirect && !argument_abi.memory);
            BUSTER_TEST(arguments, result_abi.part_count == 1 && !result_abi.indirect && !result_abi.memory);
            BUSTER_TEST(arguments, argument_abi.parts[0].size == (u32)type->layout.size);
            IrAbiClass expected_class = type->layout.size < 8 ? IR_ABI_CLASS_INTEGER : IR_ABI_CLASS_VECTOR;
            BUSTER_TEST(arguments, argument_abi.parts[0].abi_class == expected_class);
            BUSTER_TEST(arguments, result_abi.parts[0].abi_class == expected_class);
            BUSTER_TEST(arguments, aapcs_abi.part_count == 1 && aapcs_abi.parts[0].abi_class == IR_ABI_CLASS_VECTOR);
            tiny_vector_class_count += type->layout.size < 8;
            wide_vector_class_count += type->layout.size >= 8;
        }
        BUSTER_TEST(arguments, tiny_vector_class_count >= 4);
        BUSTER_TEST(arguments, wide_vector_class_count >= 1);
        for (u32 type_index = 0; type_index < tiny_vector_program->types.count; type_index += 1)
        {
            IrType* type = tiny_vector_program->types.types + type_index;
            if (type->kind != IR_TYPE_STRUCT || !string_equal(type->name, S8("TinyWrap")))
            {
                continue;
            }
            IrAbiValue wrap_abi = ir_type_abi_value(tiny_vector_program, type->id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT);
            BUSTER_TEST(arguments, wrap_abi.part_count == 1 && !wrap_abi.indirect && !wrap_abi.memory);
            BUSTER_TEST(arguments, wrap_abi.parts[0].abi_class == IR_ABI_CLASS_INTEGER && wrap_abi.parts[0].size == 2);
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
    // Zero-fill layout: offsets are planned small-first rather than in
    // declaration order, because a global declared after a ~2GiB array would
    // otherwise sit past RIP-relative +/-2^31 reach and fail the link with
    // LINK_ERROR_RELOCATION. The second array is exactly
    // CODEGEN_LARGE_ZERO_FILL_THRESHOLD bytes to pin the boundary as large;
    // no byte of it is ever allocated, since zero-fill sections carry no data.
    String8 zero_fill_layout_c_source = S8(
        "static volatile int first_small;\n"
        "static char first_large[2147483644];\n"
        "static volatile int middle_small;\n"
        "static char second_large[65536];\n"
        "static volatile int last_small;\n"
        "void touch_zero_fill(void) {\n"
        "    first_small = 1;\n"
        "    first_large[0] = 1;\n"
        "    middle_small = 2;\n"
        "    second_large[0] = 2;\n"
        "    last_small = 3;\n"
        "}\n");
    CPreprocessResult zero_fill_layout_tokens = c_preprocess(arguments->arena, zero_fill_layout_c_source, (CPreprocessOptions){0});
    CParseResult zero_fill_layout_parse = c_parse(arguments->arena, zero_fill_layout_tokens);
    CIRLowerResult zero_fill_layout_ir =
        c_lower_to_ir(arguments->arena, S8("zero-fill-layout.c"), zero_fill_layout_tokens, zero_fill_layout_parse, target);
    BUSTER_TEST(arguments, zero_fill_layout_tokens.error_count == 0);
    BUSTER_TEST(arguments, zero_fill_layout_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, zero_fill_layout_ir.diagnostic_count == 0);
    if (zero_fill_layout_ir.program)
    {
        IrProgram* zero_fill_layout_program = zero_fill_layout_ir.program;
        IrModule* zero_fill_layout_module = zero_fill_layout_program->modules;
        BUSTER_TEST(arguments, ir_validate_canonical_module(zero_fill_layout_program, zero_fill_layout_module).error == IR_VALIDATION_NONE);
        CodegenModule zero_fill_layout_codegen = codegen_generate_canonical_module(arguments->arena, zero_fill_layout_program, zero_fill_layout_module,
                                                                                   target, (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, zero_fill_layout_codegen.error == CODEGEN_ERROR_NONE);
        CodegenModuleGlobal* first_small = codegen_test_c_global_find(&zero_fill_layout_codegen, zero_fill_layout_program, S8("first_small"));
        CodegenModuleGlobal* first_large = codegen_test_c_global_find(&zero_fill_layout_codegen, zero_fill_layout_program, S8("first_large"));
        CodegenModuleGlobal* middle_small = codegen_test_c_global_find(&zero_fill_layout_codegen, zero_fill_layout_program, S8("middle_small"));
        CodegenModuleGlobal* second_large = codegen_test_c_global_find(&zero_fill_layout_codegen, zero_fill_layout_program, S8("second_large"));
        CodegenModuleGlobal* last_small = codegen_test_c_global_find(&zero_fill_layout_codegen, zero_fill_layout_program, S8("last_small"));
        BUSTER_TEST(arguments, first_small && first_large && middle_small && second_large && last_small);
        if (first_small && first_large && middle_small && second_large && last_small)
        {
            BUSTER_TEST(arguments, first_small->zero_fill && first_large->zero_fill && middle_small->zero_fill && second_large->zero_fill &&
                                       last_small->zero_fill);
            BUSTER_TEST(arguments, first_large->size >= CODEGEN_LARGE_ZERO_FILL_THRESHOLD);
            BUSTER_TEST(arguments, second_large->size == CODEGEN_LARGE_ZERO_FILL_THRESHOLD);
            // The small globals keep declaration order at the bottom of the
            // section, wholly below both large arrays.
            BUSTER_TEST(arguments, first_small->offset + first_small->size <= middle_small->offset);
            BUSTER_TEST(arguments, middle_small->offset + middle_small->size <= last_small->offset);
            BUSTER_TEST(arguments, last_small->offset + last_small->size <= first_large->offset);
            BUSTER_TEST(arguments, last_small->offset + last_small->size <= second_large->offset);
            // The large arrays follow in declaration order without overlap.
            BUSTER_TEST(arguments, (u64)first_large->offset + first_large->size <= second_large->offset);
            BUSTER_TEST(arguments, zero_fill_layout_codegen.zero_fill_size >= (u64)second_large->offset + second_large->size);
        }
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
        CodegenModule deterministic_first = codegen_generate_canonical_module(
            arguments->arena, deterministic_program, deterministic_module, target,
            (CodegenModuleOptions){
                .debug_info = true,
                .assume_validated = true,
            });
        CodegenModule deterministic_second = codegen_generate_canonical_module(
            arguments->arena, deterministic_program, deterministic_module, target,
            (CodegenModuleOptions){
                .debug_info = true,
                .assume_validated = true,
            });
        BUSTER_TEST(arguments, deterministic_first.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, deterministic_second.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, deterministic_first.code.length >= 4096);
        if (deterministic_first.error == CODEGEN_ERROR_NONE && deterministic_second.error == CODEGEN_ERROR_NONE)
        {
            ObjectFile first_object = object_from_canonical_codegen_module(arguments->arena, deterministic_program, &deterministic_first, target);
            ObjectFile second_object = object_from_canonical_codegen_module(arguments->arena, deterministic_program, &deterministic_second, target);
            ObjectFormat format = object_format_for_target(target);
            ObjectArtifact first_artifact = object_write(arguments->arena, &first_object, format);
            ObjectArtifact second_artifact = object_write(arguments->arena, &second_object, format);
            BUSTER_TEST(arguments, first_artifact.error == OBJECT_ERROR_NONE);
            BUSTER_TEST(arguments, second_artifact.error == OBJECT_ERROR_NONE);
            BUSTER_TEST(arguments, first_artifact.bytes.length == second_artifact.bytes.length);
            if (first_artifact.bytes.length == second_artifact.bytes.length)
            {
                BUSTER_TEST(arguments, memcmp(first_artifact.bytes.pointer, second_artifact.bytes.pointer, first_artifact.bytes.length) == 0);
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
    return result;
}
#endif
