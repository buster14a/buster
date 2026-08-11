#include <buster/lib/compiler/assembly/aarch64_encoding.h>

#define A64_NO_PC_RELATIVE_OPERAND UINT8_MAX

BUSTER_GLOBAL_LOCAL A64OpcodeDescriptor const a64_opcode_descriptors[A64_OPCODE_COUNT] = {
    [A64_OPCODE_NOP] =
        {
            .fixed_mask = UINT32_MAX,
            .fixed_value = UINT32_C(0xd503201f),
            .pc_relative_operand = A64_NO_PC_RELATIVE_OPERAND,
        },
    [A64_OPCODE_B] =
        {
            .fixed_mask = UINT32_C(0xfc000000),
            .fixed_value = UINT32_C(0x14000000),
            .operand_count = 1,
            .pc_relative_operand = 0,
            .pc_relative_layout = A64_PC_RELATIVE_IMM26,
        },
    [A64_OPCODE_BL] =
        {
            .fixed_mask = UINT32_C(0xfc000000),
            .fixed_value = UINT32_C(0x94000000),
            .operand_count = 1,
            .pc_relative_operand = 0,
            .pc_relative_layout = A64_PC_RELATIVE_IMM26,
        },
    [A64_OPCODE_B_COND] =
        {
            .fixed_mask = UINT32_C(0xff000010),
            .fixed_value = UINT32_C(0x54000000),
            .operand_count = 2,
            .pc_relative_operand = 0,
            .pc_relative_layout = A64_PC_RELATIVE_IMM19,
        },
    [A64_OPCODE_RET] =
        {
            .fixed_mask = UINT32_C(0xfffffc1f),
            .fixed_value = UINT32_C(0xd65f0000),
            .operand_count = 1,
            .pc_relative_operand = A64_NO_PC_RELATIVE_OPERAND,
        },
    [A64_OPCODE_BR] =
        {
            .fixed_mask = UINT32_C(0xfffffc1f),
            .fixed_value = UINT32_C(0xd61f0000),
            .operand_count = 1,
            .pc_relative_operand = A64_NO_PC_RELATIVE_OPERAND,
        },
    [A64_OPCODE_BLR] =
        {
            .fixed_mask = UINT32_C(0xfffffc1f),
            .fixed_value = UINT32_C(0xd63f0000),
            .operand_count = 1,
            .pc_relative_operand = A64_NO_PC_RELATIVE_OPERAND,
        },
    [A64_OPCODE_LDR_LITERAL_64] =
        {
            .fixed_mask = UINT32_C(0xff000000),
            .fixed_value = UINT32_C(0x58000000),
            .operand_count = 2,
            .pc_relative_operand = 1,
            .pc_relative_layout = A64_PC_RELATIVE_IMM19,
        },
    [A64_OPCODE_ADRP] =
        {
            .fixed_mask = UINT32_C(0x9f000000),
            .fixed_value = UINT32_C(0x90000000),
            .operand_count = 2,
            .pc_relative_operand = 1,
            .pc_relative_layout = A64_PC_RELATIVE_ADRP,
        },
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(a64_opcode_descriptors) == A64_OPCODE_COUNT);

A64OpcodeDescriptor const* a64_opcode_descriptor(A64Opcode opcode)
{
    if (!opcode || opcode >= A64_OPCODE_COUNT)
    {
        return 0;
    }
    return a64_opcode_descriptors + opcode;
}

bool a64_signed_scaled_immediate_encode(s64 value, u8 bits, u8 scale_log2, u32* encoded)
{
    if (!encoded || !bits || bits > 32 || scale_log2 > 31 || (u32)bits + scale_log2 > 63)
    {
        return false;
    }
    u64 scale = UINT64_C(1) << scale_log2;
    if ((u64)value & (scale - 1))
    {
        return false;
    }
    s64 scaled = value / (s64)scale;
    s64 minimum = -(INT64_C(1) << (bits - 1));
    s64 maximum = (INT64_C(1) << (bits - 1)) - 1;
    if (scaled < minimum || scaled > maximum)
    {
        return false;
    }
    u64 mask = (UINT64_C(1) << bits) - 1;
    *encoded = (u32)((u64)scaled & mask);
    return true;
}

bool a64_signed_scaled_immediate_decode(u32 encoded, u8 bits, u8 scale_log2, s64* value)
{
    if (!value || !bits || bits > 32 || scale_log2 > 31 || (u32)bits + scale_log2 > 63)
    {
        return false;
    }
    u64 limit = UINT64_C(1) << bits;
    u64 mask = limit - 1;
    if ((u64)encoded & ~mask)
    {
        return false;
    }
    u64 sign = limit >> 1;
    s64 scaled = (u64)encoded < sign ? (s64)encoded : -(s64)(limit - encoded);
    *value = scaled * (s64)(UINT64_C(1) << scale_log2);
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_mc_operand(A64MCInst const* instruction, u32 index, A64MCOperandKind kind, s64* value)
{
    if (!instruction || index >= instruction->operand_count || index >= A64_MC_MAX_OPERANDS || instruction->operands[index].kind != kind)
    {
        return false;
    }
    if (value)
    {
        *value = instruction->operands[index].value;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool a64_pc_relative_insert(A64PCRelativeLayout layout, u32 word, s64 displacement, u32* patched)
{
    u32 immediate = 0;
    if (!patched)
    {
        return false;
    }
    switch (layout)
    {
    case A64_PC_RELATIVE_IMM26:
        if (!a64_signed_scaled_immediate_encode(displacement, 26, 2, &immediate))
        {
            return false;
        }
        *patched = (word & ~UINT32_C(0x03ffffff)) | immediate;
        return true;
    case A64_PC_RELATIVE_IMM19:
        if (!a64_signed_scaled_immediate_encode(displacement, 19, 2, &immediate))
        {
            return false;
        }
        *patched = (word & ~UINT32_C(0x00ffffe0)) | (immediate << 5);
        return true;
    case A64_PC_RELATIVE_ADRP:
        if (!a64_signed_scaled_immediate_encode(displacement, 21, 12, &immediate))
        {
            return false;
        }
        *patched = (word & ~UINT32_C(0x60ffffe0)) | ((immediate & 3) << 29) | (((immediate >> 2) & UINT32_C(0x7ffff)) << 5);
        return true;
    case A64_PC_RELATIVE_NONE:
    case A64_PC_RELATIVE_LAYOUT_COUNT:
        return false;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool a64_pc_relative_extract(A64PCRelativeLayout layout, u32 word, s64* displacement)
{
    u32 immediate = 0;
    switch (layout)
    {
    case A64_PC_RELATIVE_IMM26:
        immediate = word & UINT32_C(0x03ffffff);
        return a64_signed_scaled_immediate_decode(immediate, 26, 2, displacement);
    case A64_PC_RELATIVE_IMM19:
        immediate = (word >> 5) & UINT32_C(0x7ffff);
        return a64_signed_scaled_immediate_decode(immediate, 19, 2, displacement);
    case A64_PC_RELATIVE_ADRP:
        immediate = ((word >> 29) & 3) | (((word >> 5) & UINT32_C(0x7ffff)) << 2);
        return a64_signed_scaled_immediate_decode(immediate, 21, 12, displacement);
    case A64_PC_RELATIVE_NONE:
    case A64_PC_RELATIVE_LAYOUT_COUNT:
        return false;
    }
    return false;
}

bool a64_pc_relative_patch(A64Opcode opcode, u32 word, s64 displacement, u32* patched)
{
    A64OpcodeDescriptor const* descriptor = a64_opcode_descriptor(opcode);
    return descriptor && descriptor->pc_relative_operand != A64_NO_PC_RELATIVE_OPERAND && (word & descriptor->fixed_mask) == descriptor->fixed_value &&
           a64_pc_relative_insert((A64PCRelativeLayout)descriptor->pc_relative_layout, word, displacement, patched);
}

bool a64_mc_encode(A64MCInst const* instruction, u32* word)
{
    if (!instruction || !word)
    {
        return false;
    }
    A64OpcodeDescriptor const* descriptor = a64_opcode_descriptor(instruction->opcode);
    if (!descriptor || instruction->operand_count != descriptor->operand_count)
    {
        return false;
    }
    u32 result = descriptor->fixed_value;
    s64 value = 0;
    switch (instruction->opcode)
    {
    case A64_OPCODE_NOP:
        break;
    case A64_OPCODE_B:
    case A64_OPCODE_BL:
        if (!a64_mc_operand(instruction, 0, A64_MC_OPERAND_PC_RELATIVE, &value) || !a64_pc_relative_insert(A64_PC_RELATIVE_IMM26, result, value, &result))
        {
            return false;
        }
        break;
    case A64_OPCODE_B_COND:
        if (!a64_mc_operand(instruction, 0, A64_MC_OPERAND_PC_RELATIVE, &value) || !a64_pc_relative_insert(A64_PC_RELATIVE_IMM19, result, value, &result) ||
            !a64_mc_operand(instruction, 1, A64_MC_OPERAND_IMMEDIATE, &value) || value < 0 || value > 15)
        {
            return false;
        }
        result |= (u32)value;
        break;
    case A64_OPCODE_RET:
    case A64_OPCODE_BR:
    case A64_OPCODE_BLR:
        if (!a64_mc_operand(instruction, 0, A64_MC_OPERAND_REGISTER, &value) || value < 0 || value > 31)
        {
            return false;
        }
        result |= (u32)value << 5;
        break;
    case A64_OPCODE_LDR_LITERAL_64:
        if (!a64_mc_operand(instruction, 0, A64_MC_OPERAND_REGISTER, &value) || value < 0 || value > 31)
        {
            return false;
        }
        result |= (u32)value;
        if (!a64_mc_operand(instruction, 1, A64_MC_OPERAND_PC_RELATIVE, &value) || !a64_pc_relative_insert(A64_PC_RELATIVE_IMM19, result, value, &result))
        {
            return false;
        }
        break;
    case A64_OPCODE_ADRP:
        if (!a64_mc_operand(instruction, 0, A64_MC_OPERAND_REGISTER, &value) || value < 0 || value > 31)
        {
            return false;
        }
        result |= (u32)value;
        if (!a64_mc_operand(instruction, 1, A64_MC_OPERAND_PC_RELATIVE, &value) || !a64_pc_relative_insert(A64_PC_RELATIVE_ADRP, result, value, &result))
        {
            return false;
        }
        break;
    case A64_OPCODE_INVALID:
    case A64_OPCODE_COUNT:
        return false;
    }
    *word = result;
    return true;
}

BUSTER_GLOBAL_LOCAL A64MCOperand a64_mc_operand_make(A64MCOperandKind kind, s64 value)
{
    return (A64MCOperand){.value = value, .kind = (u8)kind};
}

bool a64_mc_decode(u32 word, A64MCInst* instruction)
{
    if (!instruction)
    {
        return false;
    }
    // Specific fixed forms precede the broad PC-relative masks. The current
    // descriptors have no decode collisions; this explicit order becomes the
    // seed for the generated decoder's priority table.
    static A64Opcode const decode_order[] = {
        A64_OPCODE_NOP,  A64_OPCODE_RET, A64_OPCODE_BR, A64_OPCODE_BLR, A64_OPCODE_B_COND, A64_OPCODE_LDR_LITERAL_64,
        A64_OPCODE_ADRP, A64_OPCODE_BL,  A64_OPCODE_B,
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(decode_order); index += 1)
    {
        A64Opcode opcode = decode_order[index];
        A64OpcodeDescriptor const* descriptor = a64_opcode_descriptor(opcode);
        if ((word & descriptor->fixed_mask) != descriptor->fixed_value)
        {
            continue;
        }
        A64MCInst result = {.opcode = opcode, .operand_count = descriptor->operand_count};
        s64 displacement = 0;
        switch (opcode)
        {
        case A64_OPCODE_NOP:
            break;
        case A64_OPCODE_B:
        case A64_OPCODE_BL:
            if (!a64_pc_relative_extract(A64_PC_RELATIVE_IMM26, word, &displacement))
            {
                return false;
            }
            result.operands[0] = a64_mc_operand_make(A64_MC_OPERAND_PC_RELATIVE, displacement);
            break;
        case A64_OPCODE_B_COND:
            if (!a64_pc_relative_extract(A64_PC_RELATIVE_IMM19, word, &displacement))
            {
                return false;
            }
            result.operands[0] = a64_mc_operand_make(A64_MC_OPERAND_PC_RELATIVE, displacement);
            result.operands[1] = a64_mc_operand_make(A64_MC_OPERAND_IMMEDIATE, word & 15);
            break;
        case A64_OPCODE_RET:
        case A64_OPCODE_BR:
        case A64_OPCODE_BLR:
        {
            u32 register_number = (word >> 5) & 31;
            result.operands[0] = a64_mc_operand_make(A64_MC_OPERAND_REGISTER, register_number);
        }
        break;
        case A64_OPCODE_LDR_LITERAL_64:
            if (!a64_pc_relative_extract(A64_PC_RELATIVE_IMM19, word, &displacement))
            {
                return false;
            }
            result.operands[0] = a64_mc_operand_make(A64_MC_OPERAND_REGISTER, word & 31);
            result.operands[1] = a64_mc_operand_make(A64_MC_OPERAND_PC_RELATIVE, displacement);
            break;
        case A64_OPCODE_ADRP:
            if (!a64_pc_relative_extract(A64_PC_RELATIVE_ADRP, word, &displacement))
            {
                return false;
            }
            result.operands[0] = a64_mc_operand_make(A64_MC_OPERAND_REGISTER, word & 31);
            result.operands[1] = a64_mc_operand_make(A64_MC_OPERAND_PC_RELATIVE, displacement);
            break;
        case A64_OPCODE_INVALID:
        case A64_OPCODE_COUNT:
            return false;
        }
        *instruction = result;
        return true;
    }
    return false;
}

bool a64_pc_relative_displacement(u64 target, u64 place, s64 addend, s64* displacement)
{
    if (!displacement)
    {
        return false;
    }
    bool addend_negative = addend < 0;
    u64 addend_magnitude = addend_negative ? (u64)(-(addend + 1)) + 1 : (u64)addend;
    bool result_negative = false;
    u64 result_magnitude = 0;
    if (target >= place)
    {
        u64 difference = target - place;
        if (!addend_negative)
        {
            if (difference > (u64)INT64_MAX - addend_magnitude)
            {
                return false;
            }
            result_magnitude = difference + addend_magnitude;
        }
        else if (difference >= addend_magnitude)
        {
            result_magnitude = difference - addend_magnitude;
            if (result_magnitude > (u64)INT64_MAX)
            {
                return false;
            }
        }
        else
        {
            result_negative = true;
            result_magnitude = addend_magnitude - difference;
        }
    }
    else
    {
        u64 difference = place - target;
        if (!addend_negative)
        {
            if (addend_magnitude >= difference)
            {
                result_magnitude = addend_magnitude - difference;
            }
            else
            {
                result_negative = true;
                result_magnitude = difference - addend_magnitude;
                if (result_magnitude > (u64)INT64_MAX + 1)
                {
                    return false;
                }
            }
        }
        else
        {
            if (difference > (u64)INT64_MAX + 1 - addend_magnitude)
            {
                return false;
            }
            result_negative = true;
            result_magnitude = difference + addend_magnitude;
        }
    }
    if (result_negative)
    {
        *displacement = result_magnitude == (u64)INT64_MAX + 1 ? INT64_MIN : -(s64)result_magnitude;
    }
    else
    {
        *displacement = (s64)result_magnitude;
    }
    return true;
}

bool a64_adrp_encode(u32 destination_register, u64 instruction_address, u64 target_address, u32* word)
{
    if (!word || destination_register > 31)
    {
        return false;
    }
    u64 instruction_page = instruction_address & ~UINT64_C(0xfff);
    u64 target_page = target_address & ~UINT64_C(0xfff);
    u64 delta = target_page - instruction_page;
    s64 displacement = 0;
    if (delta <= UINT64_C(0xfffff000))
    {
        displacement = (s64)delta;
    }
    else
    {
        u64 distance = 0 - delta;
        if (distance > UINT64_C(0x100000000))
        {
            return false;
        }
        displacement = -(s64)distance;
    }
    A64MCInst instruction = {
        .operands =
            {
                {.value = destination_register, .kind = A64_MC_OPERAND_REGISTER},
                {.value = displacement, .kind = A64_MC_OPERAND_PC_RELATIVE},
            },
        .opcode = A64_OPCODE_ADRP,
        .operand_count = 2,
    };
    return a64_mc_encode(&instruction, word);
}

bool a64_condition_invert(u32 condition, u32* inverse)
{
    if (!inverse || condition > 13)
    {
        return false;
    }
    *inverse = condition ^ 1;
    return true;
}
